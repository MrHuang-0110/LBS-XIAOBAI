#include "Bsp_Tm1640/Bsp_Tm1640.h"

/*
 * TM1640 8×14 点阵驱动，软件位翻转。
 * PA4=CLK, PA5=DIN，两线制（非 I2C，无 ACK）。
 * 时序参照实战验证过的参考驱动（E:/LBS-SPARK-AI/application/matrix/tm1640.c）。
 *
 * 命令字（标准 TM1640 编码）：
 *   数据命令 0x40 = 自动地址加1
 *   地址命令 0xC0 | addr(0..15)
 *   显示命令 0x88 | level(0..7) = 开显示 + 亮度；0x80 = 关显示
 *     注意：B3=1 是开显示（不是 B5），参考驱动实测验证。
 */

#define CLK_PORT  GPIOA
#define CLK_PIN   GPIO_PIN_4
#define DIN_PORT  GPIOA
#define DIN_PIN   GPIO_PIN_5

#define CLK_H()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_SET)
#define CLK_L()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_RESET)
#define DIN_H()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_SET)
#define DIN_L()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_RESET)

/* 微秒级延时。48MHz 下每次循环约 0.5µs，n 次约 n/2 µs。
   参考驱动用 delay_us(10)，我们用循环 20 次约 10µs 量级，留足裕量。 */
static void Delay_us(uint32_t us)
{
    for (volatile uint32_t i = 0; i < us * 8; i++) __NOP();
}

/* 起始条件：CLK 高时 DIN 由高变低。
   参考驱动写法：先 CLK低/DIN高，再 CLK高，延时，DIN低，延时，CLK低。 */
static void tm_Start(void)
{
    CLK_L();
    DIN_H();
    CLK_H();
    Delay_us(1);
    DIN_L();          /* CLK 高时 DIN 由高变低 = Start */
    Delay_us(1);
    CLK_L();
    Delay_us(1);
}

/* 停止条件：CLK 高时 DIN 由低变高。
   参考驱动写法：CLK低/DIN低，CLK高，延时，DIN高，延时。 */
static void tm_Stop(void)
{
    CLK_L();
    DIN_L();
    CLK_H();
    Delay_us(1);
    DIN_H();          /* CLK 高时 DIN 由低变高 = Stop */
    Delay_us(1);
}

/* 写一个字节，LSB first。
   参考驱动：CLK低 -> 设 DIN -> 延时 10us -> CLK高（上升沿锁存）-> 下一位。
   结束后整个字节发完再延时 10us。 */
static void tm_WriteByte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        CLK_L();
        if (data & 0x01) DIN_H(); else DIN_L();
        Delay_us(10);          /* tSETUP */
        CLK_H();               /* 上升沿锁存 */
        data >>= 1;
    }
    Delay_us(10);
}

static void tm_SendCommand(uint8_t cmd)
{
    tm_Start();
    tm_WriteByte(cmd);
    tm_Stop();
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
    CLK_H(); DIN_H();   /* 空闲状态 */
}

void Bsp_Tm1640_Init(void)
{
    Tm_GpioInit();
    /* 先清显存（datasheet 建议上电后清一次避免乱码） */
    Bsp_Tm1640_Clear();
    /* 开显示 + 最亮 */
    Bsp_Tm1640_SetBrightness(7);
}

void Bsp_Tm1640_Refresh(const uint8_t data[TM1640_COLS])
{
    /* 1. 数据命令：自动地址加1 */
    tm_SendCommand(0x40);
    /* 2. 起始地址 0，连写 16 字节（14 有效 + 2 补 0） */
    tm_Start();
    tm_WriteByte(0xC0);
    for (int i = 0; i < TM1640_COLS; i++) tm_WriteByte(data[i]);
    tm_WriteByte(0x00);
    tm_WriteByte(0x00);
    tm_Stop();
}

void Bsp_Tm1640_Clear(void)
{
    uint8_t z[TM1640_COLS] = {0};
    Bsp_Tm1640_Refresh(z);
}

void Bsp_Tm1640_SetBrightness(uint8_t level)
{
    /* 标准编码：0x88 | level = 开显示 + 亮度。
       0x88=开+最暗(1/16) ... 0x8F=开+最亮(14/16)。
       参考驱动实测 B3=1 是开显示位。 */
    if (level > 7) level = 7;
    tm_SendCommand((uint8_t)(0x88 | level));
}
