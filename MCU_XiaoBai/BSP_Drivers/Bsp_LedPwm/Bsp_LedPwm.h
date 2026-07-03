#ifndef __BSP_LEDPWM_H
#define __BSP_LEDPWM_H
#include "py32f0xx_hal.h"

typedef enum { LEDPWM_1 = 0, LEDPWM_2 = 1 } Bsp_LedPwm_Id_t;

void Bsp_LedPwm_Init(void);
void Bsp_LedPwm_Set(Bsp_LedPwm_Id_t id, uint8_t duty_percent);  /* 0..100 */
void Bsp_LedPwm_PlayStartupBreath(void);                        /* blocking ~2s */

#endif
