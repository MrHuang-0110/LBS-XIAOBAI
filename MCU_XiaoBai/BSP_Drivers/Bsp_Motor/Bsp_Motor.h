#ifndef __BSP_MOTOR_H
#define __BSP_MOTOR_H
#include "py32f0xx_hal.h"

typedef enum {
    MOTOR_LEFT  = 0,
    MOTOR_RIGHT = 1,
} Bsp_Motor_Id_t;

typedef enum {
    MOTOR_DIR_STOP     = 0,
    MOTOR_DIR_FORWARD  = 1,
    MOTOR_DIR_BACKWARD = 2,
} Bsp_Motor_Dir_t;

/** 初始化 TIM3 4 通道 PWM 20kHz，固定 90% duty，全部停止 */
void Bsp_Motor_Init(void);

/** 设置某电机方向（正转/反转/停止），固定 90% 占空比 */
void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir);

/** 停两个电机 */
void Bsp_Motor_StopAll(void);

#endif
