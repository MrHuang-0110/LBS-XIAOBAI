#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_Adc/Bsp_Adc.h"

/* 分压计算：
   PA0 电压 = 电池电压 / 2（两个 100K 分压，1:1）
   ADC 12位，VREF = 3.3V
   V_bat(mV) = ADC / 4095 * 3300 * 2 = ADC * 6600 / 4095 */

#define VREF_MV         3300U
#define ADC_MAX         4095U
#define DIVIDER_RATIO   2U      /* 分压比 2 倍（1:1 分压）*/

/* 中值滤波窗口：15 个样本 @ 10ms 采样 = 150ms 观察窗；
   奇数长度取中位数，对 PWM 脉冲式尖峰完全免疫（不像均值会被拉偏）。 */
#define FILTER_N        15U

static uint16_t g_samples[FILTER_N];
static uint8_t  g_head = 0;

/* 迟滞状态（模块内保持，跨阈值才翻转） */
static uint8_t g_is_low = 0;    /* 1 = 当前处于低电量态 */

static uint16_t Adc_To_Mv(uint16_t adc)
{
    return (uint16_t)((uint32_t)adc * VREF_MV * DIVIDER_RATIO / ADC_MAX);
}

/* 15 样本插入排序取中值。样本量小，O(N^2) 完全够（~50 次比较） */
static uint16_t Median15(const uint16_t *src)
{
    uint16_t a[FILTER_N];
    for (uint8_t i = 0; i < FILTER_N; i++) a[i] = src[i];
    for (uint8_t i = 1; i < FILTER_N; i++) {
        uint16_t v = a[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
    return a[FILTER_N / 2];   /* index 7 = 中位数 */
}

void Bsp_Battery_Init(void)
{
    /* 冷启动：立即连采 N 次填满窗口，保证首次 GetVoltage 就有效。
       ADC 已由 Bsp_Adc 循环 DMA 持续更新 g_val，此处只是从中拷贝快照。
       每次采样间用短空转拉时间，让 DMA 有机会走过至少一轮扫描。 */
    for (uint8_t i = 0; i < FILTER_N; i++) {
        g_samples[i] = Bsp_Adc_Read(ADC_CH_BATTERY);
        for (volatile uint32_t d = 0; d < 500; d++) { /* 空转 ~50µs */ }
    }
    g_head = 0;

    /* 依据初始中值决定 is_low 初值 */
    g_is_low = (Bsp_Battery_GetVoltage() < LOW_BATTERY_ENTER_MV) ? 1 : 0;
}

void Bsp_Battery_Poll(void)
{
    g_samples[g_head] = Bsp_Adc_Read(ADC_CH_BATTERY);
    g_head = (uint8_t)((g_head + 1) % FILTER_N);
}

uint16_t Bsp_Battery_ReadRaw(void)
{
    return Bsp_Adc_Read(ADC_CH_BATTERY);
}

uint16_t Bsp_Battery_GetVoltage(void)
{
    return Adc_To_Mv(Median15(g_samples));
}

uint8_t Bsp_Battery_IsLow(void)
{
    uint16_t v = Bsp_Battery_GetVoltage();
    /* 迟滞：低态下要爬回 EXIT_MV 才退出，非低态下要跌破 ENTER_MV 才进入 */
    if (g_is_low) {
        if (v > LOW_BATTERY_EXIT_MV) g_is_low = 0;
    } else {
        if (v < LOW_BATTERY_ENTER_MV) g_is_low = 1;
    }
    return g_is_low;
}
