/**
 * @file    stepmotor_bus.c
 * @brief   步进电机专用串口总线服务模块实现
 *
 * 本文件实现 UART_STEPPER_BUS 上的步进电机收发调度逻辑。
 * 原始字节由 StepmotorUart Driver FIFO 缓存；本模块只保留任务态解析状态。
 */

#include "app/tasks/platform_2d/stepmotor_bus.h"

#include "driver/board_uart/stepmotor_uart.h"
#include "driver/clock/clock.h"
#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "driver/step_motor/emm42.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define STEPMOTOR_BUS_MGMT_QUEUE_DEPTH          8u
#define STEPMOTOR_BUS_TX_FRAME_MAX_LEN          32u
#define STEPMOTOR_BUS_INTERBYTE_TIMEOUT_MS      3u
#define STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN       4u
#define STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN 6u
#define STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK       0x6Bu
#define STEPMOTOR_BUS_STEPMOTOR_ADDR_Y          1u
#define STEPMOTOR_BUS_STEPMOTOR_ADDR_X          2u
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ENABLE      0xF3u
#define STEPMOTOR_BUS_STEPMOTOR_CMD_GRIP        0xF5u
#define STEPMOTOR_BUS_STEPMOTOR_CMD_SPEED       0xF6u
#define STEPMOTOR_BUS_STEPMOTOR_CMD_POS_ACK     0xFBu
#define STEPMOTOR_BUS_STEPMOTOR_CMD_POSITION    0xFDu
#define STEPMOTOR_BUS_STEPMOTOR_CMD_PID_CFG     0x4Au
#define STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED  0x35u
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_SET  0x93u
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_RUN  0x9Au
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_QUIT 0x9Cu
#define STEPMOTOR_BUS_STEPMOTOR_RET_OK          0x02u
#define STEPMOTOR_BUS_STEPMOTOR_RET_HOME        0x12u
#define STEPMOTOR_BUS_STEPMOTOR_RET_PARAM_ERR   0xE2u
#define STEPMOTOR_BUS_STEPMOTOR_RET_FRAME_ERR   0xEEu
#define STEPMOTOR_BUS_STEPMOTOR_RET_DONE        0x9Fu
#define STEPMOTOR_BUS_READ_CHUNK_SIZE           32u
/* 230400 baud, longest legal frame 32 bytes:
 * ceil(32 * 10 / 230400 s) = 2 ms; wait = 2 * frame_time + 5 ms service = 9 ms. */
#define STEPMOTOR_BUS_EXCLUSIVE_WAIT_MS         9u

