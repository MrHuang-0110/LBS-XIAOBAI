#include "Bsp_UartBle/Bsp_UartBle.h"
#include "py32f0xx_ll_system.h"   /* LL_SYSCFG_SetDMARemap_CH2 */
#include <string.h>

/*
 * USART1 (PB6=TX / PB7=RX, AF0) <-> BLE ECB00 模块
 *   115200 8N1 全双工，DMA1_Channel2 循环收 + UART IDLE 中断
 *
 * DMA 通道分配（与 Task 8 互补）：
 *   Task 8 USART2_RX -> DMA1_Channel1（HAL_SYSCFG_DMA_Req(0x08)，CH1 映射）
 *   Task 9 USART1_RX -> DMA1_Channel2（LL_SYSCFG_SetDMARemap_CH2，CH2 映射）
 *   request ID 0x06 = USART1_RX
 *
 * IDLE 处理：直接在 Bsp_UartBle_UART_IRQHandler 里检查/清 IDLE 标志，
 *   读 DMA 剩余计数得到 cur 位置，把 [last_pos, cur) 段累积到 g_out。
 *   不用 HAL_UART_IdleFrameDetectCpltCallback 弱回调——Bsp_UartAsr.c 已
 *   覆盖该回调（只处理 USART2），此处直接处理避免多重定义冲突。
 *
 * 接收路径（不定长）：
 *   DMA 循环写 g_rx[128]；IDLE 中断把增量段拷进 g_out[128] + g_out_len。
 *   Bsp_UartBle_TryRecv 一次性取走 g_out 并清零 g_out_len（关中断保护）。
 *   BLE ECB00 协议待定，本驱动只做原始字节透传，不做协议解析。
 */

static UART_HandleTypeDef huart;
static DMA_HandleTypeDef  hdma_rx;
static uint8_t g_rx[BLE_RX_BUF_SIZE];

/* 出队缓冲：IDLE 中断累积，TryRecv 一次性取走 */
static uint8_t  g_out[BLE_RX_BUF_SIZE];
static volatile uint16_t g_out_len = 0;

/* 上次处理到的 DMA 位置（只在 IRQ 里访问） */
static uint16_t g_last_pos = 0;

static void Ble_GpioClkInit(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PB6=TX1, PB7=RX1, AF0（datasheet V2.5 §3.2） */
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF0_USART1;
    gi.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gi);

    /* PF3 BLE_STA 输入（主循环轮询，不用中断） */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_NOPULL;
    gi.Pin  = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOF, &gi);
}

void Bsp_UartBle_Init(void)
{
    Ble_GpioClkInit();

    huart.Instance          = USART1;
    huart.Init.BaudRate     = 115200;
    huart.Init.WordLength   = UART_WORDLENGTH_8B;
    huart.Init.StopBits     = UART_STOPBITS_1;
    huart.Init.Parity       = UART_PARITY_NONE;
    huart.Init.Mode         = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart);

    hdma_rx.Instance                 = DMA1_Channel2;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    HAL_DMA_Init(&hdma_rx);

    __HAL_LINKDMA(&huart, hdmarx, hdma_rx);

    /* SYSCFG DMA remap：USART1_RX(request 0x06) -> DMA1_Channel2
       LL_SYSCFG_SetDMARemap_CH2 用 MODIFY_REG 清 DMA2_MAP 域再写入，
       比 HAL_SYSCFG_DMA_Req(SET_BIT) 更安全（HAL 版只 OR 不清）。 */
    LL_SYSCFG_SetDMARemap_CH2(LL_SYSCFG_DMA_MAP_USART1_RX);

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

    HAL_UART_Receive_DMA(&huart, g_rx, BLE_RX_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
}

void Bsp_UartBle_Send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart, (uint8_t *)data, len, 20);
}

uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len)
{
    __disable_irq();
    uint16_t n = g_out_len;
    if (n > max_len) n = max_len;
    if (n) memcpy(out_buf, g_out, n);
    g_out_len = 0;
    __enable_irq();
    return n;
}

uint8_t Bsp_UartBle_IsConnected(void)
{
    /* 假设 PF3 = 高 表示已连接。若极性相反，改成 == RESET。 */
    return HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_3) == GPIO_PIN_SET ? 1 : 0;
}

/* IDLE 中断把 [last_pos..cur) 追加到 g_out。
   直接在 UART IRQ 里处理 IDLE（先清标志再调 HAL_UART_IRQHandler，
   这样 HAL 不会重复进 IDLE 分支、也不会调已被 Bsp_UartAsr 覆盖的弱回调）。 */
void Bsp_UartBle_UART_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart);

        uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_rx);
        uint16_t cur  = BLE_RX_BUF_SIZE - ndtr;

        if (cur != g_last_pos) {
            uint16_t space = (uint16_t)(BLE_RX_BUF_SIZE - g_out_len);
            uint16_t seg1, seg2;
            if (cur > g_last_pos) {
                seg1 = (uint16_t)(cur - g_last_pos); seg2 = 0;
            } else {
                seg1 = (uint16_t)(BLE_RX_BUF_SIZE - g_last_pos);
                seg2 = cur;
            }
            uint16_t need = seg1 + seg2;
            if (need > space) need = space;
            uint16_t take1 = need > seg1 ? seg1 : need;
            if (take1) { memcpy(&g_out[g_out_len], &g_rx[g_last_pos], take1); g_out_len += take1; }
            uint16_t take2 = need - take1;
            if (take2) { memcpy(&g_out[g_out_len], &g_rx[0], take2); g_out_len += take2; }
            g_last_pos = cur;
        }
    }
    HAL_UART_IRQHandler(&huart);
}

void Bsp_UartBle_DMA_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_rx); }
