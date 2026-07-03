#ifndef __BSP_UART_ASR_H
#define __BSP_UART_ASR_H
#include "py32f0xx_hal.h"

/* 语音芯片交互协议 v0.2（ASCII 文本，\r\n 分帧）
   详见 resource/语音芯片交互协议.md */

typedef enum {
    ASR_EVT_NONE = 0,
    ASR_EVT_CMD  = 1,   /* ASRPRO 识别到语音命令，arg = 命令 ID (0x30..0x71) */
    ASR_EVT_WAKE = 2,   /* ASRPRO 检测到唤醒词，arg 无意义 */
    ASR_EVT_DONE = 3,   /* ASRPRO 播报完成，arg = 上次的语音 ID */
} Bsp_UartAsr_EvtType_t;

typedef struct {
    Bsp_UartAsr_EvtType_t type;
    uint8_t               arg;
} Bsp_UartAsr_Event_t;

/** 初始化 USART2 (PF0/PF1, 115200 8N1) + DMA 收 + IDLE 中断 */
void Bsp_UartAsr_Init(void);

/** 发送 "play:XX\r\n" 请求播报指定语音 ID */
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
