#ifndef __BSP_UART_ASR_H
#define __BSP_UART_ASR_H
#include "py32f0xx_hal.h"

/* 语音芯片交互协议 v0.1 */
#define ASR_FRAME_HEAD   0xA5U
#define ASR_FRAME_LEN    0x03U
#define ASR_FRAME_SIZE   6U

typedef struct {
    uint8_t cmd;
    uint8_t d0;
    uint8_t d1;
} Bsp_UartAsr_Frame_t;

/** 初始化 USART2 (115200 8N1) + DMA 收发 + IDLE 中断 */
void Bsp_UartAsr_Init(void);

/** 发送一帧（阻塞，超时 10ms） */
void Bsp_UartAsr_Send(uint8_t cmd, uint8_t d0, uint8_t d1);

/**
 * @brief 尝试取出一帧。有帧返回 1 并填充 out；无返回 0。
 *        供主循环轮询。
 */
uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Frame_t *out);

/* 中断入口（由 py32f0xx_it.c 转发） */
void Bsp_UartAsr_UART_IRQHandler(void);
void Bsp_UartAsr_DMA_IRQHandler(void);

#endif
