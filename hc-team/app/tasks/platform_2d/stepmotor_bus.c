/**
 * @file    stepmotor_bus.c
 * @brief   步进电机专用串口总线服务模块实现
 *
 * 本文件实现 UART_STEPPER_BUS (UART2) 上的步进电机收发调度逻辑。
 * 原 HorizonBus 模块视觉协议拆分到 vision_bus.{c,h}，本文件只保留电机侧。
 *
 * 功能范围：
 * - 维护电机总线的接收 FIFO 与发送队列
 * - 接收步进电机返回帧并完成基础过滤
 * - 调度控制帧与管理帧发送
 *
 * 不负责的内容：
 * - 视觉坐标帧解析（已移至 vision_bus）
 * - 云台控制算法
 * - 菜单渲染与系统调度策略
 *
 * 实现说明：
 * 1. UART ISR 仅负责收字节入 FIFO，避免在中断内做重处理
 * 2. 应答过滤与发送调度在 5ms 服务函数中推进
 * 3. 控制帧与管理帧分开管理，并统一走串口发送仲裁
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app/tasks/platform_2d/stepmotor_bus.h"

#include "app/scheduler/task_scheduler.h"
#include "driver/clock/clock.h"
#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "driver/step_motor/emm42.h"
#include "ti_msp_dl_config.h"
#include <string.h>

 /* ---- 静态配置 ----------------------------------------------------------- */

//缓冲区 队列设置
#define STEPMOTOR_BUS_RX_FIFO_SIZE              STEPMOTOR_BUS_SHARED_RX_FIFO_SIZE//接收 FIFO 大小，单位字节；根据实际协议帧长度与预期丢帧率调整，确保能容纳至少几个完整帧的字节数据
#define STEPMOTOR_BUS_MGMT_QUEUE_DEPTH          8u//管理帧发送队列深度，单位帧；根据实际控制需求与预期丢帧率调整，确保能容纳至少几个待发送的控制帧
#define STEPMOTOR_BUS_TX_FRAME_MAX_LEN          32u//发送帧最大长度，单位字节；根据实际协议定义调整，确保能容纳最长的控制帧或管理帧
#define STEPMOTOR_BUS_RX_PROCESS_BUDGET         256u//每次服务函数处理的最大接收字节数，单位字节；根据实际协议帧长度与处理效率调整，确保能在合理时间内处理完至少一个完整帧的数据
#define STEPMOTOR_BUS_INTERBYTE_TIMEOUT_MS      3u//接收字节间超时时间，单位毫秒；根据实际协议帧发送间隔与预期丢帧率调整，确保能正确分帧但不过于敏感导致误分帧
//步进电机协议基础
#define STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN       4u//步进电机协议帧长度，单位字节；根据协议定义调整，确保能正确识别完整的电机返回帧
#define STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN 6u//读速度 0x35 回复帧长度：[Addr][0x35][Sign][SpdH][SpdL][0x6B]
#define STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK       0x6Bu//步进电机返回帧的 ACK 校验值，单位字节；根据协议定义调整，确保能正确识别电机返回帧的有效性
//电机地址分配
#define STEPMOTOR_BUS_STEPMOTOR_ADDR_Y          1u//步进电机 Y 轴地址，单位字节；根据实际电机地址配置调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_ADDR_X          2u//步进电机 X 轴地址，单位字节；根据实际电机地址配置调整，确保与电机侧设置一致
//电机命令字（主机→电机）
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ENABLE      0xF3u//步进电机使能命令字，单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_GRIP        0xF5u//步进电机夹爪控制命令字，单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_SPEED       0xF6u//步进电机速度控制命令字，单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_POS_ACK     0xFBu//步进电机位置控制命令字（带 ACK），单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_POSITION    0xFDu//步进电机位置控制命令字（不带 ACK），单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_PID_CFG     0x4Au//步进电机 PID 参数修改确认命令字，单位字节
#define STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED  0x35u//步进电机“读取实时速度”命令字；返回帧为 6 字节 0x35 响应
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_SET  0x93u///步进电机回零设置命令字，单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_RUN  0x9Au//步进电机回零运行命令字，单位字节；根据协议定义调整，确保与电机侧设置一致
#define STEPMOTOR_BUS_STEPMOTOR_CMD_ORIGIN_QUIT 0x9Cu//步进电机回零退出命令字，单位字节；根据协议定义调整，确保与电机侧设置一致
//电机返回码（电机→主机）
#define STEPMOTOR_BUS_STEPMOTOR_RET_OK          0x02u//步进电机返回正常响应码，单位字节；根据协议定义调整，确保能正确识别电机的正常响应
#define STEPMOTOR_BUS_STEPMOTOR_RET_HOME        0x12u//步进电机返回回零完成码，单位字节；根据协议定义调整，确保能正确识别电机回零完成的状态
#define STEPMOTOR_BUS_STEPMOTOR_RET_PARAM_ERR   0xE2u//步进电机返回参数错误码，单位字节；根据协议定义调整，确保能正确识别电机响应中的参数错误
#define STEPMOTOR_BUS_STEPMOTOR_RET_FRAME_ERR   0xEEu//步进电机返回帧错误码，单位字节；根据协议定义调整，确保能正确识别电机响应中的帧错误
#define STEPMOTOR_BUS_STEPMOTOR_RET_DONE        0x9Fu//步进电机返回动作完成码，单位字节；根据协议定义调整，确保能正确识别电机动作完成的状态

