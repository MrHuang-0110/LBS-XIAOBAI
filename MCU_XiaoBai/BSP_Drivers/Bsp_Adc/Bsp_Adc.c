#include "Bsp_Adc/Bsp_Adc.h"
#include "py32f0xx_ll_system.h"   /* LL_SYSCFG_SetDMARemap_CH3 */

/*
 * ADC1 4 通道扫描 + DMA1_Channel3 循环搬运
 *   PA0=CH0(电池)  PA1=CH1(IR1)  PA2=CH2(IR2)  PA3=CH3(IR3)
 *
 * DMA 通道分配（全片 3 个 channel）：
 *   Task 8 USART2_RX -> DMA1_Channel1（HAL_SYSCFG_DMA_Req(0x08)）
 *   Task 9 USART1_RX -> DMA1_Channel2（LL_SYSCFG_SetDMARemap_CH2）
 *   Task 10 ADC      -> DMA1_Channel3（LL_SYSCFG_SetDMARemap_CH3，request 0x00）
 *
 * g_val[] 由 DMA 直接写、主循环随时读，故 volatile。
 *
 * 中断说明：HAL_ADC_Start_DMA 内部调 HAL_DMA_Start_IT，会打开 DMA 通道的
 *   TC/HT/TE 中断。但 DMA1_Channel2/3 共用 DMA1_Channel2_3_IRQn（已被 BLE
 *   使能），若不处理 channel3 的 TC 会 IRQ 风暴。本驱动循环模式下不需要任何
 *   回调（主循环直接读 g_val 最新值），故 Start_DMA 之后立即关掉 DMA 通道
 *   的 TC/HT/TE 中断。循环模式下 DMA 自动重载继续搬运，不受 IT 使能影响。
 */

static ADC_HandleTypeDef hadc;
static DMA_HandleTypeDef hdma_adc;
static volatile uint16_t g_val[ADC_CH_COUNT];

static void Adc_GpioClkInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode = GPIO_MODE_ANALOG;
    gi.Pull = GPIO_NOPULL;
    gi.Pin  = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gi);
}

void Bsp_Adc_Init(void)
{
    Adc_GpioClkInit();

    hadc.Instance = ADC1;
    hadc.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode          = ADC_SCAN_DIRECTION_FORWARD;
    hadc.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    hadc.Init.LowPowerAutoWait      = DISABLE;
    hadc.Init.ContinuousConvMode    = ENABLE;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc.Init.DMAContinuousRequests = ENABLE;
    hadc.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc.Init.SamplingTimeCommon    = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_Init(&hadc);

    ADC_ChannelConfTypeDef sc = {0};
    sc.Rank    = ADC_RANK_CHANNEL_NUMBER;
    sc.Channel = ADC_CHANNEL_0; HAL_ADC_ConfigChannel(&hadc, &sc);
    sc.Channel = ADC_CHANNEL_1; HAL_ADC_ConfigChannel(&hadc, &sc);
    sc.Channel = ADC_CHANNEL_2; HAL_ADC_ConfigChannel(&hadc, &sc);
    sc.Channel = ADC_CHANNEL_3; HAL_ADC_ConfigChannel(&hadc, &sc);

    /* DMA */
    hdma_adc.Instance                 = DMA1_Channel3;
    hdma_adc.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc.Init.Mode                = DMA_CIRCULAR;
    hdma_adc.Init.Priority            = DMA_PRIORITY_MEDIUM;
    HAL_DMA_Init(&hdma_adc);
    __HAL_LINKDMA(&hadc, DMA_Handle, hdma_adc);

    /* SYSCFG DMA remap：ADC(request 0x00) -> DMA1_Channel3
       LL_SYSCFG_SetDMARemap_CH3 用 MODIFY_REG 清 DMA3_MAP 域再写入，
       比 HAL_SYSCFG_DMA_Req(SET_BIT) 更安全（HAL 版只 OR 不清）。 */
    LL_SYSCFG_SetDMARemap_CH3(LL_SYSCFG_DMA_MAP_ADC);

    HAL_ADCEx_Calibration_Start(&hadc);
    HAL_ADC_Start_DMA(&hadc, (uint32_t *)g_val, ADC_CH_COUNT);

    /* HAL_ADC_Start_DMA 内部开了 DMA 通道 TC/HT/TE 中断（HAL_DMA_Start_IT）。
       DMA1_CH2/CH3 共用 DMA1_Channel2_3_IRQn（BLE 已 NVIC 使能），
       循环模式下不需要回调，关掉避免 channel3 的 TC 风暴。
       循环模式 DMA 计数到 0 自动重载，不受 IT 使能影响。 */
    __HAL_DMA_DISABLE_IT(&hdma_adc, (DMA_IT_TC | DMA_IT_HT | DMA_IT_TE));
}

uint16_t Bsp_Adc_Read(Bsp_Adc_Channel_t ch)
{
    if (ch >= ADC_CH_COUNT) return 0;
    return g_val[ch];
}
