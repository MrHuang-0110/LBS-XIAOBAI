#ifndef __BSP_MOTOR_H
#define __BSP_MOTOR_H
#include "py32f0xx_hal.h"

typedef enum { MOTOR_LEFT = 0, MOTOR_RIGHT = 1 } Bsp_Motor_Id_t;

void Bsp_Motor_Init(void);
void Bsp_Motor_Set(Bsp_Motor_Id_t id, int8_t speed);   /* -100..+100 */
void Bsp_Motor_StopAll(void);

#endif
