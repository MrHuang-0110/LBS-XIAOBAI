#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Tick/Bsp_Tick.h"

/* TIM1 clk=48MHz. Prescaler=47 -> 1MHz cnt, Period=999 -> 1kHz.
 * PA8=CH1(AF2)  PA9=CH2(AF2)
 */
#define LEDPWM_TIM_PERIOD  999U

static TIM_HandleTypeDef htim1;

static void LedPwm_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF2_TIM1;
    gi.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOA, &gi);
}

void Bsp_LedPwm_Init(void)
{
    LedPwm_GpioInit();
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance = TIM1;
    htim1.Init.Prescaler         = 47;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = LEDPWM_TIM_PERIOD;
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.Pulse        = 0;
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);

    /* 关键：HAL_TIM_PWM_ConfigChannel 打开 OCxPE 但 Start 不触发 UG，
     * 必须手动补一次 UG 让 CCMR/CCER 更新从 preload 传到工作寄存器,
     * 否则 PWM 永远不上引脚。详见 Task 5 修复过程。 */
    htim1.Instance->EGR = TIM_EGR_UG;
}

void Bsp_LedPwm_Set(Bsp_LedPwm_Id_t id, uint8_t duty_percent)
{
    if (duty_percent > 100) duty_percent = 100;
    uint32_t duty = (uint32_t)duty_percent * (LEDPWM_TIM_PERIOD + 1) / 100U;
    if (duty > LEDPWM_TIM_PERIOD) duty = LEDPWM_TIM_PERIOD;
    __HAL_TIM_SET_COMPARE(&htim1,
        (id == LEDPWM_1) ? TIM_CHANNEL_1 : TIM_CHANNEL_2,
        duty);
}

void Bsp_LedPwm_PlayStartupBreath(void)
{
    for (int p = 0; p <= 100; p += 2) {
        Bsp_LedPwm_Set(LEDPWM_1, (uint8_t)p);
        Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)p);
        Bsp_Tick_DelayMs(20);   /* 50 * 20ms = 1s */
    }
    for (int p = 100; p >= 0; p -= 2) {
        Bsp_LedPwm_Set(LEDPWM_1, (uint8_t)p);
        Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)p);
        Bsp_Tick_DelayMs(20);
    }
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
}