typedef struct {
    uint8_t data[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t length;
} StepmotorBus_TxFrame_t;

typedef struct {
    StepmotorBus_TxFrame_t frames[STEPMOTOR_BUS_MGMT_QUEUE_DEPTH];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} StepmotorBus_MgmtQueue_t;

typedef struct {
    StepmotorBus_TxFrame_t frame;
    bool dirty;
} StepmotorBus_CtrlSlot_t;

typedef enum {
    STEPMOTOR_BUS_CTRL_AXIS_Y = 0,
    STEPMOTOR_BUS_CTRL_AXIS_X,
    STEPMOTOR_BUS_CTRL_AXIS_MAX
} StepmotorBus_CtrlAxis_e;

typedef enum {
    STEPMOTOR_BUS_TX_SRC_NONE = 0,
    STEPMOTOR_BUS_TX_SRC_CONTROL,
    STEPMOTOR_BUS_TX_SRC_MGMT
} StepmotorBus_TxSource_e;

typedef struct {
    uint8_t data[STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN];
    uint8_t count;
    uint32_t last_rx_tick;
} StepmotorBus_RxState_t;

static StepmotorBus_RxState_t s_stepmotor_rx_state;
static StepmotorBus_MgmtQueue_t s_stepmotor_mgmt_queue;
static StepmotorBus_CtrlSlot_t s_stepmotor_ctrl_slots[STEPMOTOR_BUS_CTRL_AXIS_MAX];
static bool s_stepmotor_tx_dispatch_busy = false;
static StepmotorBus_CtrlAxis_e s_stepmotor_next_ctrl_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
static bool s_stepmotor_prefer_mgmt_next = false;
static bool s_stepmotor_control_gate_enabled = false;
static uint32_t s_stepmotor_control_error_count = 0u;
static uint8_t s_stepmotor_last_return_code = 0u;
static int32_t s_stepmotor_last_speed_raw[STEPMOTOR_BUS_CTRL_AXIS_MAX] = {0};
static int32_t s_stepmotor_last_speed_rpm[STEPMOTOR_BUS_CTRL_AXIS_MAX] = {0};
static bool s_stepmotor_bypass = false;

static void stepmotor_bus_control_error_inc(void)
{
    if (s_stepmotor_control_error_count < UINT32_MAX) {
        s_stepmotor_control_error_count++;
    }
}

static bool stepmotor_bus_is_stepmotor_addr(uint8_t addr)
{
    return (bool)((addr == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) ||
                  (addr == STEPMOTOR_BUS_STEPMOTOR_ADDR_X));
}

static bool stepmotor_bus_is_stepmotor_code(uint8_t code)
{
    return (bool)((code == STEPMOTOR_BUS_STEPMOTOR_CMD_PID_CFG) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_ENABLE) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_GRIP) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_SPEED) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_POS_ACK) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_POSITION) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_SET) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_RUN) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_QUIT) ||
                  (code == STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED));
}

static bool stepmotor_bus_is_stepmotor_data(uint8_t data)
{
    return (bool)((data == STEPMOTOR_BUS_STEPMOTOR_RET_OK) ||
                  (data == STEPMOTOR_BUS_STEPMOTOR_RET_HOME) ||
                  (data == STEPMOTOR_BUS_STEPMOTOR_RET_PARAM_ERR) ||
                  (data == STEPMOTOR_BUS_STEPMOTOR_RET_FRAME_ERR) ||
                  (data == STEPMOTOR_BUS_STEPMOTOR_RET_DONE));
}

static bool stepmotor_bus_is_error_return_code(uint8_t data)
{
    return (bool)((data == STEPMOTOR_BUS_STEPMOTOR_RET_PARAM_ERR) ||
                  (data == STEPMOTOR_BUS_STEPMOTOR_RET_FRAME_ERR));
}

static void stepmotor_bus_rx_reset(void)
{
    memset(&s_stepmotor_rx_state, 0, sizeof(s_stepmotor_rx_state));
}

static void stepmotor_bus_drop_prefix(uint8_t drop_count)
{
    if (drop_count >= s_stepmotor_rx_state.count) {
        s_stepmotor_rx_state.count = 0u;
        return;
    }

    memmove(s_stepmotor_rx_state.data,
            &s_stepmotor_rx_state.data[drop_count],
            (size_t)(s_stepmotor_rx_state.count - drop_count));
    s_stepmotor_rx_state.count =
        (uint8_t)(s_stepmotor_rx_state.count - drop_count);
}