/* ---- 内部类型定义 ------------------------------------------------------- */

typedef struct {
    uint8_t data[STEPMOTOR_BUS_RX_FIFO_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    volatile uint32_t last_rx_tick;
} StepmotorBus_RxFifo_t;//接收 FIFO 结构体，包含数据缓冲区、头尾索引、计数器和最后接收时间戳，用于管理串口接收的数据流

typedef struct {
    uint8_t data[STEPMOTOR_BUS_TX_FRAME_MAX_LEN];
    uint8_t length;
} StepmotorBus_TxFrame_t;//发送帧结构体，包含数据缓冲区和帧长度，用于封装待发送的控制帧或管理帧

typedef struct {
    StepmotorBus_TxFrame_t frames[STEPMOTOR_BUS_MGMT_QUEUE_DEPTH];
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint8_t count;
} StepmotorBus_MgmtQueue_t;//管理帧队列结构体，包含发送帧数组和队列状态信息，用于管理待发送的管理帧

typedef struct {
    StepmotorBus_TxFrame_t frame;
    volatile bool dirty;
} StepmotorBus_CtrlSlot_t;//控制槽结构体，包含发送帧和脏标志，用于管理待发送的控制帧

typedef enum {
    STEPMOTOR_BUS_CTRL_AXIS_Y = 0,
    STEPMOTOR_BUS_CTRL_AXIS_X,
    STEPMOTOR_BUS_CTRL_AXIS_MAX
} StepmotorBus_CtrlAxis_e;//控制轴枚举，定义了 Y 轴和 X 轴两种控制类型，用于区分不同轴的控制帧

typedef enum {
    STEPMOTOR_BUS_TX_SRC_NONE = 0,
    STEPMOTOR_BUS_TX_SRC_CONTROL,
    STEPMOTOR_BUS_TX_SRC_MGMT
} StepmotorBus_TxSource_e;
//发送来源枚举，定义了无发送、控制帧发送和管理帧发送三种状态，用于调度发送优先级

/* ---- 模块状态 ----------------------------------------------------------- */

static StepmotorBus_RxFifo_t s_stepmotor_rx_fifo;
static StepmotorBus_MgmtQueue_t s_stepmotor_mgmt_queue;
static StepmotorBus_CtrlSlot_t s_stepmotor_ctrl_slots[STEPMOTOR_BUS_CTRL_AXIS_MAX];
static volatile bool s_stepmotor_tx_dispatch_busy = false;
static StepmotorBus_CtrlAxis_e s_stepmotor_next_ctrl_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
static volatile bool s_stepmotor_prefer_mgmt_next = false;
static volatile bool s_stepmotor_control_gate_enabled = false;
static volatile uint32_t s_stepmotor_control_error_count = 0u;
static volatile uint8_t s_stepmotor_last_return_code = 0u;
/* 最近一次解析成功的 0x35 反馈速度：
 * - raw：高低字节直拼后的带符号原始值
 * - rpm：按当前上层显示口径缩放后的 RPM
 */
static volatile int32_t s_stepmotor_last_speed_raw[STEPMOTOR_BUS_CTRL_AXIS_MAX] = {0};
static volatile int32_t s_stepmotor_last_speed_rpm[STEPMOTOR_BUS_CTRL_AXIS_MAX] = {0};

static uint32_t stepmotor_bus_irq_lock(void);
static void stepmotor_bus_irq_unlock(uint32_t primask);

static void stepmotor_bus_control_error_inc(void)
{
    uint32_t primask = stepmotor_bus_irq_lock();

    if (s_stepmotor_control_error_count < UINT32_MAX) {
        s_stepmotor_control_error_count++;
    }

    stepmotor_bus_irq_unlock(primask);
}

/* ---- 静态辅助函数 ------------------------------------------------------- */

static uint32_t stepmotor_bus_irq_lock(void)
{
    uint32_t primask = __get_PRIMASK();//

    __disable_irq();
    return primask;
}//锁定中断并返回当前中断状态，调用者应保存返回值以便后续解锁时恢复原状态

static void stepmotor_bus_irq_unlock(uint32_t primask)
{
    if (primask == 0u) {
        __enable_irq();
    }
}//根据传入的中断状态参数决定是否解锁中断，确保只在原本未锁定的情况下才恢复中断响应

static void stepmotor_bus_rx_reset(StepmotorBus_RxFifo_t* fifo)
{
    if (fifo == NULL) {
        return;
    }

    fifo->head = 0u;
    fifo->tail = 0u;
    fifo->count = 0u;
}//重置接收 FIFO 的状态，清空数据并将头尾索引和计数器归零，使其回到初始状态

static uint16_t stepmotor_bus_rx_count(const StepmotorBus_RxFifo_t* fifo)
{
    if (fifo == NULL) {
        return 0u;
    }

    return fifo->count;
}//获取接收 FIFO 中当前存储的字节数，返回计数器的值，调用者可根据此值判断是否有足够数据进行帧解析

//以下函数实现了对接收 FIFO 的基本操作，包括推入新数据、预览数据、复制数据和丢弃数据，确保对 FIFO 的访问安全且高效
static bool stepmotor_bus_rx_push(StepmotorBus_RxFifo_t* fifo, uint8_t data)
{
    if (fifo == NULL) {
        return false;
    }

    if (fifo->count >= STEPMOTOR_BUS_RX_FIFO_SIZE) {
        fifo->tail = (uint16_t)((fifo->tail + 1u) % STEPMOTOR_BUS_RX_FIFO_SIZE);
        fifo->count--;
    }

    fifo->data[fifo->head] = data;
    fifo->head = (uint16_t)((fifo->head + 1u) % STEPMOTOR_BUS_RX_FIFO_SIZE);
    fifo->count++;

    fifo->last_rx_tick = Clock_NowMs();

    return true;
}//将新接收的字节数据推入 FIFO 中，如果 FIFO 已满则丢弃最旧的数据以腾出空间，同时更新最后接收时间戳，确保 FIFO 始终包含最新的接收数据

//以下函数实现了对接收 FIFO 中数据的预览、复制和丢弃操作，支持在不修改 FIFO 状态的前提下查看数据内容，以及在处理完成后正确更新 FIFO 的状态
static bool stepmotor_bus_rx_peek(const StepmotorBus_RxFifo_t* fifo,
    uint16_t offset,
    uint8_t* p_data)
{
    uint16_t index = 0u;

    if ((fifo == NULL) || (p_data == NULL) ||
        (offset >= fifo->count)) {
        return false;
    }

    index = (uint16_t)((fifo->tail + offset) % STEPMOTOR_BUS_RX_FIFO_SIZE);
    *p_data = fifo->data[index];
    return true;
}

//以下函数实现了对接收 FIFO 中数据的批量复制和丢弃操作，支持在处理完整帧数据后一次性更新 FIFO 的状态，确保处理效率和数据一致性
static bool stepmotor_bus_rx_copy(const StepmotorBus_RxFifo_t* fifo,
    uint16_t length,
    uint8_t* p_buf)
{
    uint16_t index = 0u;

    if ((fifo == NULL) || (p_buf == NULL) ||
        (length > fifo->count)) {
        return false;
    }

    for (index = 0u; index < length; index++) {
        if (stepmotor_bus_rx_peek(fifo, index, &p_buf[index]) == false) {
            return false;
        }
    }

    return true;
}


//以下函数实现了对接收 FIFO 中数据的丢弃操作，支持在处理完成后正确更新 FIFO 的状态，确保处理效率和数据一致性
static void stepmotor_bus_rx_drop(StepmotorBus_RxFifo_t* fifo, uint16_t length)
{
    if (fifo == NULL) {
        return;
    }

    if (length >= fifo->count) {
        stepmotor_bus_rx_reset(fifo);
        return;
    }

    fifo->tail = (uint16_t)((fifo->tail + length) % STEPMOTOR_BUS_RX_FIFO_SIZE);
    fifo->count = (uint16_t)(fifo->count - length);
}

//地址和命令字判断函数，(x or y)
static bool stepmotor_bus_is_stepmotor_addr(uint8_t addr)
{
    return (bool)((addr == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) ||
        (addr == STEPMOTOR_BUS_STEPMOTOR_ADDR_X));
}

//模式和命令字判断函数，(速度模式, 位置模式, 回零模式, 读速度回复)
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

//电机返回码判断函数，(正常响应, 回零完成, 参数错误, 帧错误, 动作完成)
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

//以下函数实现了对接收 FIFO 中数据的帧头识别和分流判断，支持在处理完整帧数据前快速判断数据类型，确保处理效率和正确分流
static bool stepmotor_bus_has_stepmotor_prefix(const StepmotorBus_RxFifo_t* fifo)
{
    uint8_t data = 0u;
    uint8_t code = 0u;
    uint16_t count = stepmotor_bus_rx_count(fifo);

    //最大可能帧长度=读速度回复 6 字节；超过即应交由 try_handle 消费
    if ((count == 0u) || (count >= STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN)) {
        return false;
    }

    if ((stepmotor_bus_rx_peek(fifo, 0u, &data) == false) ||
        (stepmotor_bus_is_stepmotor_addr(data) == false)) {
        return false;
    }

    if (count >= 2u) {
        if ((stepmotor_bus_rx_peek(fifo, 1u, &code) == false) ||
            (stepmotor_bus_is_stepmotor_code(code) == false)) {
            return false;
        }
    }

    //offset-2 数据域判定仅对 4 字节 ACK 帧有效；0x35 回复此处是 Sign(0/1)
    if ((count >= 3u) &&
        (code != STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED)) {
        if ((stepmotor_bus_rx_peek(fifo, 2u, &data) == false) ||
            (stepmotor_bus_is_stepmotor_data(data) == false)) {
            return false;
        }
    }

    //对 0x35 回复：count<6 时继续等字节，count>=6 已在头部被排除
    //对 4 字节 ACK：count<4 表示尚未收齐，保持等待
    if ((code != STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) &&
        (count >= STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN)) {
        return false;
    }

    return true;
}

//以下函数实现了对共享总线控制权限的判断，当前实现为始终不允许控制帧发送，后续可根据实际需求调整逻辑以支持不同的控制策略
static bool stepmotor_bus_control_allowed(void)
{
    return s_stepmotor_control_gate_enabled;
}

//以下函数实现了对接收 FIFO 中数据的过期判断和丢弃操作，支持在接收过程中如果发现数据停滞超过预设时间则丢弃部分数据以避免处理过时信息，确保系统响应的实时性和有效性
static void stepmotor_bus_discard_stale_partial(void)
{
    uint32_t tick_ms = 0u;

    if (s_stepmotor_rx_fifo.count == 0u) {
        return;
    }

    tick_ms = Clock_NowMs();

    if ((tick_ms - s_stepmotor_rx_fifo.last_rx_tick) <
        STEPMOTOR_BUS_INTERBYTE_TIMEOUT_MS) {
        return;
    }

    if ((s_stepmotor_rx_fifo.count < STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN) ||
        (stepmotor_bus_has_stepmotor_prefix(&s_stepmotor_rx_fifo) == true)) {
        stepmotor_bus_rx_reset(&s_stepmotor_rx_fifo);
    }
}

//尝试消费一帧“读速度 0x35”回复：6 字节 [Addr][0x35][Sign][SpdH][SpdL][0x6B]
//这里同时缓存原始字段和上层显示口径 RPM，避免调试时混淆“原始值”和“缩放值”。
//按当前实机验证，Sign=0x01 与上层 DIR=1 同号，因此映射为正 RPM。
static bool stepmotor_bus_try_handle_speed_reply(StepmotorBus_RxFifo_t* fifo)
{
    uint8_t frame[STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN];
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    uint16_t raw_speed = 0u;
    int32_t signed_speed_raw = 0;
    int32_t signed_speed_rpm = 0;

    if ((fifo == NULL) ||
        (stepmotor_bus_rx_count(fifo) < STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN)) {
        return false;
    }

    if (stepmotor_bus_rx_copy(fifo, STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN,
            frame) == false) {
        return false;
    }

    //结构校验：addr 合法、code=0x35、sign∈{0,1}、尾字节=0x6B
    if ((stepmotor_bus_is_stepmotor_addr(frame[0]) == false) ||
        (frame[1] != STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) ||
        ((frame[2] != 0x00u) && (frame[2] != 0x01u)) ||
        (frame[5] != STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
        return false;
    }

    stepmotor_bus_rx_drop(fifo, STEPMOTOR_BUS_STEPMOTOR_SPEED_FRAME_LEN);

    raw_speed = (uint16_t)(((uint16_t)frame[3] << 8) | (uint16_t)frame[4]);
    signed_speed_raw = (frame[2] == 0x01u) ? (int32_t)raw_speed : -((int32_t)raw_speed);
    signed_speed_rpm = signed_speed_raw / (int32_t)EMM42_SPEED_SCALE_X10;

    //内联 2 轴映射，避免向前依赖 stepmotor_bus_axis_from_stepmotor_id
    if (frame[0] == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
        s_stepmotor_last_speed_raw[(uint32_t)axis] = signed_speed_raw;
        s_stepmotor_last_speed_rpm[(uint32_t)axis] = signed_speed_rpm;
    }
    else if (frame[0] == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_X;
        s_stepmotor_last_speed_raw[(uint32_t)axis] = signed_speed_raw;
        s_stepmotor_last_speed_rpm[(uint32_t)axis] = signed_speed_rpm;
    }
    return true;
}

//以下函数实现了对接收 FIFO 中数据的步进电机返回帧尝试消费操作，支持在处理完整帧数据前快速判断数据类型并进行分流处理，确保处理效率和正确分流
static bool stepmotor_bus_try_handle_stepmotor_return(StepmotorBus_RxFifo_t* fifo)
{
    uint8_t frame[STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN];
    uint8_t code = 0u;

    if ((fifo == NULL) ||
        (stepmotor_bus_rx_count(fifo) < STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN)) {
        return false;
    }

    //先根据 code 位分流：0x35 回复走 6 字节专用解析
    if (stepmotor_bus_rx_peek(fifo, 1u, &code) == true) {
        if (code == STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) {
            return stepmotor_bus_try_handle_speed_reply(fifo);
        }
    }

    if (stepmotor_bus_rx_copy(fifo, STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN, frame) ==
        false) {
        return false;
    }

    if ((stepmotor_bus_is_stepmotor_addr(frame[0]) == false) ||
        (stepmotor_bus_is_stepmotor_code(frame[1]) == false) ||
        (stepmotor_bus_is_stepmotor_data(frame[2]) == false) ||
        (frame[3] != STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
        return false;
    }

    stepmotor_bus_rx_drop(fifo, STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN);
    s_stepmotor_last_return_code = frame[2];
    if (stepmotor_bus_is_error_return_code(frame[2]) == true) {
        stepmotor_bus_control_error_inc();
    }
    return true;
}

//以下函数实现了根据步进电机地址判断对应控制轴的功能，支持在处理控制帧时根据地址正确分配到对应的控制槽，确保控制帧的正确管理和发送
static bool stepmotor_bus_axis_from_stepmotor_id(uint8_t axis_id,
    StepmotorBus_CtrlAxis_e* p_axis)
{
    if (p_axis == NULL) {
        return false;
    }

    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        *p_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
        return true;
    }

    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        *p_axis = STEPMOTOR_BUS_CTRL_AXIS_X;
        return true;
    }

    return false;
}

//以下函数实现了对管理帧队列中数据的预览和丢弃操作，支持在处理管理帧时正确管理队列状态，确保管理帧的正确发送和队列的一致性
static bool stepmotor_bus_mgmt_peek_locked(StepmotorBus_TxFrame_t* p_frame)
{
    StepmotorBus_TxFrame_t* slot = NULL;

    if ((p_frame == NULL) || (s_stepmotor_mgmt_queue.count == 0u)) {
        return false;
    }

    slot = &s_stepmotor_mgmt_queue.frames[s_stepmotor_mgmt_queue.tail];
    memcpy(p_frame->data, slot->data, slot->length);
    p_frame->length = slot->length;
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
        StepmotorBus_CtrlSlot_t* slot = &s_stepmotor_ctrl_slots[axis];

        if ((slot->dirty == true) && (slot->frame.length > 0u)) {
            return true;
        }
    }

    return false;
}

