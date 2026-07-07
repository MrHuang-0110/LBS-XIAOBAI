#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include "py32f0xx_ll_system.h"   /* LL_SYSCFG_SetDMARemap_CH1 */
#include <string.h>

/*
 * 语音协议 v0.5 实现：ASCII 文本
 *   MCU 发（无帧尾）：<tag>[=<dec>]
 *   MCU 收（帧尾 \r\n）：<tag>[=<dec>]\r\n
 *
 * 接收路径：DMA 循环 + UART IDLE 中断
 *   - IDLE 中断里读 DMA 剩余字节数得到新收字节段
 *   - 逐字节喂进字符状态机 Frame_FeedByte
 *   - 收到 '\n' 时判定为一帧结束（若前一字节是 '\r' 顺便剔除），
 *     把 g_line 里累积的字符交给 Line_Dispatch 解析
 *
 * 发送路径：把 "tag[=NN]" 组装成字符串，HAL_UART_Transmit 阻塞发送（无帧尾）。
 */

#define RX_DMA_BUF_SIZE   64U     /* DMA 循环缓冲 */
#define LINE_BUF_SIZE     32U     /* 单帧最大长度（协议约定，不含 \r\n） */
#define EVT_Q_SIZE        4U

static UART_HandleTypeDef huart;
static DMA_HandleTypeDef  hdma_rx;
static uint8_t g_rx_dma[RX_DMA_BUF_SIZE];

/* 字符状态机 */
static uint8_t  g_line[LINE_BUF_SIZE];
static uint16_t g_line_len = 0;
static uint8_t  g_line_ovf = 0;   /* 当前行已溢出，等 \n 复位 */

/* 事件环形队列 */
static Bsp_UartAsr_Event_t g_evtq[EVT_Q_SIZE];
static volatile uint8_t g_qw = 0, g_qr = 0;

static void Evt_Push(Bsp_UartAsr_EvtType_t t, uint8_t arg)
{
    uint8_t next = (uint8_t)((g_qw + 1) % EVT_Q_SIZE);
    if (next == g_qr) return;   /* 满了丢弃 */
    g_evtq[g_qw].type = t;
    g_evtq[g_qw].arg  = arg;
    g_qw = next;
}

/* 从形如 "cmd=30" 里取 "=NN"（1..3 位十进制，值 0..255），成功返回 0 */
static uint8_t Parse_EqDec(const uint8_t *line, uint16_t len, uint16_t tag_len, uint8_t *out)
{
    if (len < tag_len + 2) return 1;
    if (line[tag_len] != '=') return 1;

    uint16_t val = 0;
    uint16_t digits = 0;
    for (uint16_t i = tag_len + 1; i < len; i++) {
        uint8_t c = line[i];
        if (c < '0' || c > '9') return 2;
        val = val * 10U + (uint16_t)(c - '0');
        digits++;
        if (digits > 3 || val > 255U) return 2;
    }
    if (digits == 0) return 2;
    *out = (uint8_t)val;
    return 0;
}

/* 一帧字符（已剥除 \r\n）到齐后调这个函数分派 */
static void Line_Dispatch(const uint8_t *line, uint16_t len)
{
    if (len == 0) return;

    /* wake */
    if (len == 4 && memcmp(line, "wake", 4) == 0) {
        Evt_Push(ASR_EVT_WAKE, 0);
        return;
    }
    /* cmd=NN */
    if (len >= 3 && memcmp(line, "cmd", 3) == 0) {
        uint8_t v;
        if (Parse_EqDec(line, len, 3, &v) == 0) Evt_Push(ASR_EVT_CMD, v);
        return;
    }
    /* done=NN */
    if (len >= 4 && memcmp(line, "done", 4) == 0) {
        uint8_t v;
        if (Parse_EqDec(line, len, 4, &v) == 0) Evt_Push(ASR_EVT_DONE, v);
        return;
    }
    /* 其它 tag 忽略 */
}

