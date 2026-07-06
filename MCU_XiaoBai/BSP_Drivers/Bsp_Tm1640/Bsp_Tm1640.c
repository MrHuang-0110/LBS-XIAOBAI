#include "Bsp_Tm1640/Bsp_Tm1640.h"

#define CLK_PORT  GPIOA
#define CLK_PIN   GPIO_PIN_4
#define DIN_PORT  GPIOA
#define DIN_PIN   GPIO_PIN_5

#define CLK_H()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_SET)
#define CLK_L()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_RESET)
#define DIN_H()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_SET)
#define DIN_L()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_RESET)

/* 时序延时：datasheet 第 4 页要求 PWCLK≥400ns, tWAIT≥1µs, tSETUP/tHOLD≥100ns。
   48MHz 主频下 20 次 NOP + 循环开销约 1-2µs，留足裕量。 */
static void Delay_Bit(void) { for (volatile int i = 0; i < 30; i++) __NOP(); }

/* Start：CLK 高时 DIN 由高变低（datasheet 第 4 页）。
   进入时 CLK/DIN 状态任意，先规整到 CLK高/DIN高 再产生下降沿。 */
static void tm_Start(void)
{
    CLK_H(); DIN_H(); Delay_Bit();
    DIN_L(); Delay_Bit();   /* CLK 高时 DIN 由高变低 = Start */
    CLK_L(); Delay_Bit();   /* 拉低 CLK 准备传第一位 */
}

/* Stop：CLK 高时 DIN 由低变高。调用前 WriteByte 结束时 CLK 已高。 */
static void tm_Stop(void)
{
    CLK_L(); Delay_Bit();       /* 先拉低 CLK，准备让 DIN 进入已知态 */
    DIN_L(); Delay_Bit();
    CLK_H(); Delay_Bit();       /* CLK 升高，此时 DIN=低 */
    DIN_H(); Delay_Bit();       /* CLK 高时 DIN 由低变高 = Stop */
}

static void tm_WriteByte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        /* CLK 低时改变 DIN（datasheet：CLK 低电平时 DIN 才能变） */
        CLK_L();
        if (b & 0x01) DIN_H(); else DIN_L();
        Delay_Bit();            /* tSETUP ≥100ns */
        CLK_H();                /* CLK 上升沿，芯片采样 DIN */
        Delay_Bit();            /* tWAIT ≥1µs, PWCLK ≥400ns */
        b >>= 1;
    }
    /* 结束时 CLK 为高，DIN 为最后一位 */
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
    /* TM1640 显示控制命令（datasheet 第 7 页）：
       B7B6=10，B5=1 开显示 / B5=0 关显示，低 6 位是亮度编码。
       注意：不是 0x88|level！0x88 的 B5=0 是关显示，灯不亮。
       亮度档（开显示）：1/16=0xA0, 2/16=0xA4, 4/16=0xAA, 10/16=0xAB,
       11/16=0xAC, 12/16=0xAD, 13/16=0xAE, 14/16=0xAF（最亮）*/
    static const uint8_t k_bright[8] = {0xA0, 0xA4, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    if (level > 7) level = 7;
    tm_Start(); tm_WriteByte(k_bright[level]); tm_Stop();
}
