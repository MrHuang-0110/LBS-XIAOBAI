#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_Adc/Bsp_Adc.h"

/* 换算：
   PA0 电压 = 电池电压 / 2（两个 100K 分压，1:1）
   ADC 12位（ADC_MAX=4095）
   VDDA 未知 → 用内部 VREFINT (1.2V 基准) 反算：
     VDDA_mV      = VREFINT_MV × ADC_MAX / ADC_VREFINT
     V_battery_mV = ADC_PA0 × VDDA_mV × DIVIDER / ADC_MAX
   两式合并（消去 VDDA_mV 中间量、约去 ADC_MAX）：
     V_battery_mV = VREFINT_MV × DIVIDER × ADC_PA0 / ADC_VREFINT
                  = 2400 × ADC_PA0 / ADC_VREFINT
   注意：PY32 手册给的是固定值 1200±10mV，不像 STM32L 有出厂校准地址。 */

#define VREFINT_MV      1200U   /* PY32F030 VREFINT 名义值，±10mV 精度 */
#define DIVIDER_RATIO   2U      /* 分压比 2 倍（1:1 分压）*/

/* 中值滤波窗口：31 个样本 @ 10ms 采样 = 310ms 观察窗；
   奇数长度取中位数，对 PWM 脉冲式尖峰完全免疫（不像均值会被拉偏）。
   PA0（电池分压）和 VREFINT 各一份，独立滤波。 */
#define FILTER_N        31U

static uint16_t g_bat_samples[FILTER_N];
static uint16_t g_ref_samples[FILTER_N];
static uint8_t  g_head = 0;

/* 迟滞状态（模块内保持，跨阈值才翻转） */
static uint8_t g_is_low = 0;    /* 1 = 当前处于低电量态 */

/* 31 样本插入排序取中值。样本量小，O(N^2) 完全够（~465 次比较，PY32 上 <1ms） */
static uint16_t Median(const uint16_t *src)
{
    uint16_t a[FILTER_N];
    for (uint8_t i = 0; i < FILTER_N; i++) a[i] = src[i];
    for (uint8_t i = 1; i < FILTER_N; i++) {
        uint16_t v = a[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
    return a[FILTER_N / 2];   /* index 15 = 中位数 */
}

void Bsp_Battery_Init(void)
{
    /* 冷启动：立即连采 N 次填满两个窗口，保证首次 GetVoltage 就有效。
       ADC 已由 Bsp_Adc 循环 DMA 持续更新 g_val，此处只是从中拷贝快照。
       每次采样间用短空转拉时间，让 DMA 有机会走过至少一轮扫描。 */
    for (uint8_t i = 0; i < FILTER_N; i++) {
        g_bat_samples[i] = Bsp_Adc_Read(ADC_CH_BATTERY);
        g_ref_samples[i] = Bsp_Adc_Read(ADC_CH_VREFINT);
        for (volatile uint32_t d = 0; d < 500; d++) { /* 空转 ~50µs */ }
    }
    g_head = 0;

    /* 依据初始中值决定 is_low 初值 */
    g_is_low = (Bsp_Battery_GetVoltage() < LOW_BATTERY_ENTER_MV) ? 1 : 0;
}

void Bsp_Battery_Poll(void)
{
    g_bat_samples[g_head] = Bsp_Adc_Read(ADC_CH_BATTERY);
    g_ref_samples[g_head] = Bsp_Adc_Read(ADC_CH_VREFINT);
    g_head = (uint8_t)((g_head + 1) % FILTER_N);
}

uint16_t Bsp_Battery_ReadRaw(void)
{
    return Bsp_Adc_Read(ADC_CH_BATTERY);
}

uint16_t Bsp_Battery_GetVoltage(void)
{
    uint16_t adc_bat = Median(g_bat_samples);
    uint16_t adc_ref = Median(g_ref_samples);
    if (adc_ref == 0) return 0;   /* 极端保护，防除零 */
    /* V_bat = VREFINT_MV × DIVIDER × ADC_PA0 / ADC_VREFINT
       中间乘积上限：1200 × 2 × 4095 = 9,828,000 → uint32_t 完全够 */
    return (uint16_t)((uint32_t)VREFINT_MV * DIVIDER_RATIO * adc_bat / adc_ref);
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
