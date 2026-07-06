#ifndef __BSP_UART_ASR_H
#define __BSP_UART_ASR_H
#include "py32f0xx_hal.h"

/* 语音芯片交互协议 v0.3（ASCII 文本；MCU 发 `=`，收 `:`；数值全部十进制）
   详见 resource/语音芯片交互协议.md */

/* --- 语音 ID（十进制，供 Bsp_UartAsr_SendPlay 使用） --- */
#define ASR_VOICE_BOOT           1   /* 开机语 */
#define ASR_VOICE_SHUTDOWN       2   /* 关机语 */
#define ASR_VOICE_ENTER_POWER    3   /* 进入动力 */
#define ASR_VOICE_ENTER_SENSOR   4   /* 进入感应 */
#define ASR_VOICE_ENTER_REMOTE   5   /* 进入遥控 */
#define ASR_VOICE_ENTER_VOICE    6   /* 进入语音 */
#define ASR_VOICE_BLE_CONNECTED  7   /* 遥控连接 */
#define ASR_VOICE_BLE_LOST       8   /* 遥控断开 */
#define ASR_VOICE_LOW_BATTERY    9   /* 低电量 */
#define ASR_VOICE_RECEIVED       10  /* 收到 */
#define ASR_VOICE_FORWARD        11  /* 前进 */
#define ASR_VOICE_BACKWARD       12  /* 后退 */
#define ASR_VOICE_LEFT           13  /* 左转 */
#define ASR_VOICE_RIGHT          14  /* 右转 */
#define ASR_VOICE_STOP           15  /* 停止 */
#define ASR_VOICE_APPROACH_GO    20  /* 靠近启动 */
#define ASR_VOICE_OBSTACLE_STOP  21  /* 遇障停止 */
#define ASR_VOICE_WAVE_TOGGLE    22  /* 挥手开关 */
#define ASR_VOICE_BRIGHTNESS     23  /* 明暗调速 */

/* --- 命令 ID（十进制，Bsp_UartAsr_TryRecv 收到 CMD 事件时 arg = 这些值之一） --- */
#define ASR_CMD_FORWARD          30
#define ASR_CMD_BACKWARD         31
#define ASR_CMD_LEFT             32
#define ASR_CMD_RIGHT            33
#define ASR_CMD_STOP             34
#define ASR_CMD_L_FWD            40
#define ASR_CMD_L_REV            41
#define ASR_CMD_L_STOP           42
#define ASR_CMD_R_FWD            43
#define ASR_CMD_R_REV            44
#define ASR_CMD_R_STOP           45
#define ASR_CMD_ENTER_POWER      50
#define ASR_CMD_ENTER_SENSOR     51
#define ASR_CMD_ENTER_REMOTE     52
#define ASR_CMD_ENTER_VOICE      53

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

/** 初始化 USART2 (PF0/PF1, 115200 8N1) + DMA 收 + IDLE 中断 */
void Bsp_UartAsr_Init(void);

/** 发送 "play=NN\r\n" 请求播报指定语音 ID（十进制） */
void Bsp_UartAsr_SendPlay(uint8_t voice_id);

/** 发送 "stop\r\n" */
void Bsp_UartAsr_SendStop(void);

/** 发送 "ping\r\n" */
void Bsp_UartAsr_SendPing(void);

/**
 * @brief 主循环轮询：取出一个已解析事件。
 *        无事件返回 0；有事件返回 1 并填充 out。
 */
uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Event_t *out);

/* 中断入口（由 py32f0xx_it.c 转发） */
void Bsp_UartAsr_UART_IRQHandler(void);
void Bsp_UartAsr_DMA_IRQHandler(void);

#endif
