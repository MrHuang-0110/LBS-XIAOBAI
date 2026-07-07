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

/* 速度档位（占空比）：1=30% / 2=60% / 3=100%。
   文档 §11 遥控器 3 档速度对应这三档。 */
typedef enum {
    MOTOR_SPEED_LOW  = 1,   /* 30%  */
    MOTOR_SPEED_MID  = 2,   /* 60%  */
    MOTOR_SPEED_HIGH = 3,   /* 100% */
} Bsp_Motor_Speed_t;

/** 初始化 TIM3 4 通道 PWM 20kHz，全部停止 */
void Bsp_Motor_Init(void);

/** 设置某电机方向 + 速度档位 */
void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed);

/** 停两个电机 */
void Bsp_Motor_StopAll(void);

#endif