static bool stepmotor_bus_pick_control_frame_locked(StepmotorBus_TxFrame_t* p_frame,
    StepmotorBus_TxSource_e* p_source,
    StepmotorBus_CtrlAxis_e* p_axis)
{
    uint8_t index = 0u;

    if ((p_frame == NULL) || (p_source == NULL) ||
        (p_axis == NULL)) {
        return false;
    }

    if (stepmotor_bus_control_allowed() == false) {
        return false;
    }

    for (index = 0u; index < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; index++) {
        StepmotorBus_CtrlAxis_e axis =
            (StepmotorBus_CtrlAxis_e)(((uint8_t)s_stepmotor_next_ctrl_axis + index) %
                (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX);
        StepmotorBus_CtrlSlot_t* slot = &s_stepmotor_ctrl_slots[axis];

        if ((slot->dirty == true) && (slot->frame.length > 0u)) {
            memcpy(p_frame->data, slot->frame.data, slot->frame.length);
            p_frame->length = slot->frame.length;
            *p_source = STEPMOTOR_BUS_TX_SRC_CONTROL;
            *p_axis = axis;
            slot->dirty = false;
            s_stepmotor_next_ctrl_axis =
                (StepmotorBus_CtrlAxis_e)(((uint8_t)axis + 1u) %
                    (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX);
            return true;
        }
    }

    return false;
}

static bool stepmotor_bus_is_read_speed_frame(const uint8_t* frame,
    uint8_t len,
    uint8_t* p_axis_addr)
{
    if ((frame == NULL) || (len < 3u)) {
        return false;
    }

    if ((stepmotor_bus_is_stepmotor_addr(frame[0]) == false) ||
        (frame[1] != STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) ||
        (frame[len - 1u] != STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
        return false;
    }

    if (p_axis_addr != NULL) {
        *p_axis_addr = frame[0];
    }

    return true;
}

static bool stepmotor_bus_has_pending_read_speed_locked(uint8_t axis_addr)
{
    uint8_t index = 0u;

    if (s_stepmotor_mgmt_queue.count == 0u) {
        return false;
    }

    for (index = 0u; index < s_stepmotor_mgmt_queue.count; index++) {
        uint8_t slot_index =
            (uint8_t)((s_stepmotor_mgmt_queue.tail + index) % STEPMOTOR_BUS_MGMT_QUEUE_DEPTH);
        StepmotorBus_TxFrame_t* slot = &s_stepmotor_mgmt_queue.frames[slot_index];

        if ((slot->length >= 3u) &&
            (slot->data[0] == axis_addr) &&
            (slot->data[1] == STEPMOTOR_BUS_STEPMOTOR_CMD_READ_SPEED) &&
            (slot->data[slot->length - 1u] == STEPMOTOR_BUS_STEPMOTOR_ACK_CHECK)) {
            return true;
        }
    }

    return false;
}

//以下函数实现了对管理帧队列中数据的丢弃操作，支持在处理管理帧时正确管理队列状态，确保管理帧的正确发送和队列的一致性
static void stepmotor_bus_mgmt_drop_locked(void)
{
    if (s_stepmotor_mgmt_queue.count == 0u) {
        return;
    }

    s_stepmotor_mgmt_queue.tail =
        (uint8_t)((s_stepmotor_mgmt_queue.tail + 1u) % STEPMOTOR_BUS_MGMT_QUEUE_DEPTH);
    s_stepmotor_mgmt_queue.count--;
}

//以下函数实现了对控制帧的发送调度逻辑，支持在控制权限允许的情况下优先发送控制帧，并在没有控制帧时发送管理帧，确保发送调度的正确性和效率
static bool stepmotor_bus_pick_next_frame_locked(StepmotorBus_TxFrame_t* p_frame,
    StepmotorBus_TxSource_e* p_source,
    StepmotorBus_CtrlAxis_e* p_axis)
{
    bool has_ctrl = false;
    bool has_mgmt = false;

    if ((p_frame == NULL) || (p_source == NULL) ||
        (p_axis == NULL)) {
        return false;
    }

    has_ctrl = stepmotor_bus_ctrl_has_pending_locked();
    has_mgmt = (bool)(s_stepmotor_mgmt_queue.count > 0u);

    if (has_ctrl == true) {
        if ((s_stepmotor_prefer_mgmt_next == false) || (has_mgmt == false)) {
            if (stepmotor_bus_pick_control_frame_locked(p_frame, p_source, p_axis) == true) {
                s_stepmotor_prefer_mgmt_next = (bool)(has_mgmt == true);
                return true;
            }
        }
    }

    if (has_mgmt == true) {
        if (stepmotor_bus_mgmt_peek_locked(p_frame) == true) {
            *p_source = STEPMOTOR_BUS_TX_SRC_MGMT;
            *p_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
            s_stepmotor_prefer_mgmt_next = false;
            return true;
        }
    }

    if (has_ctrl == true) {
        if (stepmotor_bus_pick_control_frame_locked(p_frame, p_source, p_axis) == true) {
            s_stepmotor_prefer_mgmt_next = true;
            return true;
        }
    }

    if (stepmotor_bus_mgmt_peek_locked(p_frame) == true) {
        *p_source = STEPMOTOR_BUS_TX_SRC_MGMT;
        *p_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
        s_stepmotor_prefer_mgmt_next = false;
        return true;
    }

    return false;
}

//以下函数实现了对发送调度的尝试启动逻辑，支持在当前没有发送任务且 DMA 通道空闲的情况下根据优先级选择下一帧进行发送，确保发送调度的正确性和效率
static void stepmotor_bus_try_start_tx(void)
{
    StepmotorBus_TxFrame_t frame;
    StepmotorBus_TxSource_e source = STEPMOTOR_BUS_TX_SRC_NONE;
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    uint32_t primask = 0u;
    bool send_ok = false;
    bool dma_started = false;

    primask = stepmotor_bus_irq_lock();

    if ((s_stepmotor_tx_dispatch_busy == true) ||
        (Mspm0Runtime_IsStepmotorTxBusy() != false)) {
        stepmotor_bus_irq_unlock(primask);
        return;
    }

    if (stepmotor_bus_pick_next_frame_locked(&frame, &source, &axis) == false) {
        stepmotor_bus_irq_unlock(primask);
        return;
    }

    s_stepmotor_tx_dispatch_busy = true;
    stepmotor_bus_irq_unlock(primask);

    send_ok = Mspm0Runtime_SendStepmotor(frame.data, (uint32_t)frame.length) ?
              true : false;

    primask = stepmotor_bus_irq_lock();

    if (send_ok == true) {
        if (source == STEPMOTOR_BUS_TX_SRC_MGMT) {
            stepmotor_bus_mgmt_drop_locked();
        }

        dma_started = Mspm0Runtime_IsStepmotorTxBusy() ? true : false;
        if (dma_started == false) {
            s_stepmotor_tx_dispatch_busy = false;
        }
    }
    else {
        if (source == STEPMOTOR_BUS_TX_SRC_CONTROL) {
            StepmotorBus_CtrlSlot_t* slot = &s_stepmotor_ctrl_slots[axis];

            if (slot->dirty == false) {
                slot->dirty = true;
            }
        }

        stepmotor_bus_control_error_inc();
        s_stepmotor_tx_dispatch_busy = false;
    }

    stepmotor_bus_irq_unlock(primask);

    if ((send_ok == true) && (dma_started == false)) {
        stepmotor_bus_try_start_tx();
    }
}

/* UART TX complete via mspm0_runtime DMA IRQ (not legacy HAL TX callback). */
static void stepmotor_bus_uart_tx_callback(void)
{
    uint32_t primask = 0u;

    primask = stepmotor_bus_irq_lock();
    s_stepmotor_tx_dispatch_busy = false;
    stepmotor_bus_irq_unlock(primask);

    stepmotor_bus_try_start_tx();
}

//以下函数实现了对管理帧的入队逻辑，支持在管理帧有效且队列未满的情况下将新帧加入队列，并尝试启动发送，确保管理帧的正确管理和发送
static StepmotorBus_Status_e stepmotor_bus_enqueue_mgmt_frame(const uint8_t* frame, uint8_t len)
{
    uint32_t primask = 0u;
    StepmotorBus_TxFrame_t* slot = NULL;
    uint8_t read_speed_axis = 0u;

    if ((frame == NULL) || (len == 0u) ||
        (len > STEPMOTOR_BUS_TX_FRAME_MAX_LEN)) {
        return STEPMOTOR_BUS_ERR_INVALID;
    }

    primask = stepmotor_bus_irq_lock();

    if ((stepmotor_bus_is_read_speed_frame(frame, len, &read_speed_axis) == true) &&
        (stepmotor_bus_has_pending_read_speed_locked(read_speed_axis) == true)) {
        stepmotor_bus_irq_unlock(primask);
        return STEPMOTOR_BUS_OK;
    }

    if (s_stepmotor_mgmt_queue.count >= STEPMOTOR_BUS_MGMT_QUEUE_DEPTH) {
        stepmotor_bus_irq_unlock(primask);
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

    stepmotor_bus_irq_unlock(primask);

    stepmotor_bus_try_start_tx();
    return STEPMOTOR_BUS_OK;
}

//以下函数实现了对控制帧的提交逻辑，支持在控制权限允许且参数有效的情况下将控制帧写入对应控制槽，并尝试启动发送，确保控制帧的正确管理和发送
static StepmotorBus_Status_e stepmotor_bus_submit_control_frame(StepmotorBus_CtrlAxis_e axis,
    const uint8_t* frame,
    uint8_t len)
{
    uint32_t primask = 0u;
    StepmotorBus_CtrlSlot_t* slot = NULL;

    if (((uint32_t)axis >= (uint32_t)STEPMOTOR_BUS_CTRL_AXIS_MAX) ||
        (frame == NULL) || (len == 0u) ||
        (len > STEPMOTOR_BUS_TX_FRAME_MAX_LEN)) {
        stepmotor_bus_control_error_inc();
        return STEPMOTOR_BUS_ERR_INVALID;
    }

    if (stepmotor_bus_control_allowed() == false) {
        stepmotor_bus_control_error_inc();
        return STEPMOTOR_BUS_ERR_NOT_READY;
    }

    primask = stepmotor_bus_irq_lock();

    slot = &s_stepmotor_ctrl_slots[axis];
    memcpy(slot->frame.data, frame, len);
    slot->frame.length = len;
    slot->dirty = true;

    stepmotor_bus_irq_unlock(primask);

    stepmotor_bus_try_start_tx();
    return STEPMOTOR_BUS_OK;
}

static void stepmotor_bus_service_rx(void)
{
    uint16_t processed_bytes = 0u;

    stepmotor_bus_discard_stale_partial();

    while (processed_bytes < STEPMOTOR_BUS_RX_PROCESS_BUDGET) {
        if (stepmotor_bus_rx_count(&s_stepmotor_rx_fifo) == 0u) {
            break;
        }

        if (stepmotor_bus_try_handle_stepmotor_return(&s_stepmotor_rx_fifo) ==
            true) {
            processed_bytes =
                (uint16_t)(processed_bytes + STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN);
            continue;
        }

        //不足一整帧且前缀合法时等下一次服务，避免中间截断
        if ((stepmotor_bus_rx_count(&s_stepmotor_rx_fifo) < STEPMOTOR_BUS_STEPMOTOR_FRAME_LEN) &&
            (stepmotor_bus_has_stepmotor_prefix(&s_stepmotor_rx_fifo) == true)) {
            break;
        }

        stepmotor_bus_rx_drop(&s_stepmotor_rx_fifo, 1u);
        processed_bytes++;
    }

    stepmotor_bus_discard_stale_partial();
}

/* ---- 传输适配导出 ------------------------------------------------------- */

int Emm42_TransportSendMgmtFrame(const uint8_t* frame, uint8_t len)
{
    return (int)stepmotor_bus_enqueue_mgmt_frame(frame, len);
}

int Emm42_TransportSubmitControlFrame(uint8_t axis_id,
    const uint8_t* frame,
    uint8_t len)
{
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;

    if (stepmotor_bus_axis_from_stepmotor_id(axis_id, &axis) == false) {
        stepmotor_bus_control_error_inc();
        return (int)STEPMOTOR_BUS_ERR_INVALID;
    }

    return (int)stepmotor_bus_submit_control_frame(axis, frame, len);
}

/* ---- 公开 API ----------------------------------------------------------- */

/* 旁路开关：DEBUG 压测场景下置 true，冻结本模块状态机 */
static volatile bool s_stepmotor_bypass = false;

/**
 * @brief 步进电机总线初始化
 * @note  复位收发状态，并注册步进电机 UART 的发送回调
 */
void StepmotorBus_Init(void)
{
    //清零所有静态状态变量，确保总线模块从一个干净的状态开始运行，避免因残留数据导致的异常行为
    memset(&s_stepmotor_rx_fifo, 0, sizeof(s_stepmotor_rx_fifo));
    memset(&s_stepmotor_mgmt_queue, 0, sizeof(s_stepmotor_mgmt_queue));
    memset(&s_stepmotor_ctrl_slots, 0, sizeof(s_stepmotor_ctrl_slots));

    //初始化发送调度状态
    s_stepmotor_tx_dispatch_busy = false;
    s_stepmotor_next_ctrl_axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    s_stepmotor_control_gate_enabled = false;
    s_stepmotor_control_error_count = 0u;
    s_stepmotor_last_return_code = 0u;
    {
        uint32_t axis = 0u;
        for (axis = 0u; axis < (uint32_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; axis++) {
            s_stepmotor_last_speed_raw[axis] = 0;
            s_stepmotor_last_speed_rpm[axis] = 0;
        }
    }

    Mspm0Runtime_SetStepmotorTxCallback(stepmotor_bus_uart_tx_callback);
}

/**
 * @brief 步进电机串口接收 ISR 入口
 * @param data 本次接收到的单字节数据
 */
void StepmotorBus_RxISR(uint8_t data)
{
    if (s_stepmotor_bypass == true) {
        return;//旁路态下不累积 RX，交由外部回调接管
    }
    (void)stepmotor_bus_rx_push(&s_stepmotor_rx_fifo, data);
}

/**
 * @brief 步进电机总线 5ms 周期服务
 * @note  先过滤电机应答帧，再尝试推进发送侧调度
 */
void StepmotorBus_Service5ms(void)
{
    if (s_stepmotor_bypass == true) {
        return;//旁路态下不消费 RX 也不推进 TX 调度
    }
    stepmotor_bus_service_rx();
    stepmotor_bus_try_start_tx();
}

/**
 * @brief 旁路开关实现
 * @note  置 TRUE 时清空内部 RX FIFO，避免恢复后误解析残留字节
 */
void StepmotorBus_SetBypass(bool bypass)
{
    s_stepmotor_bypass = bypass;
    if (bypass == true) {
        stepmotor_bus_rx_reset(&s_stepmotor_rx_fifo);
    }
}

/**
 * @brief 设置控制帧门控
 * @param enable true=允许控制帧，false=拒绝控制帧
 */
void StepmotorBus_SetControlGate(bool enable)
{
    s_stepmotor_control_gate_enabled = enable;
}

/**
 * @brief 复位控制链路诊断计数
 * @note  在临界区内同步清零，避免与 ISR 并发冲突
 */
void StepmotorBus_ResetDiagCounters(void)
{
    uint32_t primask = stepmotor_bus_irq_lock();
    s_stepmotor_control_error_count = 0u;
    s_stepmotor_last_return_code = 0u;
    stepmotor_bus_irq_unlock(primask);
}

/**
 * @brief 读取控制链路累计错误计数
 * @return 错误累计次数
 */
uint32_t StepmotorBus_GetControlErrorCount(void)
{
    uint32_t value = 0u;
    uint32_t primask = stepmotor_bus_irq_lock();
    value = s_stepmotor_control_error_count;
    stepmotor_bus_irq_unlock(primask);
    return value;
}

/**
 * @brief 读取最近一次电机返回码
 * @return 电机协议返回码
 */
uint8_t StepmotorBus_GetLastReturnCode(void)
{
    uint8_t value = 0u;
    uint32_t primask = stepmotor_bus_irq_lock();
    value = s_stepmotor_last_return_code;
    stepmotor_bus_irq_unlock(primask);
    return value;
}

/**
 * @brief 清空待发送控制帧槽位
 * @note  仅清理软件槽，不中止当前在途 DMA 发送
 */
void StepmotorBus_ClearControlFrames(void)
{
    uint8_t axis = 0u;
    uint32_t primask = stepmotor_bus_irq_lock();

    for (axis = 0u; axis < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; axis++) {
        s_stepmotor_ctrl_slots[axis].dirty = false;
        s_stepmotor_ctrl_slots[axis].frame.length = 0u;
    }

    stepmotor_bus_irq_unlock(primask);
}

/**
 * @brief 查询控制路径是否空闲
 * @return true 表示无脏槽且发送调度和 DMA 都不忙
 */
bool StepmotorBus_IsControlPathIdle(void)
{
    uint32_t primask = stepmotor_bus_irq_lock();
    bool has_dirty = false;
    bool tx_dispatch_busy = s_stepmotor_tx_dispatch_busy;
    uint8_t axis = 0u;

    for (axis = 0u; axis < (uint8_t)STEPMOTOR_BUS_CTRL_AXIS_MAX; axis++) {
        if (s_stepmotor_ctrl_slots[axis].dirty == true) {
            has_dirty = true;
            break;
        }
    }

    stepmotor_bus_irq_unlock(primask);

    if ((tx_dispatch_busy == true) ||
        (Mspm0Runtime_IsStepmotorTxBusy() != false) ||
        (has_dirty == true)) {
        return false;
    }

    return true;
}

/**
 * @brief 读取最近一次成功解析的 0x35 反馈速度
 * @param axis_id 电机轴地址（1=Y，2=X）
 * @return 带符号整数 RPM；按当前上层显示口径，将原始字段缩放为 RPM；
 *         无效地址或未收到回复返回 0
 */
int32_t StepmotorBus_GetLastSpeedRpm(uint8_t axis_id)
{
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    int32_t value = 0;
    uint32_t primask = 0u;

    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    }
    else if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_X;
    }
    else {
        return 0;
    }

    primask = stepmotor_bus_irq_lock();
    value = s_stepmotor_last_speed_rpm[(uint32_t)axis];
    stepmotor_bus_irq_unlock(primask);
    return value;
}

int32_t StepmotorBus_GetLastSpeedRaw(uint8_t axis_id)
{
    StepmotorBus_CtrlAxis_e axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    int32_t value = 0;
    uint32_t primask = 0u;

    if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_Y) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_Y;
    }
    else if (axis_id == STEPMOTOR_BUS_STEPMOTOR_ADDR_X) {
        axis = STEPMOTOR_BUS_CTRL_AXIS_X;
    }
    else {
        return 0;
    }

    primask = stepmotor_bus_irq_lock();
    value = s_stepmotor_last_speed_raw[(uint32_t)axis];
    stepmotor_bus_irq_unlock(primask);
    return value;
}
