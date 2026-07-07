#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_Adc/Bsp_Adc.h"

/* 分压计算：
   PA0 电压 = 电池电压 / 2（两个 100K 分压，1:1）
   ADC 12位，VREF = 3.3V
   V_bat(mV) = ADC / 4095 * 3300 * 2 = ADC * 6600 / 4095 ≈ ADC * 1.612 */

#define VREF_MV         3300U
#define ADC_MAX         4095U
#define DIVIDER_RATIO   2U      /* 分压比 2 倍（1:1 分压）*/

void     Bsp_Battery_Init(void)      { /* ADC 已由 Bsp_Adc 管理 */ }

uint16_t Bsp_Battery_ReadRaw(void)   { return Bsp_Adc_Read(ADC_CH_BATTERY); }

uint16_t Bsp_Battery_ReadVoltage(void)
{
    uint16_t adc = Bsp_Adc_Read(ADC_CH_BATTERY);
    return (uint16_t)((uint32_t)adc * VREF_MV * DIVIDER_RATIO / ADC_MAX);
}

uint8_t  Bsp_Battery_IsLow(void)
{
    return Bsp_Battery_ReadVoltage() < LOW_BATTERY_MV ? 1 : 0;
}
