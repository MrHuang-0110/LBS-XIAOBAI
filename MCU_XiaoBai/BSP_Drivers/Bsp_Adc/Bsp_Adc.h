#ifndef __BSP_ADC_H
#define __BSP_ADC_H
#include "py32f0xx_hal.h"

typedef enum {
    ADC_CH_BATTERY = 0,   /* PA0 IN0 */
    ADC_CH_IR1     = 1,   /* PA1 IN1 */
    ADC_CH_IR2     = 2,   /* PA2 IN2 */
    ADC_CH_IR3     = 3,   /* PA3 IN3 */
    ADC_CH_VREFINT = 4,   /* 内部 1.2V 基准通道（IN12），用于反算真实 VDDA */
    ADC_CH_COUNT
} Bsp_Adc_Channel_t;

/** 初始化 ADC + DMA 连续扫描 4 通道，DMA 循环写入内部缓冲 */
void     Bsp_Adc_Init(void);

/** 读取某通道最新原始 12-bit 值 */
uint16_t Bsp_Adc_Read(Bsp_Adc_Channel_t ch);

#endif
