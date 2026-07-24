#ifndef __BSP_BATTERY_H
#define __BSP_BATTERY_H
#include "py32f0xx_hal.h"

/*
 * 电池检测（PA0 分压，上下 100K 1:1，VREF=3.3V，DIVIDER=2）
 *
 * 抗电机干扰策略：
 *   - Bsp_Battery_Poll 每 10ms 采一次 ADC 值入 15 样本环形缓冲
 *   - Bsp_Battery_GetVoltage 返回窗口内中值转换的 mV（中值免疫 PWM 尖峰）
 *   - Bsp_Battery_IsLow / CanDriveMotor 均带迟滞，跨阈值才翻转，抖动不误判
 *
 * 阈值（mV）：
 *   低电量 : 掉入 3300 / 恢复 3400（100mV 迟滞）
 *   电机 gate: 允许 3800↑ / 禁止 3900↓（100mV 迟滞，
 *              门槛 3800 = 低电阈值 3300 + 电机启动瞬态压降 500mV）
 */
#define LOW_BATTERY_ENTER_MV    3300U   /* 跌破此值 → 进入低电量态 */
#define LOW_BATTERY_EXIT_MV     3400U   /* 回升到此值以上 → 退出低电量态 */
#define MOTOR_MIN_MV            3800U   /* 静态电压 < 此值 → 拒启动电机 */
#define MOTOR_RESUME_MV         3900U   /* 静态电压回到此值以上 → 允许再启动 */

/** 初始化：Poll 15 次填满滤波窗口（依赖 Bsp_Adc 已初始化） */
void     Bsp_Battery_Init(void);

/** 主循环调（10ms 一次即可），采一样本进环形缓冲 */
void     Bsp_Battery_Poll(void);

/** 原始 12-bit ADC 值（未滤波，调试用） */
uint16_t Bsp_Battery_ReadRaw(void);

/** 中值滤波后的电池电压 mV（15 样本中位数换算） */
uint16_t Bsp_Battery_GetVoltage(void);

/** 是否低电量（带迟滞：< 3300 进入 / > 3400 退出） */
uint8_t  Bsp_Battery_IsLow(void);

/** 当前电压是否足以驱动电机（带迟滞：< 3800 禁止 / > 3900 允许） */
uint8_t  Bsp_Battery_CanDriveMotor(void);

#endif