/* 字符状态机 */
static void Frame_FeedByte(uint8_t c)
{
    if (c == '\n') {
        if (!g_line_ovf) {
            uint16_t n = g_line_len;
            if (n > 0 && g_line[n - 1] == '\r') n--;    /* 剥掉 \r */
            Line_Dispatch(g_line, n);
        }
        g_line_len = 0;
        g_line_ovf = 0;
        return;
    }
    if (g_line_ovf) return;
    if (g_line_len >= LINE_BUF_SIZE) {
        g_line_ovf = 1;
        return;
    }
    g_line[g_line_len++] = c;
}

/* 从 DMA 环形缓冲 [last_pos, cur) 区段喂给状态机（跨界自动 wrap） */
static void Frame_FeedRange(uint16_t last_pos, uint16_t cur)
{
    if (cur == last_pos) return;
    if (cur > last_pos) {
        for (uint16_t i = last_pos; i < cur; i++) Frame_FeedByte(g_rx_dma[i]);
    } else {
        for (uint16_t i = last_pos; i < RX_DMA_BUF_SIZE; i++) Frame_FeedByte(g_rx_dma[i]);
        for (uint16_t i = 0; i < cur; i++) Frame_FeedByte(g_rx_dma[i]);
    }
}

void Bsp_UartAsr_Init(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PF0=RX, PF1=TX, AF4（datasheet V2.5 §3.3） */
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF4_USART2;
    gi.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOF, &gi);

    /* SYSCFG DMA remap：USART2_RX -> DMA1_Channel1
       用 LL 库 MODIFY_REG 先清 DMA1_MAP 位域再写值，比 HAL_SYSCFG_DMA_Req
       的 SET_BIT（只 OR 不清）更可靠。 */
    LL_SYSCFG_SetDMARemap_CH1(LL_SYSCFG_DMA_MAP_USART2_RX);

    hdma_rx.Instance                 = DMA1_Channel1;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_rx);

    huart.Instance          = USART2;
    huart.Init.BaudRate     = 9600;
    huart.Init.WordLength   = UART_WORDLENGTH_8B;
    huart.Init.StopBits     = UART_STOPBITS_1;
    huart.Init.Parity       = UART_PARITY_NONE;
    huart.Init.Mode         = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart);

    __HAL_LINKDMA(&huart, hdmarx, hdma_rx);

    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    HAL_UART_Receive_DMA(&huart, g_rx_dma, RX_DMA_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
}

/* --- 发送（无帧尾） --- */

static void SendStr(const char *s, uint16_t len)
{
    HAL_UART_Transmit(&huart, (uint8_t *)s, len, 20);
}

void Bsp_UartAsr_SendPlay(uint8_t voice_id)
{
    /* "play=NN"（voice_id 都在 1..99 之间，2 位十进制补零） */
    if (voice_id > 99U) voice_id = 99U;
    char buf[7];
    buf[0] = 'p'; buf[1] = 'l'; buf[2] = 'a'; buf[3] = 'y'; buf[4] = '=';
    buf[5] = (char)('0' + (voice_id / 10U));
    buf[6] = (char)('0' + (voice_id % 10U));
    SendStr(buf, 7);
}

void Bsp_UartAsr_SendStop(void) { SendStr("stop", 4); }
void Bsp_UartAsr_SendPing(void) { SendStr("ping", 4); }

void Bsp_UartAsr_SendRaw(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart, (uint8_t *)data, len, 50);
}

/* --- 接收 --- */

uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Event_t *out)
{
    if (g_qr == g_qw) return 0;
    *out = g_evtq[g_qr];
    g_qr = (uint8_t)((g_qr + 1) % EVT_Q_SIZE);
    return 1;
}

/* HAL 在 IDLE 触发时调这个弱回调：把 DMA 增量段喂给字符状态机 */
void HAL_UART_IdleFrameDetectCpltCallback(UART_HandleTypeDef *huart_p)
{
    if (huart_p->Instance != USART2) return;

    uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_rx);
    uint16_t cur  = RX_DMA_BUF_SIZE - ndtr;

    static uint16_t last_pos = 0;
    if (cur != last_pos) {
        Frame_FeedRange(last_pos, cur);
        last_pos = cur;
    }
}

/* 中断入口 */
void Bsp_UartAsr_UART_IRQHandler(void) { HAL_UART_IRQHandler(&huart); }
void Bsp_UartAsr_DMA_IRQHandler (void) { HAL_DMA_IRQHandler(&hdma_rx); }
