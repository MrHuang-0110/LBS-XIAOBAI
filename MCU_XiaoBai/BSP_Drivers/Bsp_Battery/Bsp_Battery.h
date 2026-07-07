#ifndef __BSP_BATTERY_H
#define __BSP_BATTERY_H
#include "py32f0xx_hal.h"

/* 硬件：PA0 分压检测电池电压，上下分压电阻各 100K（1:1），VREF=3.3V */
#define LOW_BATTERY_MV   3300U   /* 低电量阈值 3.3V（3节1.5V锂电池每节1.1V）*/

void     Bsp_Battery_Init(void);      /* 占位，ADC 已由 Bsp_Adc 管理 */
uint16_t Bsp_Battery_ReadRaw(void);   /* 12-bit 原始 ADC 值 */
uint16_t Bsp_Battery_ReadVoltage(void);  /* 返回电池电压 mV */
uint8_t  Bsp_Battery_IsLow(void);       /* 1=低电量（< 3.3V）*/

#endif
