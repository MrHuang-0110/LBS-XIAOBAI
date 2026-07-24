#ifndef __BSP_BATTERY_H
#define __BSP_BATTERY_H
#include "py32f0xx_hal.h"

/*
 * 电池检测（PA0 分压，上下 100K 1:1，VREF=3.3V，DIVIDER=2）
 *
 * 抗电机干扰策略：
 *   - Bsp_Battery_Poll 每 10ms 采一次 ADC 值入 31 样本环形缓冲
 *   - Bsp_Battery_GetVoltage 返回窗口内中值转换的 mV
 *   - 31 样本 × 10ms = 310ms 观察窗，中值免疫单次 PWM 尖峰；
 *     电机启动 100~200ms 的瞬态低电压期占窗口 <50%，中位数仍取到静态值
 *   - Bsp_Battery_IsLow 带迟滞判定，跨阈值才翻转，抖动不误判
 */
#define LOW_BATTERY_ENTER_MV    3000U   /* 跌破此值 → 进入低电量态 */
#define LOW_BATTERY_EXIT_MV     3100U   /* 回升到此值以上 → 退出低电量态 */

/** 初始化：Poll 31 次填满滤波窗口（依赖 Bsp_Adc 已初始化） */
void     Bsp_Battery_Init(void);

/** 主循环调（10ms 一次即可），采一样本进环形缓冲 */
void     Bsp_Battery_Poll(void);

/** 原始 12-bit ADC 值（未滤波，调试用） */
uint16_t Bsp_Battery_ReadRaw(void);

/** 中值滤波后的电池电压 mV（31 样本中位数换算） */
uint16_t Bsp_Battery_GetVoltage(void);

/** 是否低电量（带迟滞：< 3000 进入 / > 3100 退出） */
uint8_t  Bsp_Battery_IsLow(void);

#endif
