#ifndef __BSP_TICK_H
#define __BSP_TICK_H
#include "py32f0xx_hal.h"

void     Bsp_Tick_Init(void);
uint32_t Bsp_Tick_GetMs(void);
void     Bsp_Tick_DelayMs(uint32_t ms);

#endif
