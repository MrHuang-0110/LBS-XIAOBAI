#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_Adc/Bsp_Adc.h"

void     Bsp_Battery_Init(void)      { /* ADC 已由 Bsp_Adc 管理 */ }
uint16_t Bsp_Battery_ReadRaw(void)   { return Bsp_Adc_Read(ADC_CH_BATTERY); }