static bool stepmotor_bus_try_handle_speed_reply(void)
{
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    uint16_t raw_speed = 0u;
    int32_t signed_speed_raw = 0;
    int32_t signed_speed_rpm = 0;
    uint8_t *frame = s_stepmotor_rx_state.data;

    if (s_stepmotor_rx_state.count < STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN) {
        return false;
    }

    if ((stepmotor_bus_is_stepmotor_addr(frame[0]) == false) ||
        (frame[1] != STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) ||
        ((frame[2] != 0x00u) && (frame[2] != 0x01u)) ||
        (frame[5] != STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
        stepmotor_bus_drop_prefix(1u);
        return true;
    }

    raw_speed = (uint16_t)(((uint16_t)frame[3] << 8) | (uint16_t)frame[4]);
    signed_speed_raw = (frame[2] == 0x01u) ? (int32_t)raw_speed : -((int32_t)raw_speed);
    signed_speed_rpm = signed_speed_raw / (int32_t)EMM42_SPEED_SCALE_X10;

    if (frame[0] == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    } else {
        axis = STEPMOTOR_BUS_CTRL_AXIS_X;
    }

    s_stepmotor_last_speed_raw[(uint32_t)axis] = signed_speed_raw;
    s_stepmotor_last_speed_rpm[(uint32_t)axis] = signed_speed_rpm;
    stepmotor_bus_drop_prefix(STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN);
    return true;
}

static bool stepmotor_bus_try_handle_return(void)
{
    uint8_t *frame = s_stepmotor_rx_state.data;

    while (s_stepmotor_rx_state.count > 0u) {
        if (stepmotor_bus_is_stepmotor_addr(frame[0]) == false) {
            stepmotor_bus_drop_prefix(1u);
            continue;
        }

        if (s_stepmotor_rx_state.count < 2u) {
            return false;
        }
        if (stepmotor_bus_is_stepmotor_code(frame[1]) == false) {
            stepmotor_bus_drop_prefix(1u);
            continue;
        }

        if (frame[1] == STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) {
            if (s_stepmotor_rx_state.count < STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN) {
                return false;
            }
            return stepmotor_bus_try_handle_speed_reply();
        }

        if (s_stepmotor_rx_state.count < 3u) {
            return false;
        }
        if (stepmotor_bus_is_stepmotor_data(frame[2]) == false) {
            stepmotor_bus_drop_prefix(1u);
            continue;
        }

        if (s_stepmotor_rx_state.count < STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN) {
            return false;
        }
        if (frame[3] != STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK) {
            stepmotor_bus_drop_prefix(1u);
            continue;
        }

        s_stepmotor_last_return_code = frame[2];
        if (stepmotor_bus_is_error_return_code(frame[2]) == true) {
            stepmotor_bus_control_error_inc();
        }
        stepmotor_bus_drop_prefix(STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN);
        return true;
    }

    return false;
}

static void stepmotor_bus_process_rx_byte(uint8_t data)
{
    if (s_stepmotor_rx_state.count >= STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN) {
        stepmotor_bus_drop_prefix(1u);
    }

    s_stepmotor_rx_state.data[s_stepmotor_rx_state.count++] = data;
    s_stepmotor_rx_state.last_rx_tick = Clock_NowMs();

    while (stepmotor_bus_try_handle_return() == true) {
    }
}

static void stepmotor_bus_discard_stale_partial(void)
{
    if ((s_stepmotor_rx_state.count > 0u) &&
        ((Clock_NowMs() - s_stepmotor_rx_state.last_rx_tick) >=
         STEPMOTOR_BUS_INTERBYTE_TIMEOUT_MS)) {
        stepmotor_bus_rx_reset();
    }
}

static bool stepmotor_bus_control_allowed(void)
{
    return s_stepmotor_control_gate_enabled;
}

static bool stepmotor_bus_axis_from_stepmotor_id(uint8_t axis_id,
                                                 StepmotorBus_CtrlAxis_e *axis)
{
    if (axis == NULL) {
        return false;
    }

    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        *axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
        return true;
    }
    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        *axis = STEPMOTOR_BUS_CTRL_AXIS_X;
        return true;
    }

    return false;
}

static bool stepmotor_bus_mgmt_peek_locked(StepmotorBus_TxFrame_t *frame)
{
    if ((frame == NULL) || (s_stepmotor_mgmt_queue.count == 0u)) {
        return false;
    }

    memcpy(frame->data,
           s_stepmotor_mgmt_queue.frames[s_stepmotor_mgmt_queue.tail].data,
           s_stepmotor_mgmt_queue.frames[s_stepmotor_mgmt_queue.tail].length);
    frame->length = s_stepmotor_mgmt_queue.frames[s_stepmotor_mgmt_queue.tail].length;
    return true;
}

