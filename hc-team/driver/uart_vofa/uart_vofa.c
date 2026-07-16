/*******************************************************************************
 * @file        uart_vofa_v2.c
 * @brief       VOFA+上位机串口通信协议实现 (Decoupled V2)
 * @version     V2.1
 * @date        2025-11-23
 * @author      eternal_fu, 中性粒
 *******************************************************************************/
#include "uart_vofa.h"
#include "driver/mspm0_runtime/mspm0_runtime.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/***************************************************************************************************************
 *                                           内部类型定义 (Internal Types)
 **************************************************************************************************************/

/* 发送通道类型 */
typedef enum {
    VOFA_TYPE_FLOAT = 0,
    VOFA_TYPE_INT
} vofa_data_type_t;

/* 内部数据项结构体 */
typedef struct {
    vofa_data_type_t type;
    void *data;
    uint8_t count;
} vofa_item_t;

/* 接收参数配置结构体 */
typedef struct {
    const char *cmd;
    volatile float *val_ptr;
} vofa_recv_param_t;

/* 策略模式：发送策略函数指针定义 */
typedef void (*vofa_send_strategy_fn)(void);

/***************************************************************************************************************
 *                                           全局变量 (Global Variables)
 **************************************************************************************************************/

/* 发送相关 */
static vofa_item_t g_data_list[VOFA_CHANNEL_MAX];       /* 注册的数据源列表 */
static uint8_t g_data_count = 0;                         /* 已注册的数据源数量 */
static uint8_t g_tx_buf[VOFA_TX_BUF_SIZE];              /* 发送缓冲区 */
static vofa_send_fn_t g_send_fn = NULL;                  /* 底层发送函数指针 */
static vofa_send_strategy_fn g_send_strategy = NULL;     /* 协议策略函数指针 */

/* 接收相关 */
static vofa_recv_param_t g_recv_table[VOFA_RX_PARAM_MAX]; /* 接收命令绑定表 */
static uint8_t g_recv_count = 0;                          /* 已绑定的命令数量 */
static uint8_t g_rx_parse_buf[64];                        /* 接收解析缓冲区 */
static uint8_t g_rx_parse_idx = 0;                        /* 接收缓冲区索引 */

/***************************************************************************************************************
 *                                           前向声明 (Forward Declarations)
 **************************************************************************************************************/

/* 协议层策略 / 组包函数（按 VOFA_PROTOCOL_SELECT 编译，避免未选中协议告警） */
#if (VOFA_PROTOCOL_SELECT == VOFA_PROTOCOL_FIREWATER)
static void vofa_strategy_firewater(void);
static uint16_t vofa_pack_firewater(uint8_t *buf, uint16_t buf_size);
static inline void vofa_finalize_csv_frame(char *buf, uint16_t *pos, uint16_t buf_size);
#elif (VOFA_PROTOCOL_SELECT == VOFA_PROTOCOL_JUSTFLOAT)
static void vofa_strategy_justfloat(void);
static uint16_t vofa_pack_justfloat(uint8_t *buf, uint16_t buf_size);
#endif

/* 协议层辅助函数（内联优化） */
static inline float vofa_get_value_as_float(const vofa_item_t *item, uint8_t index);

/* 协议层解析函数 */
static void vofa_parse_rx_frame(const char *frame);
static void vofa_parse_rx_token(char *token);
static void vofa_apply_bound_value(const char *cmd, float val);
static int vofa_cmd_equals_ignore_case(const char *lhs, const char *rhs);
static char *vofa_trim_inplace(char *str);
static void vofa_rx_reset_buffer(void);
static void vofa_clear_profile_internal(void);

/* 应用层发送函数 */
static void vofa_send_data(void);

/* 驱动层函数 */
void vofa_driver_hal_send(uint8_t *puc_data, uint16_t us_len);

/***************************************************************************************************************
 *                                               应用层 (Application Layer)
 *                                          负责：用户接口、通道管理、回调分发
 **************************************************************************************************************/

/**
 * @brief   初始化VOFA模块
 * @return  0=成功, -1=失败
 * @details 重置所有内部状态，清空注册表，并根据平台执行特定初始化
 */
