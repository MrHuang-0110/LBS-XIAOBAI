#include "Bsp.h"

void BSP_Init(void)
{
    Bsp_Tick_Init();
    Bsp_Led_Init();

    /* 电源锁存：长按 KEY1 ≥2s + 开机动画。失败则死循环等硬件掉电。 */
    if (!Bsp_Power_Init_WaitConfirm()) {
        while (1) { }
    }

    Bsp_LedPwm_Init();
    Bsp_LedPwm_PlayStartupBreath();

    Bsp_Key_Init();
    Bsp_Motor_Init();

    Bsp_Adc_Init();
    Bsp_IR_Init();
    Bsp_Battery_Init();

    Bsp_UartAsr_Init();
    Bsp_UartBle_Init();

    Bsp_Tm1640_Init();
    Bsp_Tm1640_Clear();
}