static bool stepmotor_bus_ctrl_has_pending_locked(void)
{
    uint8_t index = 0u;

    if (stepmotor_bus_control_allowed() == false) {
        return false;
    }

    for (index = 0u; index < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; index++) {
        StepmotorBus_CtrlAxis_e axis =
            (StepmotorBus_CtrlAxis_e)(((uint8_t)s_stepmotor_next_ctrl_axis + index) %
                                      (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX);
        if ((s_stepmotor_ctrl_slots[axis].dirty == true) &&
            (s_stepmotor_ctrl_slots[axis].frame.length > 0u)) {
            return true;
        }
    }

    return false;
}

static bool stepmotor_bus_pick_control_frame_locked(StepmotorBus_TxFrame_t *frame,
                                                    StepmotorBus_TxSource_e *source,
                                                    StepmotorBus_CtrlAxis_e *axis)
{
    uint8_t index = 0u;

    if ((frame == NULL) || (source == NULL) || (axis == NULL)) {
        return false;
    }

    if (stepmotor_bus_control_allowed() == false) {
        return false;
    }

    for (index = 0u; index < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; index++) {
        StepmotorBus_CtrlAxis_e candidate =
            (StepmotorBus_CtrlAxis_e)(((uint8_t)s_stepmotor_next_ctrl_axis + index) %
                                      (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX);
        StepmotorBus_CtrlSlot_t *slot = &s_stepmotor_ctrl_slots[candidate];

        if ((slot->dirty == true) && (slot->frame.length > 0u)) {
            memcpy(frame->data, slot->frame.data, slot->frame.length);
            frame->length = slot->frame.length;
            slot->dirty = false;
            *source = STEPMOTOR_BUS_TX_SRC_CONTROL;
            *axis = candidate;
            s_stepmotor_next_ctrl_axis =
                (StepmotorBus_CtrlAxis_e)(((uint8_t)candidate + 1u) %
                                          (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX);
            return true;
        }
    }

    return false;
}

static bool stepmotor_bus_is_read_speed_frame(const uint8_t *frame,
                                              uint8_t len,
                                              uint8_t *axis_addr)
{
    if ((frame == NULL) || (len < 3u)) {
        return false;
    }

    if ((stepmotor_bus_is_stepmotor_addr(frame[0]) == false) ||
        (frame[1] != STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) ||
        (frame[len - 1u] != STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
        return false;
    }

    if (axis_addr != NULL) {
        *axis_addr = frame[0];
    }

    return true;
}

static bool stepmotor_bus_has_pending_read_speed_locked(uint8_t axis_addr)
{
    uint8_t index = 0u;

    for (index = 0u; index < s_stepmotor_mgmt_queue.count; index++) {
        uint8_t slot_index =
            (uint8_t)((s_stepmotor_mgmt_queue.tail + index) % STEPMOTOR_BUS_MGMT_QUEUE_DEPTH);
        StepmotorBus_TxFrame_t *slot = &s_stepmotor_mgmt_queue.frames[slot_index];

        if ((slot->length >= 3u) &&
            (slot->data[0] == axis_addr) &&
            (slot->data[1] == STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) &&
            (slot->data[slot->length - 1u] == STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
            return true;
        }
    }

    return false;
}

static void stepmotor_bus_mgmt_drop_locked(void)
{
    if (s_stepmotor_mgmt_queue.count == 0u) {
        return;
    }

    s_stepmotor_mgmt_queue.tail =
        (uint8_t)((s_stepmotor_mgmt_queue.tail + 1u) % STEPMOTOR_BUS_MGMT_QUEUE_DEPTH);
    s_stepmotor_mgmt_queue.count--;
}

static bool stepmotor_bus_pick_next_frame_locked(StepmotorBus_TxFrame_t *frame,
                                                 StepmotorBus_TxSource_e *source,
                                                 StepmotorBus_CtrlAxis_e *axis)
{
    bool has_ctrl = false;
    bool has_mgmt = false;

    if ((frame == NULL) || (source == NULL) || (axis == NULL)) {
        return false;
    }

    has_ctrl = stepmotor_bus_ctrl_has_pending_locked();
    has_mgmt = (bool)(s_stepmotor_mgmt_queue.count > 0u);

    if (has_ctrl == true) {
        if ((s_stepmotor_prefer_mgmt_next == false) || (has_mgmt == false)) {
            if (stepmotor_bus_pick_control_frame_locked(frame, source, axis) == true) {
                s_stepmotor_prefer_mgmt_next = (bool)(has_mgmt == true);
                return true;
            }
        }
    }

    if (has_mgmt == true) {
        if (stepmotor_bus_mgmt_peek_locked(frame) == true) {
            *source = STEPMOTOR_BUS_TX_SRC_MGMT;
            *axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
            s_stepmotor_prefer_mgmt_next = false;
            return true;
        }
    }

    if (has_ctrl == true) {
        if (stepmotor_bus_pick_control_frame_locked(frame, source, axis) == true) {
            s_stepmotor_prefer_mgmt_next = true;
            return true;
        }
    }

    return false;
}

static void stepmotor_bus_try_start_tx(void)
{
    StepmotorBus_TxFrame_t frame;
    StepmotorBus_TxSource_e source = STEPMOTOR_BUS_TX_SRC_NONE;
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;

    if ((s_stepmotor_tx_dispatch_busy == true) ||
        (StepmotorUart_IsTxIdle() == false)) {
        return;
    }

    if (stepmotor_bus_pick_next_frame_locked(&frame, &source, &axis) == false) {
        return;
    }

    s_stepmotor_tx_dispatch_busy = true;
    if (StepmotorUart_TryWrite(frame.data, (uint32_t)frame.length) == false) {
        if (source == STEPMOTOR_BUS_TX_SRC_CONTROL) {
            s_stepmotor_ctrl_slots[axis].dirty = true;
        }
        s_stepmotor_tx_dispatch_busy = false;
        stepmotor_bus_control_error_inc();
        return;
    }

    if (source == STEPMOTOR_BUS_TX_SRC_MGMT) {
        stepmotor_bus_mgmt_drop_locked();
    }
}

static void stepmotor_bus_handle_tx_done(void)
{
    if (StepmotorUart_ConsumeTxDone() == true) {
        s_stepmotor_tx_dispatch_busy = false;
    }
}

static StepmotorBus_Status_e stepmotor_bus_enqueue_mgmt_frame(const uint8_t *frame,
                                                              uint8_t len)
{
    StepmotorBus_TxFrame_t *slot = NULL;
    uint8_t read_speed_axis = 0u;

    if ((frame == NULL) || (len == 0u) || (len > STEPMOTOR_BUS_TX_FRAME_MAX_LEN)) {
        return STEPMOTOR_BUS_ERR_INVALID;
    }

    if ((stepmotor_bus_is_read_speed_frame(frame, len, &read_speed_axis) == true) &&
        (stepmotor_bus_has_pending_read_speed_locked(read_speed_axis) == true)) {
        return STEPMOTOR_BUS_OK;
    }

    if (s_stepmotor_mgmt_queue.count >= STEPMOTOR_BUS_MGMT_QUEUE_DEPTH) {
        return STEPMOTOR_BUS_ERR_BUSY;
    }

    slot = &s_stepmotor_mgmt_queue.frames[s_stepmotor_mgmt_queue.head];
    memcpy(slot->data, frame, len);
    slot->length = len;
    s_stepmotor_mgmt_queue.head =
        (uint8_t)((s_stepmotor_mgmt_queue.head + 1u) % STEPMOTOR_BUS_MGMT_QUEUE_DEPTH);
    s_stepmotor_mgmt_queue.count++;
    if (stepmotor_bus_is_read_speed_frame(frame, len, NULL) == true) {
        s_stepmotor_prefer_mgmt_next = true;
    }

    stepmotor_bus_try_start_tx();
    return STEPMOTOR_BUS_OK;
}

static StepmotorBus_Status_e stepmotor_bus_submit_control_frame(
    StepmotorBus_CtrlAxis_e axis,
    const uint8_t *frame,
    uint8_t len)
{
    if (((uint32_t)axis >= (uint32_t)STEPMOTOR_BUS_CTRL_AXIS_MAX) ||
        (frame == NULL) || (len == 0u) || (len > STEPMOTOR_BUS_TX_FRAME_MAX_LEN)) {
        stepmotor_bus_control_error_inc();
        return STEPMOTOR_BUS_ERR_INVALID;
    }

    if (stepmotor_bus_control_allowed() == false) {
        stepmotor_bus_control_error_inc();
        return STEPMOTOR_BUS_ERR_NOT_READY;
    }

    memcpy(s_stepmotor_ctrl_slots[axis].frame.data, frame, len);
    s_stepmotor_ctrl_slots[axis].frame.length = len;
    s_stepmotor_ctrl_slots[axis].dirty = true;

    stepmotor_bus_try_start_tx();
    return STEPMOTOR_BUS_OK;
}

static void stepmotor_bus_service_rx(void)
{
    uint8_t read_buf[STEPMOTOR_BUS_READ_CHUNK_SIZE];
    uint32_t read_count = 0u;
    uint32_t index = 0u;

    stepmotor_bus_discard_stale_partial();

    do {
        read_count = StepmotorUart_Read(read_buf, sizeof(read_buf));
        for (index = 0u; index < read_count; index++) {
            stepmotor_bus_process_rx_byte(read_buf[index]);
        }
    } while (read_count > 0u);

    stepmotor_bus_discard_stale_partial();
}

void StepmotorBus_Init(void)
{
    memset(&s_stepmotor_rx_state, 0, sizeof(s_stepmotor_rx_state));
    memset(&s_stepmotor_mgmt_queue, 0, sizeof(s_stepmotor_mgmt_queue));
    memset(&s_stepmotor_ctrl_slots, 0, sizeof(s_stepmotor_ctrl_slots));
    s_stepmotor_tx_dispatch_busy = false;
    s_stepmotor_next_ctrl_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    s_stepmotor_prefer_mgmt_next = false;
    s_stepmotor_control_gate_enabled = false;
    s_stepmotor_control_error_count = 0u;
    s_stepmotor_last_return_code = 0u;
    s_stepmotor_last_speed_raw[STEPMOTOR_BUS_CTRL_AXIS_Y] = 0;
    s_stepmotor_last_speed_raw[STEPMOTOR_BUS_CTRL_AXIS_X] = 0;
    s_stepmotor_last_speed_rpm[STEPMOTOR_BUS_CTRL_AXIS_Y] = 0;
    s_stepmotor_last_speed_rpm[STEPMOTOR_BUS_CTRL_AXIS_X] = 0;
    s_stepmotor_bypass = false;
    StepmotorUart_Init();
}

void StepmotorBus_Service5ms(void)
{
    stepmotor_bus_handle_tx_done();

    if (s_stepmotor_bypass == true) {
        return;
    }

    stepmotor_bus_service_rx();
    stepmotor_bus_try_start_tx();
}

bool StepmotorBus_RequestBypass(void)
{
    uint32_t waited_ms = 0u;

    while (waited_ms <= STEPMOTOR_BUS_EXCLUSIVE_WAIT_MS) {
        stepmotor_bus_handle_tx_done();
        if ((s_stepmotor_tx_dispatch_busy == false) &&
            (StepmotorUart_IsTxIdle() == true)) {
            StepmotorBus_SetBypass(true);
            return true;
        }

        if (waited_ms == STEPMOTOR_BUS_EXCLUSIVE_WAIT_MS) {
            break;
        }

        Mspm0Runtime_DelayMs(1u);
        waited_ms++;
    }

    return false;
}

void StepmotorBus_SetBypass(bool bypass)
{
    s_stepmotor_bypass = bypass;
    if (bypass == true) {
        stepmotor_bus_rx_reset();
    }
}

void StepmotorBus_SetControlGate(bool enable)
{
    s_stepmotor_control_gate_enabled = enable;
}

void StepmotorBus_ResetDiagCounters(void)
{
    s_stepmotor_control_error_count = 0u;
    s_stepmotor_last_return_code = 0u;
}

uint32_t StepmotorBus_GetControlErrorCount(void)
{
    return s_stepmotor_control_error_count;
}

uint8_t StepmotorBus_GetLastReturnCode(void)
{
    return s_stepmotor_last_return_code;
}

void StepmotorBus_ClearControlFrames(void)
{
    uint8_t axis = 0u;

    for (axis = 0u; axis < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; axis++) {
        s_stepmotor_ctrl_slots[axis].dirty = false;
        s_stepmotor_ctrl_slots[axis].frame.length = 0u;
    }
}

bool StepmotorBus_IsControlPathIdle(void)
{
    uint8_t axis = 0u;

    stepmotor_bus_handle_tx_done();

    if ((s_stepmotor_tx_dispatch_busy == true) ||
        (StepmotorUart_IsTxIdle() == false)) {
        return false;
    }

    for (axis = 0u; axis < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; axis++) {
        if (s_stepmotor_ctrl_slots[axis].dirty == true) {
            return false;
        }
    }

    return true;
}

int32_t StepmotorBus_GetLastSpeedRpm(uint8_t axis_id)
{
    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        return s_stepmotor_last_speed_rpm[STEPMOTOR_BUS_CTRL_AXIS_Y];
    }
    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        return s_stepmotor_last_speed_rpm[STEPMOTOR_BUS_CTRL_AXIS_X];
    }
    return 0;
}

int32_t StepmotorBus_GetLastSpeedRaw(uint8_t axis_id)
{
    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        return s_stepmotor_last_speed_raw[STEPMOTOR_BUS_CTRL_AXIS_Y];
    }
    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        return s_stepmotor_last_speed_raw[STEPMOTOR_BUS_CTRL_AXIS_X];
    }
    return 0;
}

void Emm42_SendEnableCommand(uint8_t axis_id, uint8_t enable_status)
{
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if (Emm42_BuildEnableFrame(axis_id, enable_status, frame, &frame_len) == true) {
        (void)stepmotor_bus_enqueue_mgmt_frame(frame, frame_len);
    }
}

void Emm42_SendReadSpeedCommand(uint8_t axis_id)
{
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if (Emm42_BuildReadSpeedFrame(axis_id, frame, &frame_len) == true) {
        (void)stepmotor_bus_enqueue_mgmt_frame(frame, frame_len);
    }
}

void Emm42_SendSpeedCommand(uint8_t axis_id,
                            uint8_t direction,
                            uint16_t speed,
                            uint8_t acceleration)
{
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if ((stepmotor_bus_axis_from_stepmotor_id(axis_id, &axis) == false) ||
        (Emm42_BuildSpeedFrame(axis_id,
                               direction,
                               speed,
                               acceleration,
                               frame,
                               &frame_len) == false)) {
        stepmotor_bus_control_error_inc();
        return;
    }

    (void)stepmotor_bus_submit_control_frame(axis, frame, frame_len);
}

void Emm42_SendPositionCommand(uint8_t axis_id,
                               uint8_t direction,
                               uint16_t speed,
                               uint8_t acceleration,
                               uint32_t pulses,
                               uint8_t mode)
{
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if ((stepmotor_bus_axis_from_stepmotor_id(axis_id, &axis) == false) ||
        (Emm42_BuildPositionFrame(axis_id,
                                  direction,
                                  speed,
                                  acceleration,
                                  pulses,
                                  mode,
                                  frame,
                                  &frame_len) == false)) {
        stepmotor_bus_control_error_inc();
        return;
    }

    (void)stepmotor_bus_submit_control_frame(axis, frame, frame_len);
}

void Emm42_EnableAll(void)
{
    Emm42_SendEnableCommand(STEPMOTOR_BUS_STEPMOTOR_ADDR_Y, EMM42_ENABLE_ON);
    Emm42_SendEnableCommand(STEPMOTOR_BUS_STEPMOTOR_ADDR_X, EMM42_ENABLE_ON);
}

void Emm42_DisableAll(void)
{
    Emm42_SendEnableCommand(STEPMOTOR_BUS_STEPMOTOR_ADDR_Y, EMM42_ENABLE_OFF);
    Emm42_SendEnableCommand(STEPMOTOR_BUS_STEPMOTOR_ADDR_X, EMM42_ENABLE_OFF);
}

void Emm42_SetAllAxesZero(void)
{
    Emm42_SetZeroPosition(STEPMOTOR_BUS_STEPMOTOR_ADDR_Y);
    Emm42_SetZeroPosition(STEPMOTOR_BUS_STEPMOTOR_ADDR_X);
}

void Emm42_MoveRelative(Emm42_Axis_e axis, int32_t pulses, uint16_t speed, uint8_t acceleration)
{
    uint32_t pulse_count = 0u;
    uint8_t direction = EMM42_DIR_CW;

    if ((axis != EMM42_AXIS_Y) && (axis != EMM42_AXIS_X)) {
        return;
    }
    if (pulses == 0) {
        return;
    }

    if (pulses < 0) {
        direction = EMM42_DIR_CCW;
        pulse_count = (uint32_t)(-pulses);
    } else {
        pulse_count = (uint32_t)pulses;
    }

    Emm42_SendPositionCommand((uint8_t)axis,
                              direction,
                              speed,
                              acceleration,
                              pulse_count,
                              EMM42_POSITION_MODE_RELATIVE);
}

void Emm42_MoveAbsolute(Emm42_Axis_e axis, uint32_t position_pulses, uint16_t speed)
{
    if ((axis != EMM42_AXIS_Y) && (axis != EMM42_AXIS_X)) {
        return;
    }

    Emm42_SendPositionCommand((uint8_t)axis,
                              EMM42_POSITION_DIR_ABSOLUTE,
                              speed,
                              EMM42_POSITION_ACCEL_FIXED,
                              position_pulses,
                              EMM42_POSITION_MODE_ABSOLUTE);
}

void Emm42_SetZeroPosition(uint8_t axis_id)
{
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if (Emm42_BuildSetZeroFrame(axis_id, frame, &frame_len) == true) {
        (void)stepmotor_bus_enqueue_mgmt_frame(frame, frame_len);
    }
}

void Emm42_StartHoming(uint8_t axis_id)
{
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if (Emm42_BuildStartHomingFrame(axis_id, frame, &frame_len) == true) {
        (void)stepmotor_bus_enqueue_mgmt_frame(frame, frame_len);
    }
}

void Emm42_ExitHoming(uint8_t axis_id)
{
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if (Emm42_BuildExitHomingFrame(axis_id, frame, &frame_len) == true) {
        (void)stepmotor_bus_enqueue_mgmt_frame(frame, frame_len);
    }
}

void Emm42_SendPidConfigCommand(uint8_t axis_id,
                                uint8_t save_to_flash,
                                uint32_t kp,
                                uint32_t ki,
                                uint32_t kd)
{
    uint8_t frame[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t frame_len = 0u;

    if (Emm42_BuildPidConfigFrame(axis_id,
                                  save_to_flash,
                                  kp,
                                  ki,
                                  kd,
                                  frame,
                                  &frame_len) == true) {
        (void)stepmotor_bus_enqueue_mgmt_frame(frame, frame_len);
    }
}
