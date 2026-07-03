#ifndef __BSP_POWER_H
#define __BSP_POWER_H
#include "py32f0xx_hal.h"

/**
 * @brief 上电确认 + 电源锁存。阻塞最多 2000ms。
 *        返回 1 = 锁存成功；返回 0 = 用户提前松手，调用方应死循环等硬件掉电。
 *        依赖 Bsp_Led_Init / Bsp_Tick_Init 已完成。
 */
uint8_t Bsp_Power_Init_WaitConfirm(void);

/** 主动关机：PA15 = 0，不返回 */
void    Bsp_Power_ShutDown(void);

/** 读 KEY1(PB3)：按下 = 1，未按 = 0 */
uint8_t Bsp_Power_IsKey1Down(void);

#endif
