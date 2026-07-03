#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Key/Bsp_Key.h"

/*
 * 临时最小模式状态机（未来 M2 会抽到独立文件）：
 *   - 开机默认语音模式，LED4 常亮
 *   - 短按 KEY1：循环 语音 → 动力 → 感应 → 遥控 → 语音（按 LED 序号 4→1→2→3→4 循环）
 *   - 双击 KEY1：忽略（本项目已取消双击）
 *   - 长按 KEY1：预留给关机；本 demo 里点 LED3 500ms 做调试提示
 *
 * LED-模式映射（对齐文档 §6）：LED1=动力 / LED2=感应 / LED3=遥控 / LED4=语音
 */

typedef enum {
    APP_MODE_VOICE   = 0,  /* 默认 */
    APP_MODE_POWER   = 1,  /* 动力 */
    APP_MODE_SENSOR  = 2,  /* 感应 */
    APP_MODE_REMOTE  = 3,  /* 遥控 */
    APP_MODE_COUNT
} App_Mode_t;

static const Bsp_Led_Id_t g_mode_led[APP_MODE_COUNT] = {
    LED_MODE_VOICE,    /* 语音 → LED4 */
    LED_MODE_POWER,    /* 动力 → LED1 */
    LED_MODE_SENSOR,   /* 感应 → LED2 */
    LED_MODE_REMOTE,   /* 遥控 → LED3 */
};

static void Show_Mode(App_Mode_t m)
{
    Bsp_Led_AllOff();
    Bsp_Led_On(g_mode_led[m]);
}

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

    App_Mode_t mode = APP_MODE_VOICE;
    Show_Mode(mode);

    while (1) {
        Bsp_Key_Id_t id;
        Bsp_Key_Evt_t e = Bsp_Key_Poll(&id);

        if (e == KEY_EVT_SHORT && id == KEY_ID_1) {
            mode = (App_Mode_t)((mode + 1) % APP_MODE_COUNT);
            Show_Mode(mode);
        }
        else if (e == KEY_EVT_LONG && id == KEY_ID_1) {
            /* 未来 Task 14 会在这里关机；本 demo 只闪一下 LED3 做提示 */
            Bsp_Led_On(LED_MODE_REMOTE);
            Bsp_Tick_DelayMs(500);
            Show_Mode(mode);
        }
        /* 忽略 KEY2/3/4 事件（保留以后接入） */

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
