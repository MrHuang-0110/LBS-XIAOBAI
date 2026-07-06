#ifndef __BSP_IR_H
#define __BSP_IR_H
#include "py32f0xx_hal.h"

/** 初始化 PF4 = 高（三路红外发射常亮）。ADC 通道由 Bsp_Adc 提供。 */
void     Bsp_IR_Init(void);

/** 读 3 路红外反射 12-bit 原始值 */
uint16_t Bsp_IR_ReadCh1(void);   /* PA1 */
uint16_t Bsp_IR_ReadCh2(void);   /* PA2 */
uint16_t Bsp_IR_ReadCh3(void);   /* PA3 */

/** 关闭 / 打开红外发射（1=亮 0=灭），业务层暂不调用，保留扩展 */
void     Bsp_IR_Emit(uint8_t on);

#endif
