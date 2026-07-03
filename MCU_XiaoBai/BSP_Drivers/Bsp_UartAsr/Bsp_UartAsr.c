#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include <string.h>

#define RX_BUF_SIZE   64U
#define FRAME_Q_SIZE  4U    /* 环形队列最多缓 4 帧 */

static UART_HandleTypeDef huart;
static DMA_HandleTypeDef  hdma_rx;
static uint8_t g_rx_buf[RX_BUF_SIZE];

static Bsp_UartAsr_Frame_t g_frame_q[FRAME_Q_SIZE];
static volatile uint8_t g_qw = 0, g_qr = 0;

static uint8_t Frame_Check(const uint8_t *p6)
{
    if (p6[0] != ASR_FRAME_HEAD) return 0;
    if (p6[1] != ASR_FRAME_LEN)  return 0;
    uint8_t x = p6[1] ^ p6[2] ^ p6[3] ^ p6[4];
    return x == p6[5];
}

/* 在 buf 中扫 0xA5 帧头，每找到一个就试解 6 字节，成功入队 */
static void Frame_Feed(const uint8_t *buf, uint16_t len)
{
    uint16_t i = 0;
    while (i + ASR_FRAME_SIZE <= len) {
        if (buf[i] != ASR_FRAME_HEAD) { i++; continue; }
        if (Frame_Check(&buf[i])) {
            uint8_t next = (uint8_t)((g_qw + 1) % FRAME_Q_SIZE);
            if (next != g_qr) {   /* 队列未满 */
                g_frame_q[g_qw].cmd = buf[i + 2];
                g_frame_q[g_qw].d0  = buf[i + 3];
                g_frame_q[g_qw].d1  = buf[i + 4];
                g_qw = next;
            }
            i += ASR_FRAME_SIZE;
        } else {
            i++;
        }
    }
}

void Bsp_UartAsr_Init(void)
{
    /* 时钟 */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PF0=USART2_RX, PF1=USART2_TX，AF4（datasheet V2.5 §3.3）*/
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF4_USART2;
    gi.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOF, &gi);

    /* SYSCFG DMA remap：USART2_RX -> DMA1_Channel1 (request ID 0x08)
       API 在 py32f0xx_hal.c:534，就是直接写 SYSCFG->CFGR3 的 CH1 位域 */
    HAL_SYSCFG_DMA_Req(0x08);

    /* DMA channel 1 for USART2_RX，循环模式 */
    hdma_rx.Instance                 = DMA1_Channel1;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_rx);

    /* USART */
    huart.Instance          = USART2;
    huart.Init.BaudRate     = 115200;
    huart.Init.WordLength   = UART_WORDLENGTH_8B;
    huart.Init.StopBits     = UART_STOPBITS_1;
    huart.Init.Parity       = UART_PARITY_NONE;
    huart.Init.Mode         = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart);

    __HAL_LINKDMA(&huart, hdmarx, hdma_rx);

    /* NVIC */
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    /* 启动 DMA 循环接收 + 使能 IDLE 中断 */
    HAL_UART_Receive_DMA(&huart, g_rx_buf, RX_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
}

void Bsp_UartAsr_Send(uint8_t cmd, uint8_t d0, uint8_t d1)
{
    uint8_t tx[ASR_FRAME_SIZE] = {
        ASR_FRAME_HEAD, ASR_FRAME_LEN, cmd, d0, d1,
        (uint8_t)(ASR_FRAME_LEN ^ cmd ^ d0 ^ d1)
    };
    HAL_UART_Transmit(&huart, tx, sizeof(tx), 10);
}

uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Frame_t *out)
{
    if (g_qr == g_qw) return 0;
    *out = g_frame_q[g_qr];
    g_qr = (uint8_t)((g_qr + 1) % FRAME_Q_SIZE);
    return 1;
}

/* HAL 会在 IDLE 检测到帧结束时调这个弱回调 */
void HAL_UART_IdleFrameDetectCpltCallback(UART_HandleTypeDef *huart_p)
{
    if (huart_p->Instance != USART2) return;

    uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_rx);
    uint16_t cur  = RX_BUF_SIZE - ndtr;

    static uint16_t last_pos = 0;
    if (cur == last_pos) return;

    if (cur > last_pos) {
        Frame_Feed(&g_rx_buf[last_pos], (uint16_t)(cur - last_pos));
    } else {
        /* wrap */
        Frame_Feed(&g_rx_buf[last_pos], (uint16_t)(RX_BUF_SIZE - last_pos));
        if (cur) Frame_Feed(&g_rx_buf[0], cur);
    }
    last_pos = cur;
}

/* 中断入口（由 py32f0xx_it.c 转发） */
void Bsp_UartAsr_UART_IRQHandler(void) { HAL_UART_IRQHandler(&huart); }
void Bsp_UartAsr_DMA_IRQHandler (void) { HAL_DMA_IRQHandler(&hdma_rx); }
