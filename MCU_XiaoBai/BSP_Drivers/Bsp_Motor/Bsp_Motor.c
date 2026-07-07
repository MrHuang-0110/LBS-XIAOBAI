#include "Bsp_Motor/Bsp_Motor.h"

/*
 * HAL 库版本，TIM3 4 通道 PWM，20 kHz，固定 90% duty，只支持正/反/停三种状态。
 *
 * 引脚（Datasheet V2.5 §3.1/§3.2）：
 *   PA6=CH1 (AF1) L_A   PA7=CH2 (AF1) L_B
 *   PB0=CH3 (AF1) R_A   PB1=CH4 (AF1) R_B
 *
 * 关键坑：HAL_TIM_PWM_ConfigChannel 默认打开 OCxPE (CCR preload)，之后 HAL_TIM_PWM_Start
 *   只翻 CEN 位、不生成 UG。CCMR/CCER 里的位改动没落地到工作寄存器 → PWM 永远不出。
 *   修复方案：Init 末尾主动补一次 EGR.UG=1，把配置从 preload 传到工作寄存器。
 */

#define MOTOR_TIM_PERIOD  2399U    /* 48MHz / (2400) = 20 kHz */
/* 3 档速度占空比（CCR 值）：30% / 60% / 100%
   拉开低档比例，让三档区别明显（小电机高占空比转速趋近饱和）*/
#define MOTOR_DUTY_30     720U     /* 2400 * 30% = 720   */
#define MOTOR_DUTY_60     1440U    /* 2400 * 60% = 1440  */
#define MOTOR_DUTY_100    2399U    /* 2400 * 100% = 2400，clamp 到 PERIOD */

static TIM_HandleTypeDef htim3;

/* HAL_TIM_PWM_Init 会调用此回调（SDK 例程 Bsp_Timer1_PWM.c 的标准做法）*/
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef gi = {0};
        gi.Mode      = GPIO_MODE_AF_PP;
        gi.Pull      = GPIO_NOPULL;
        gi.Speed     = GPIO_SPEED_FREQ_HIGH;
        gi.Alternate = GPIO_AF1_TIM3;   /* 4 路 TIM3 都是 AF1（datasheet 亲测）*/

        gi.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        HAL_GPIO_Init(GPIOA, &gi);

        gi.Pin = GPIO_PIN_0 | GPIO_PIN_1;
        HAL_GPIO_Init(GPIOB, &gi);
    }
}

void Bsp_Motor_Init(void)
{
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 0;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = MOTOR_TIM_PERIOD;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim3);

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

    /* 关键补丁：让 preload 中的 CCMR/CCER 值传到工作寄存器 */
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    htim3.Instance->EGR = TIM_EGR_UG;
}

static void Motor_Drive(uint32_t ch_a, uint32_t ch_b, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    /* 速度档位 -> CCR 值 */
    static const uint32_t duty_tbl[4] = {
        0,              /* 占位，speed 从 1 开始 */
        MOTOR_DUTY_30,  /* MOTOR_SPEED_LOW  = 1 */
        MOTOR_DUTY_60,  /* MOTOR_SPEED_MID  = 2 */
        MOTOR_DUTY_100, /* MOTOR_SPEED_HIGH = 3 */
    };
    uint8_t s = (uint8_t)speed;
    if (s < 1) s = 1;
    if (s > 3) s = 3;
    uint32_t duty = duty_tbl[s];

    switch (dir) {
    case MOTOR_DIR_FORWARD:
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, duty);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, 0);
        break;
    case MOTOR_DIR_BACKWARD:
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, 0);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, duty);
        break;
    case MOTOR_DIR_STOP:
    default:
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, 0);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, 0);
        break;
    }
}

void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    if (id == MOTOR_LEFT)
        Motor_Drive(TIM_CHANNEL_1, TIM_CHANNEL_2, dir, speed);
    else
        Motor_Drive(TIM_CHANNEL_3, TIM_CHANNEL_4, dir, speed);
}

void Bsp_Motor_StopAll(void)
{
    /* 停机占空比为 0，speed 参数无实际影响，给 HIGH 占位 */
    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP, MOTOR_SPEED_HIGH);
    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP, MOTOR_SPEED_HIGH);
}
