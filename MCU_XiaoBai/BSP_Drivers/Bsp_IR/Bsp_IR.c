#include "Bsp_IR/Bsp_IR.h"
#include "Bsp_Adc/Bsp_Adc.h"

#define IR_PORT   GPIOF
#define IR_PIN    GPIO_PIN_4

void Bsp_IR_Init(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = IR_PIN;
    HAL_GPIO_Init(IR_PORT, &gi);
    HAL_GPIO_WritePin(IR_PORT, IR_PIN, GPIO_PIN_SET);   /* 上电常亮 */
}

void Bsp_IR_Emit(uint8_t on)
{
    HAL_GPIO_WritePin(IR_PORT, IR_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint16_t Bsp_IR_ReadCh1(void) { return Bsp_Adc_Read(ADC_CH_IR1); }
uint16_t Bsp_IR_ReadCh2(void) { return Bsp_Adc_Read(ADC_CH_IR2); }
uint16_t Bsp_IR_ReadCh3(void) { return Bsp_Adc_Read(ADC_CH_IR3); }
