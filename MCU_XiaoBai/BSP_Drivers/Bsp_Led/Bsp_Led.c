#include "Bsp_Led/Bsp_Led.h"

typedef struct { GPIO_TypeDef *port; uint16_t pin; } LedPin_t;

static const LedPin_t g_leds[LED_MODE_COUNT] = {
    { GPIOB, GPIO_PIN_2  },  /* LED1 PB2  */
    { GPIOA, GPIO_PIN_10 },  /* LED2 PA10 */
    { GPIOA, GPIO_PIN_11 },  /* LED3 PA11 */
    { GPIOA, GPIO_PIN_12 },  /* LED4 PA12 */
};

void Bsp_Led_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    gi.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &gi);
    gi.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOB, &gi);

    Bsp_Led_AllOff();
}

void Bsp_Led_On(Bsp_Led_Id_t id)
{
    if (id >= LED_MODE_COUNT) return;
    HAL_GPIO_WritePin(g_leds[id].port, g_leds[id].pin, GPIO_PIN_RESET);
}

void Bsp_Led_Off(Bsp_Led_Id_t id)
{
    if (id >= LED_MODE_COUNT) return;
    HAL_GPIO_WritePin(g_leds[id].port, g_leds[id].pin, GPIO_PIN_SET);
}

void Bsp_Led_Toggle(Bsp_Led_Id_t id)
{
    if (id >= LED_MODE_COUNT) return;
    HAL_GPIO_TogglePin(g_leds[id].port, g_leds[id].pin);
}

void Bsp_Led_AllOff(void)
{
    for (int i = 0; i < LED_MODE_COUNT; i++) Bsp_Led_Off((Bsp_Led_Id_t)i);
}
