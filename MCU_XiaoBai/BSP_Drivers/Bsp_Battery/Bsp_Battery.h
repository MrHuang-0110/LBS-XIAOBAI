#ifndef __BSP_BATTERY_H
#define __BSP_BATTERY_H
#include "py32f0xx_hal.h"

/*
 * 电池检测（PA0 分压，上下 100K 1:1，DIVIDER=2）
 *
 * ★ 关键：MCU VDDA 由电池自身供电 → VDDA 随电池电压变化！
 *   代码假设 VREF=3.3V 会永远错。必须用内部 VREFINT (1.2V) 反算
 *   真实 VDDA 后，才能得到 PA0 的真实电压：
 *     VDDA_mV      = 1200 × 4095 / ADC_VREFINT
 *     V_battery_mV = ADC_PA0 × VDDA_mV × 2 / 4095
 *   两式合并（约去 VDDA_mV 中间量）：
 *     V_battery_mV = 1200 × 2 × ADC_PA0 / ADC_VREFINT
 *
 * 抗电机干扰策略：
 *   - Bsp_Battery_Poll 每 10ms 采一次 ADC 值入 31 样本环形缓冲（PA0 和 VREFINT 各一）
 *   - Bsp_Battery_GetVoltage 用两路窗口的中值一起换算
 *   - Bsp_Battery_IsLow 带迟滞判定，跨阈值才翻转
 */
#define LOW_BATTERY_ENTER_MV    3000U   /* 跌破此值 → 进入低电量态 */
#define LOW_BATTERY_EXIT_MV     3100U   /* 回升到此值以上 → 退出低电量态 */

/** 初始化：Poll N 次填满滤波窗口（依赖 Bsp_Adc 已初始化） */
void     Bsp_Battery_Init(void);

/** 主循环调（10ms 一次即可），采一样本进环形缓冲 */
void     Bsp_Battery_Poll(void);

/** 原始 12-bit ADC 值（未滤波，调试用；PA0 通道） */
uint16_t Bsp_Battery_ReadRaw(void);

/** 中值滤波后的电池电压 mV（由 VREFINT 反算真实 VDDA 后换算） */
uint16_t Bsp_Battery_GetVoltage(void);

/** 是否低电量（带迟滞：< 3000 进入 / > 3100 退出） */
uint8_t  Bsp_Battery_IsLow(void);

#endif