int vofa_init(void)
{
    /* 1. 清空缓冲区 */
    memset(g_tx_buf, 0, sizeof(g_tx_buf));
    vofa_clear_profile_internal();
    
    /* 2. 根据宏定义选择协议策略 */
#if (VOFA_PROTOCOL_SELECT == VOFA_PROTOCOL_FIREWATER)
    g_send_strategy = vofa_strategy_firewater;
#elif (VOFA_PROTOCOL_SELECT == VOFA_PROTOCOL_JUSTFLOAT)
    g_send_strategy = vofa_strategy_justfloat;
#else
    g_send_strategy = NULL;
    return -1;  /* 未定义协议 */
#endif

    /* 3. 平台特定初始化 */
#if DEVICE_TYPE == DEVICE_TYPE_STM32
    /* STM32 HAL库平台 */
    g_send_fn = vofa_driver_hal_send;
    
    return 0;
#elif DEVICE_TYPE == DEVICE_TYPE_MSPM0
    /* MSPM0 DriverLib 平台 */
    g_send_fn = vofa_driver_hal_send;

    return 0;
#elif DEVICE_TYPE == DEVICE_TYPE_ESP32
    /* ESP32平台（预留） */
    // g_send_fn = esp32_uart_send;
    return -1;  /* 暂未实现 */
#else
    /* 未知平台 */
    return -1;
#endif
}

void vofa_clear_profile(void)
{
    vofa_clear_profile_internal();
}

/**
 * @brief   注册Float类型数据源
 * @param   data 数据指针
 * @return  0:成功, -1:失败
 */
int vofa_register_float(float *data)
{
    if (data == NULL || g_data_count >= VOFA_CHANNEL_MAX) return -1;
    
    g_data_list[g_data_count].type = VOFA_TYPE_FLOAT;
    g_data_list[g_data_count].data = data;
    g_data_list[g_data_count].count = 1;
    g_data_count++;
    
    return 0;
}

/**
 * @brief   注册Int类型数据源
 * @param   data 数据指针
 * @return  0:成功, -1:失败
 */
int vofa_register_int(int *data)
{
    if (data == NULL || g_data_count >= VOFA_CHANNEL_MAX) return -1;
    
    g_data_list[g_data_count].type = VOFA_TYPE_INT;
    g_data_list[g_data_count].data = data;
    g_data_list[g_data_count].count = 1;
    g_data_count++;
    
    return 0;
}

/**
 * @brief   绑定接收命令与变量
 * @param   cmd      命令字符串
 * @param   val_ptr  绑定变量指针
 * @return  0:成功, -1:失败
 */
int vofa_bind_cmd(const char *cmd, volatile float *val_ptr)
{
    /* 检查参数 */
    if (cmd == NULL || val_ptr == NULL) return -1;

    /* 检查是否已存在，存在则更新 */
    for (int i = 0; i < g_recv_count; i++) {
        if (strcmp(g_recv_table[i].cmd, cmd) == 0) {
            g_recv_table[i].val_ptr = val_ptr;
            return 0;
        }
    }
    
    /* 新增 */
    if (g_recv_count >= VOFA_RX_PARAM_MAX) return -1;
    
    g_recv_table[g_recv_count].cmd = cmd;
    g_recv_table[g_recv_count].val_ptr = val_ptr;
    g_recv_count++;
    
    return 0;
}

/**
 * @brief   VOFA主运行函数
 * @details 遍历所有有效通道并发送数据。
 *          通常在定时器中断或主循环中定时调用 (如10ms一次)
 */
void vofa_run(void)
{
    vofa_send_data();
}

/**
 * @brief   发送所有注册的数据
 * @details 通过策略模式调用具体的协议打包函数
 */
void vofa_send_data(void)
{
    if (g_send_fn == NULL || g_data_count == 0 || g_send_strategy == NULL) return;
    
    g_send_strategy();
}

/***************************************************************************************************************
 *                                               协议层 (Protocol Layer)
 *                                     负责：策略选择、数据打包、数据解包
 **************************************************************************************************************/

/* ============================================ 策略 / 组包函数 ============================================ */

#if (VOFA_PROTOCOL_SELECT == VOFA_PROTOCOL_FIREWATER)
/**
 * @brief   FireWater协议发送策略 (CSV格式)
 */
static void vofa_strategy_firewater(void)
{
    uint16_t len = vofa_pack_firewater(g_tx_buf, VOFA_TX_BUF_SIZE);
    if (len > 0) {
        g_send_fn(g_tx_buf, len);
    }
}

/**
 * @brief   FireWater协议组包函数 (CSV格式)
 * @param   buf       缓冲区指针
 * @param   buf_size  缓冲区大小
 * @return  打包后的数据长度
 */
