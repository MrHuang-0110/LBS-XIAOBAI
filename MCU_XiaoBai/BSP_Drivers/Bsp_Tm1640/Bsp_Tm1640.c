#include "Bsp_Tm1640/Bsp_Tm1640.h"

#define CLK_PORT  GPIOA
#define CLK_PIN   GPIO_PIN_4
#define DIN_PORT  GPIOA
#define DIN_PIN   GPIO_PIN_5

#define CLK_H()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_SET)
#define CLK_L()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_RESET)
#define DIN_H()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_SET)
#define DIN_L()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_RESET)

static void Delay_Bit(void) { for (volatile int i = 0; i < 20; i++) __NOP(); }

static void tm_Start(void) { CLK_H(); DIN_H(); Delay_Bit(); DIN_L(); Delay_Bit(); CLK_L(); }
static void tm_Stop(void)  { CLK_L(); DIN_L(); Delay_Bit(); CLK_H(); Delay_Bit(); DIN_H(); }

static void tm_WriteByte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        CLK_L();
        if (b & 0x01) DIN_H(); else DIN_L();
        Delay_Bit();
        CLK_H();
        Delay_Bit();
        b >>= 1;
    }
}

static void Tm_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    gi.Pin   = CLK_PIN | DIN_PIN;
    HAL_GPIO_Init(GPIOA, &gi);
    CLK_H(); DIN_H();
}

void Bsp_Tm1640_Init(void)
{
    Tm_GpioInit();
    Bsp_Tm1640_Clear();
    Bsp_Tm1640_SetBrightness(7);
}

void Bsp_Tm1640_Refresh(const uint8_t data[TM1640_COLS])
{
    /* 1. 数据命令：自增 */
    tm_Start(); tm_WriteByte(0x40); tm_Stop();
    /* 2. 地址 0 起，连写 16 字节（14 有效 + 2 补 0） */
    tm_Start();
    tm_WriteByte(0xC0);
    for (int i = 0; i < TM1640_COLS; i++) tm_WriteByte(data[i]);
    tm_WriteByte(0x00); tm_WriteByte(0x00);
    tm_Stop();
}

void Bsp_Tm1640_Clear(void)
{
    uint8_t z[TM1640_COLS] = {0};
    Bsp_Tm1640_Refresh(z);
}

void Bsp_Tm1640_SetBrightness(uint8_t level)
{
    if (level > 7) level = 7;
    tm_Start(); tm_WriteByte((uint8_t)(0x88 | level)); tm_Stop();
}
