#ifndef __BSP_UART_BLE_H
#define __BSP_UART_BLE_H
#include "py32f0xx_hal.h"

#define BLE_RX_BUF_SIZE   128U

/** 初始化 USART1 115200 8N1 + DMA 收 + IDLE，PF3 输入（BLE_STA） */
void Bsp_UartBle_Init(void);

/** 发送若干字节（阻塞，超时 20ms） */
void Bsp_UartBle_Send(const uint8_t *data, uint16_t len);

/**
 * @brief 读一段收到的数据（尽可能多），复制到 out_buf。
 * @return 实际拷贝的字节数（0 表示无数据）。
 */
uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len);

/** PF3 BLE 连接状态：1 = 已连接（视电路极性，若相反在此取反） */
uint8_t  Bsp_UartBle_IsConnected(void);

void Bsp_UartBle_UART_IRQHandler(void);
void Bsp_UartBle_DMA_IRQHandler(void);

#endif
