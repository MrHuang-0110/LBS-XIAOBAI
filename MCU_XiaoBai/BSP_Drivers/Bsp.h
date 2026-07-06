#ifndef __BSP_H
#define __BSP_H
#include "py32f0xx_hal.h"

#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Key/Bsp_Key.h"
#include "Bsp_Motor/Bsp_Motor.h"
#include "Bsp_Adc/Bsp_Adc.h"
#include "Bsp_IR/Bsp_IR.h"
#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include "Bsp_UartBle/Bsp_UartBle.h"
#include "Bsp_Tm1640/Bsp_Tm1640.h"

/**
 * @brief 汇总初始化所有 BSP 模块。
 *        内部顺序：Tick → Led → Power(2s确认+开机动画，失败死循环) →
 *        LedPwm(呼吸) → Key → Motor → Adc → IR → Battery →
 *        UartAsr → UartBle → Tm1640(清屏)。
 *        调用前必须已执行 HAL_Init() + 系统时钟配置。
 *        电源锁存失败时内部 while(1){} 不返回。
 */
void BSP_Init(void);

#endif
