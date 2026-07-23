#ifndef __BSP_UART_ASR_H
#define __BSP_UART_ASR_H
#include "py32f0xx_hal.h"

/* 语音芯片交互协议 v0.7（ASCII 文本；两方向均带帧尾：MCU 发帧尾 `\n`，收帧尾 `\r\n`；
   tag 用 `=` 分隔十进制数值；命令 ID ∈ [1,16] 播报 ID ∈ [17,41] 不重叠）。
   权威文档见 resource/语音芯片交互协议.md 与 resource/MCU协议注意事项.md */

/* --- 语音 ID（十进制，供 Bsp_UartAsr_SendPlay 使用；v0.7 §三 17-41 连续） --- */
#define ASR_VOICE_BOOT           17  /* 你好呀我是小白进入语音模式（开机语） */
#define ASR_VOICE_SHUTDOWN       18  /* 再见啦，小白去休息了（关机语） */
#define ASR_VOICE_ENTER_POWER    19  /* 进入动力模式 */
#define ASR_VOICE_ENTER_SENSOR   20  /* 进入感应模式 */
#define ASR_VOICE_ENTER_REMOTE   21  /* 进入遥控模式 */
#define ASR_VOICE_ENTER_VOICE    22  /* 进入语音模式 */
#define ASR_VOICE_BLE_CONNECTED  23  /* 遥控已连接 */
#define ASR_VOICE_BLE_LOST       24  /* 遥控已断开 */
#define ASR_VOICE_LOW_BATTERY    25  /* 低电量 */
#define ASR_VOICE_RECEIVED       26  /* 收到 */
#define ASR_VOICE_FORWARD        27  /* 前进 */
#define ASR_VOICE_BACKWARD       28  /* 后退 */
#define ASR_VOICE_LEFT           29  /* 左转 */
#define ASR_VOICE_RIGHT          30  /* 右转 */
#define ASR_VOICE_STOP           31  /* 停止 */
#define ASR_VOICE_APPROACH_GO    32  /* 靠近启动 */
#define ASR_VOICE_OBSTACLE_STOP  33  /* 遇障停止 */
#define ASR_VOICE_WAVE_TOGGLE    34  /* 挥手开关 */
#define ASR_VOICE_BRIGHTNESS     35  /* 明暗调速 */
#define ASR_VOICE_L_FWD          36  /* 左电机正转 */
#define ASR_VOICE_L_REV          37  /* 左电机反转 */
#define ASR_VOICE_L_STOP         38  /* 左电机停止 */
#define ASR_VOICE_R_FWD          39  /* 右电机正转 */
#define ASR_VOICE_R_REV          40  /* 右电机反转 */
#define ASR_VOICE_R_STOP         41  /* 右电机停止 */

/* --- 命令 ID（十进制，Bsp_UartAsr_TryRecv 收到 CMD 事件时 arg = 这些值之一；v0.7 §四 1-16 连续） --- */
#define ASR_CMD_SHUTDOWN         1   /* 关机 */
#define ASR_CMD_ENTER_POWER      2
#define ASR_CMD_ENTER_SENSOR     3
#define ASR_CMD_ENTER_REMOTE     4
#define ASR_CMD_ENTER_VOICE      5
#define ASR_CMD_FORWARD          6
#define ASR_CMD_BACKWARD         7
#define ASR_CMD_LEFT             8
#define ASR_CMD_RIGHT            9
#define ASR_CMD_STOP             10
#define ASR_CMD_L_FWD            11
#define ASR_CMD_L_REV            12
#define ASR_CMD_L_STOP           13
#define ASR_CMD_R_FWD            14
#define ASR_CMD_R_REV            15
#define ASR_CMD_R_STOP           16

typedef enum {
    ASR_EVT_NONE = 0,
    ASR_EVT_CMD  = 1,   /* ASRPRO 识别到语音命令，arg = 命令 ID (ASR_CMD_*) */
    ASR_EVT_WAKE = 2,   /* ASRPRO 检测到唤醒词，arg 无意义 */
    ASR_EVT_DONE = 3,   /* ASRPRO 播报完成，arg = 上次的语音 ID */
} Bsp_UartAsr_EvtType_t;

typedef struct {
    Bsp_UartAsr_EvtType_t type;
    uint8_t               arg;
} Bsp_UartAsr_Event_t;

/** 初始化 USART2 (PF0/PF1, 9600 8N1) + DMA 收 + IDLE 中断 */
void Bsp_UartAsr_Init(void);

/** 发送 "play=NN\n" 请求播报指定语音 ID（十进制） */
void Bsp_UartAsr_SendPlay(uint8_t voice_id);

/** 发送 "stop\n" */
void Bsp_UartAsr_SendStop(void);

/** 发送 "ping\n" */
void Bsp_UartAsr_SendPing(void);

/**
 * @brief 发送原始字节（调试用，绕过协议格式化）。
 *        直接 HAL_UART_Transmit，不加任何 tag/分隔符/帧尾。
 *        供调试时把其它来源的数据透传到 ASRPRO 串口观察。
 */
void Bsp_UartAsr_SendRaw(const uint8_t *data, uint16_t len);

/**
 * @brief 主循环轮询：取出一个已解析事件。
 *        无事件返回 0；有事件返回 1 并填充 out。
 */
uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Event_t *out);

/* 中断入口（由 py32f0xx_it.c 转发） */
void Bsp_UartAsr_UART_IRQHandler(void);
void Bsp_UartAsr_DMA_IRQHandler(void);

#endif
