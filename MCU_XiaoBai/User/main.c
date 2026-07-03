#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Key/Bsp_Key.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();
    if (!Bsp_Power_Init_WaitConfirm()) while (1) { }
    Bsp_LedPwm_Init();
    Bsp_LedPwm_PlayStartupBreath();
    Bsp_Key_Init();

    while (1) {
        Bsp_Key_Id_t id;
        Bsp_Key_Evt_t e = Bsp_Key_Poll(&id);
        if (e == KEY_EVT_SHORT)  Bsp_Led_Toggle(LED_MODE_POWER);
        if (e == KEY_EVT_DOUBLE) Bsp_Led_Toggle(LED_MODE_SENSOR);
        if (e == KEY_EVT_LONG)   { Bsp_Led_On(LED_MODE_REMOTE); Bsp_Tick_DelayMs(500); Bsp_Led_Off(LED_MODE_REMOTE); }
        Bsp_Tick_DelayMs(10);
    }
}

static void APP_SystemClockConfig(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSIDiv = RCC_HSI_DIV1;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) APP_ErrorHandler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) APP_ErrorHandler();
}

void APP_ErrorHandler(void) { while (1) { } }
