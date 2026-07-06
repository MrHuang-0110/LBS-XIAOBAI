#include "main.h"
#include "py32f0xx_it.h"
#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include "Bsp_UartBle/Bsp_UartBle.h"

void NMI_Handler(void)       { }
void HardFault_Handler(void) { while (1) { } }
void SVC_Handler(void)       { }
void PendSV_Handler(void)    { }
void SysTick_Handler(void)   { HAL_IncTick(); }

void USART2_IRQHandler(void)        { Bsp_UartAsr_UART_IRQHandler(); }
void DMA1_Channel1_IRQHandler(void) { Bsp_UartAsr_DMA_IRQHandler();  }

void USART1_IRQHandler(void)        { Bsp_UartBle_UART_IRQHandler(); }
void DMA1_Channel2_3_IRQHandler(void) { Bsp_UartBle_DMA_IRQHandler();  }
