#ifndef __BSP_LED_H
#define __BSP_LED_H
#include "py32f0xx_hal.h"

typedef enum {
    LED_MODE_POWER  = 0,  /* LED1 - 动力 - PB2  */
    LED_MODE_SENSOR = 1,  /* LED2 - 感应 - PA10 */
    LED_MODE_REMOTE = 2,  /* LED3 - 遥控 - PA11 */
    LED_MODE_VOICE  = 3,  /* LED4 - 语音 - PA12 */
    LED_MODE_COUNT
} Bsp_Led_Id_t;

void Bsp_Led_Init(void);
void Bsp_Led_On(Bsp_Led_Id_t id);
void Bsp_Led_Off(Bsp_Led_Id_t id);
void Bsp_Led_Toggle(Bsp_Led_Id_t id);
void Bsp_Led_AllOff(void);

#endif
