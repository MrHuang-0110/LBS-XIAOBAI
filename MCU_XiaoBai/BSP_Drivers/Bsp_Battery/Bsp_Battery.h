#ifndef __BSP_BATTERY_H
#define __BSP_BATTERY_H
#include "py32f0xx_hal.h"

void     Bsp_Battery_Init(void);      /* 目前无 GPIO 需初始化，占位便于后续扩展 */
uint16_t Bsp_Battery_ReadRaw(void);   /* 12-bit 原始 ADC 值，不换算电压 */

#endif
