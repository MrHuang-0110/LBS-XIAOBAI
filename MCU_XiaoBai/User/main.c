#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_Motor/Bsp_Motor.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();
    if (!Bsp_Power_Init_WaitConfirm()) while (1) { }

    Bsp_Motor_Init();

    /* 非阻塞相位机 + LED3 1Hz 心跳 */
    uint32_t last_beat = Bsp_Tick_GetMs();
    uint32_t phase_t   = Bsp_Tick_GetMs();
    uint8_t  phase     = 0;

    while (1) {
        uint32_t now = Bsp_Tick_GetMs();

        if (now - last_beat >= 500) {
            last_beat = now;
            Bsp_Led_Toggle(LED_MODE_REMOTE);
        }

        uint32_t dt = now - phase_t;
        switch (phase) {
        case 0:
            Bsp_Motor_Set(MOTOR_LEFT,  +50); Bsp_Motor_Set(MOTOR_RIGHT, +50);
            if (dt >= 2000) { phase = 1; phase_t = now; }
            break;
        case 1:
            Bsp_Motor_StopAll();
            if (dt >=  500) { phase = 2; phase_t = now; }
            break;
        case 2:
            Bsp_Motor_Set(MOTOR_LEFT,  -50); Bsp_Motor_Set(MOTOR_RIGHT, -50);
            if (dt >= 2000) { phase = 3; phase_t = now; }
            break;
        case 3:
            Bsp_Motor_StopAll();
            if (dt >= 1500) { phase = 0; phase_t = now; }
            break;
        }
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