static uint16_t vofa_pack_firewater(uint8_t *buf, uint16_t buf_size)
{
    uint16_t pos = 0;
    char *str_buf = (char *)buf;
    
    /* 遍历所有注册的数据源 */
    for (int i = 0; i < g_data_count; i++) {
        if (!g_data_list[i].data) continue;
        
        /* 遍历该数据源的所有元素 */
        for (int j = 0; j < g_data_list[i].count; j++) {
            float val = vofa_get_value_as_float(&g_data_list[i], j);
            
            /* 检查缓冲区剩余空间 (保留2字节用于换行) */
            if (pos < buf_size - 2) {
                int len = snprintf(str_buf + pos, buf_size - pos, "%.2f,", val);
                if (len > 0 && pos + len < buf_size) {
                    pos += len;
                } else {
                    pos = buf_size; /* 标记已满 */
                    break;
                }
            } else {
                break;
            }
        }
        if (pos >= buf_size) break;
    }
    
    /* 完成帧封装（替换逗号为换行符） */
    vofa_finalize_csv_frame(str_buf, &pos, buf_size);
    
    return pos;
}

/**
 * @brief   完成CSV帧封装（替换最后逗号为换行符）
 */
static inline void vofa_finalize_csv_frame(char *buf, uint16_t *pos, uint16_t buf_size)
{
    if (*pos > 0 && *pos < buf_size) {
        if (buf[*pos - 1] == ',') {
            buf[*pos - 1] = '\n'; /* 替换最后的逗号 */
        } else {
            buf[(*pos)++] = '\n'; /* 追加换行符 */
        }
    } else if (*pos >= buf_size) {
        /* 缓冲区满，强制最后一个字符为换行 */
        buf[buf_size - 1] = '\n';
        *pos = buf_size;
    }
}
#elif (VOFA_PROTOCOL_SELECT == VOFA_PROTOCOL_JUSTFLOAT)
/**
 * @brief   JustFloat协议发送策略 (二进制浮点)
 */
static void vofa_strategy_justfloat(void)
{
    uint16_t len = vofa_pack_justfloat(g_tx_buf, VOFA_TX_BUF_SIZE);
    if (len > 0) {
        g_send_fn(g_tx_buf, len);
    }
}

/**
 * @brief   JustFloat协议组包函数 (二进制浮点)
 * @param   buf       缓冲区指针
 * @param   buf_size  缓冲区大小
 * @return  打包后的数据长度
 */
static uint16_t vofa_pack_justfloat(uint8_t *buf, uint16_t buf_size)
{
    uint16_t pos = 0;
    uint8_t tail[] = VOFA_JUSTFLOAT_TAIL;
    union {
        float f;
        uint8_t b[4];
    } converter;
    
    for (int i = 0; i < g_data_count; i++) {
        if (!g_data_list[i].data) continue;
        
        for (int j = 0; j < g_data_list[i].count; j++) {
            /* 获取float值（支持类型转换） */
            float val = vofa_get_value_as_float(&g_data_list[i], j);
            
            converter.f = val;
            
            if (pos + 4 <= buf_size) {
                buf[pos++] = converter.b[0];
                buf[pos++] = converter.b[1];
                buf[pos++] = converter.b[2];
                buf[pos++] = converter.b[3];
            } else {
                break; 
            }
        }
    }
    
    /* 添加帧尾 */
    if (pos + 4 <= buf_size) {
        memcpy(&buf[pos], tail, 4);
        pos += 4;
    }
    
    return pos;
}
#endif

/* ============================================ 辅助函数 ============================================ */

/**
 * @brief   从数据项中获取float值（支持类型转换）
 * @param   item  数据项指针
 * @param   index 数组索引
 * @return  float值
 * @note    内联函数，优化性能
 */
static inline float vofa_get_value_as_float(const vofa_item_t *item, uint8_t index)
{
    if (item->type == VOFA_TYPE_FLOAT) {
        return ((float*)item->data)[index];
    } else {
        return (float)((int*)item->data)[index];
    }
}

/* ============================================ 解包函数 ============================================ */

/**
 * @brief   解析接收帧（命令格式：cmd=value）
 * @param   frame 完整的帧字符串（不含换行符）
 * @note    支持格式："kp=1.5", "speed=100", "enable=1"
 *          所有值按float解析
 */
static void vofa_parse_rx_frame(const char *frame)
{
    char *work_frame = (char *)frame;
    char *token_end = NULL;
    char saved_ch = '\0';

    if (frame == NULL || frame[0] == '\0') return;

    work_frame = vofa_trim_inplace(work_frame);
    if ((work_frame == NULL) || (work_frame[0] == '\0')) {
        return;
    }

    while ((work_frame != NULL) && (work_frame[0] != '\0')) {
        token_end = work_frame;
        while ((*token_end != '\0') &&
               (*token_end != ';') &&
               (*token_end != ',')) {
            token_end++;
        }

        saved_ch = *token_end;
        *token_end = '\0';
        vofa_parse_rx_token(work_frame);

        if (saved_ch == '\0') {
            break;
        }

        work_frame = vofa_trim_inplace(token_end + 1);
    }
}

