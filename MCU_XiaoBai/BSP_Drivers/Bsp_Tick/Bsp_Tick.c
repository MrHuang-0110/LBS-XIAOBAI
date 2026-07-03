#include "Bsp_Tick/Bsp_Tick.h"

void     Bsp_Tick_Init(void)          { /* HAL_Init 已配置 SysTick 1kHz */ }
uint32_t Bsp_Tick_GetMs(void)         { return HAL_GetTick(); }
void     Bsp_Tick_DelayMs(uint32_t ms){ HAL_Delay(ms); }
