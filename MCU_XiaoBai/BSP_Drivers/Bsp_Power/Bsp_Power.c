#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"

#define PWR_CTRL_PORT   GPIOA
#define PWR_CTRL_PIN    GPIO_PIN_15
#define KEY1_PORT       GPIOB
#define KEY1_PIN        GPIO_PIN_3

#define WAIT_TOTAL_MS   2000
#define WAIT_STEP_MS    500     /* 4 段进度 */

static void Power_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = PWR_CTRL_PIN;
    HAL_GPIO_Init(PWR_CTRL_PORT, &gi);
    HAL_GPIO_WritePin(PWR_CTRL_PORT, PWR_CTRL_PIN, GPIO_PIN_RESET);

    gi.Mode = GPIO_MODE_INPUT;   /* PB3 外部已上拉 */
    gi.Pin  = KEY1_PIN;
    HAL_GPIO_Init(KEY1_PORT, &gi);
}

uint8_t Bsp_Power_IsKey1Down(void)
{
    return HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) == GPIO_PIN_RESET ? 1 : 0;
}

uint8_t Bsp_Power_Init_WaitConfirm(void)
{
    Power_GpioInit();
    Bsp_Led_AllOff();

    uint32_t t0 = Bsp_Tick_GetMs();
    uint8_t stage = 0;

    while (1) {
        if (!Bsp_Power_IsKey1Down()) {
            Bsp_Led_AllOff();
            return 0;
        }

        uint32_t elapsed = Bsp_Tick_GetMs() - t0;

        uint8_t want_stage = (uint8_t)(elapsed / WAIT_STEP_MS);
        if (want_stage > LED_MODE_COUNT) want_stage = LED_MODE_COUNT;
        while (stage < want_stage) {
            Bsp_Led_On((Bsp_Led_Id_t)stage);
            stage++;
        }

        if (elapsed >= WAIT_TOTAL_MS) {
            HAL_GPIO_WritePin(PWR_CTRL_PORT, PWR_CTRL_PIN, GPIO_PIN_SET);
            /* 等 KEY1 稳定释放 100ms */
            uint32_t rel_t = Bsp_Tick_GetMs();
            while (1) {
                if (Bsp_Power_IsKey1Down()) rel_t = Bsp_Tick_GetMs();
                else if (Bsp_Tick_GetMs() - rel_t >= 100) break;
            }
            /* 开机成功指示：4 只 LED 快速滚动 3 圈 */
            Bsp_Led_AllOff();
            for (uint8_t round = 0; round < 3; round++) {
                for (uint8_t i = 0; i < LED_MODE_COUNT; i++) {
                    Bsp_Led_AllOff();
                    Bsp_Led_On((Bsp_Led_Id_t)i);
                    Bsp_Tick_DelayMs(80);
                }
            }
            Bsp_Led_AllOff();
            return 1;
        }
    }
}

void Bsp_Power_ShutDown(void)
{
    HAL_GPIO_WritePin(PWR_CTRL_PORT, PWR_CTRL_PIN, GPIO_PIN_RESET);
    while (1) { }
}
