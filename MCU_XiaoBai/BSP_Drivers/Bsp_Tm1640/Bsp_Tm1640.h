#ifndef __BSP_TM1640_H
#define __BSP_TM1640_H
#include "py32f0xx_hal.h"

#define TM1640_COLS  14        /* 我们只用 14 列 */
#define TM1640_ROWS  8

/** 初始化 GPIO + 清屏 + 打开显示 */
void Bsp_Tm1640_Init(void);

/** 全屏刷新，data[0..13] = 14 列，每列 bit0..bit7 = 8 行像素 */
void Bsp_Tm1640_Refresh(const uint8_t data[TM1640_COLS]);

/** 清屏 */
void Bsp_Tm1640_Clear(void);

/** 显示亮度 0..7；0=最暗、7=最亮 */
void Bsp_Tm1640_SetBrightness(uint8_t level);

#endif