static void vofa_parse_rx_token(char *token)
{
    char *sep_pos = NULL;
    char *cmd = NULL;
    char *value_str = NULL;
    float val = 0.0f;

    if (token == NULL) {
        return;
    }

    token = vofa_trim_inplace(token);
    if ((token == NULL) || (token[0] == '\0')) {
        return;
    }

    sep_pos = strchr(token, '=');
    if (sep_pos == NULL) {
        sep_pos = strchr(token, ':');
    }

    if (sep_pos == NULL) {
        return;
    }

    *sep_pos = '\0';
    cmd = vofa_trim_inplace(token);
    value_str = vofa_trim_inplace(sep_pos + 1);

    if ((cmd == NULL) || (value_str == NULL) ||
        (cmd[0] == '\0') || (value_str[0] == '\0')) {
        return;
    }

    val = (float)atof(value_str);
    vofa_apply_bound_value(cmd, val);
}

static void vofa_apply_bound_value(const char *cmd, float val)
{
    int i = 0;

    if (cmd == NULL) {
        return;
    }

    for (i = 0; i < g_recv_count; i++) {
        if (vofa_cmd_equals_ignore_case(g_recv_table[i].cmd, cmd) != 0) {
            if (g_recv_table[i].val_ptr != NULL) {
                *g_recv_table[i].val_ptr = val;
            }
            break;
        }
    }
}

static int vofa_cmd_equals_ignore_case(const char *lhs, const char *rhs)
{
    unsigned char ch_l = 0u;
    unsigned char ch_r = 0u;

    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }

    while ((*lhs != '\0') && (*rhs != '\0')) {
        ch_l = (unsigned char)*lhs;
        ch_r = (unsigned char)*rhs;

        if (tolower(ch_l) != tolower(ch_r)) {
            return 0;
        }

        lhs++;
        rhs++;
    }

    return ((*lhs == '\0') && (*rhs == '\0')) ? 1 : 0;
}

/***************************************************************************************************************
 *                                               设备层 (Driver Layer)
 *                                          负责：硬件中断处理、底层IO接口
 **************************************************************************************************************/

/* ============================================ 发送接口 ============================================ */

/**
 * @brief   【驱动层接口】发送字节数组到 VOFA UART
 * @param   puc_data  待发送的数据指针
 * @param   us_len    数据长度（字节）
 * @return  无
 * @note    VOFA 库通过该接口调用 runtime VOFA 发送
 */
void vofa_driver_hal_send(uint8_t *puc_data, uint16_t us_len)
{
    if ((puc_data == NULL) || (us_len == 0u)) {
        return;
    }

    (void)Mspm0Runtime_SendVofa(puc_data, (uint32_t)us_len);
}

static char *vofa_trim_inplace(char *str)
{
    char *end = NULL;

    if (str == NULL) {
        return NULL;
    }

    while ((*str != '\0') && (isspace((unsigned char)*str) != 0)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    end = str + strlen(str) - 1;
    while ((end >= str) && (isspace((unsigned char)*end) != 0)) {
        *end = '\0';
        end--;
    }

    return str;
}

static void vofa_rx_reset_buffer(void)
{
    g_rx_parse_idx = 0;
    memset(g_rx_parse_buf, 0, sizeof(g_rx_parse_buf));
}

static void vofa_clear_profile_internal(void)
{
    g_data_count = 0;
    memset(g_data_list, 0, sizeof(g_data_list));

    g_recv_count = 0;
    memset(g_recv_table, 0, sizeof(g_recv_table));

    vofa_rx_reset_buffer();
}

/* ============================================ 接收接口 ============================================ */

/**
 * @brief   【驱动层接口】串口接收中断处理
 * @param   byte 接收到的字节
 * @note    负责字节接收和帧边界检测
 *          检测到完整帧后调用协议层解析
 *          在UART中断回调中调用此函数
 */
void vofa_rx_isr(uint8_t byte)
{
    if ((byte == (uint8_t)'\r') ||
        (byte == (uint8_t)'\n') ||
        (byte == (uint8_t)';')) {
        if (g_rx_parse_idx == 0u) {
            return;
        }

        g_rx_parse_buf[g_rx_parse_idx] = '\0';
        vofa_parse_rx_frame((const char *)g_rx_parse_buf);
        vofa_rx_reset_buffer();
        return;
    }

    /* 防止缓冲区溢出 */
    if (g_rx_parse_idx >= sizeof(g_rx_parse_buf) - 1) {
        vofa_rx_reset_buffer();
        return;
    }
    
    /* 累积接收字节 */
    g_rx_parse_buf[g_rx_parse_idx++] = byte;
    
}

