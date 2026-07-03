#include "Bsp_Motor/Bsp_Motor.h"

/*
 * TIM3 clock = 48MHz. Prescaler=0, Period=2399 -> 20 kHz.
 * PA6=CH1(AF1)  PA7=CH2(AF1)  PB0=CH3(AF1)  PB1=CH4(AF1)
 * AF 号来自 PY32F030 Datasheet V2.5 Section 3.1/3.2（Port A/B multiplexing）。
 * 注：SDK 里的 GPIO_AF13_TIM3 宏在 K28 (LQFP32/QFN32) 上不适用，四路都用 AF1。
 */
#define MOTOR_TIM_PERIOD  2399U

static TIM_HandleTypeDef htim3;

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef gi = {0};
        gi.Mode  = GPIO_MODE_AF_PP;
        gi.Pull  = GPIO_NOPULL;
        gi.Speed = GPIO_SPEED_FREQ_HIGH;
        gi.Alternate = GPIO_AF1_TIM3;

        /* PA6/PA7 -> TIM3 CH1/CH2 */
        gi.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        HAL_GPIO_Init(GPIOA, &gi);

        /* PB0/PB1 -> TIM3 CH3/CH4 */
        gi.Pin = GPIO_PIN_0 | GPIO_PIN_1;
        HAL_GPIO_Init(GPIOB, &gi);
    }
}

void Bsp_Motor_Init(void)
{
    htim3.Instance = TIM3;
    htim3.Init.Prescaler         = 0;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = MOTOR_TIM_PERIOD;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim3);        /* -> HAL_TIM_PWM_MspInit 配 GPIO/时钟 */

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_4);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
}

static void Motor_WritePair(uint32_t ch_a, uint32_t ch_b, int8_t speed)
{
    uint32_t abs_s = (uint32_t)(speed >= 0 ? speed : -speed);
    uint32_t duty  = abs_s * (MOTOR_TIM_PERIOD + 1) / 100U;
    if (duty > MOTOR_TIM_PERIOD) duty = MOTOR_TIM_PERIOD;

    if (speed > 0) {
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, duty);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, 0);
    } else if (speed < 0) {
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, 0);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, duty);
    } else {
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, 0);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, 0);
    }
}

void Bsp_Motor_Set(Bsp_Motor_Id_t id, int8_t speed)
{
    if (speed >  100) speed =  100;
    if (speed < -100) speed = -100;

    if (id == MOTOR_LEFT) Motor_WritePair(TIM_CHANNEL_1, TIM_CHANNEL_2, speed);
    else                  Motor_WritePair(TIM_CHANNEL_3, TIM_CHANNEL_4, speed);
}

void Bsp_Motor_StopAll(void)
{
    Bsp_Motor_Set(MOTOR_LEFT,  0);
    Bsp_Motor_Set(MOTOR_RIGHT, 0);
}
