#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include "Bsp_UartBle/Bsp_UartBle.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();
    if (!Bsp_Power_Init_WaitConfirm()) while (1) { }

    Bsp_LedPwm_Init();
    Bsp_LedPwm_PlayStartupBreath();

    Bsp_UartAsr_Init();

    /* ASRPRO 跟 MCU 同电源上电，但 ASRPRO 启动较慢。
       全速下 MCU ~4.5s 后发 play=01 时 ASRPRO 可能还没 ready 会丢帧。
       留 1.5s 让 ASRPRO 完成 boot + UART 初始化。 */
    Bsp_Tick_DelayMs(1500);

    /* 请求 ASRPRO 播开机语 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);

    /* 默认语音模式：LED4 常亮 */
    Bsp_Led_On(LED_MODE_VOICE);

    /* BLE (USART1 PB6/PB7 + DMA1_CH2 + IDLE + PF3 状态) */
    Bsp_UartBle_Init();

    /* 配置蓝牙名称（ECB00 默认从机透传，只需设名）。
       留 500ms 让 BLE 模块完成上电 */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("LBS_XIAOBAI", 11);
 
    /* PF3 连接状态边沿检测：断→通 触发 play=07，通→断 触发 play=08 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    while (1) {
        /* --- ASRPRO 事件处理（Task 8 逻辑保留） --- */
        Bsp_UartAsr_Event_t e;
        if (Bsp_UartAsr_TryRecv(&e)) {
            switch (e.type) {
            case ASR_EVT_CMD:
                /* 收到语音命令，翻转 LED3（暂时用来验证 RX 通路） */
                Bsp_Led_Toggle(LED_MODE_REMOTE);
                break;
            case ASR_EVT_WAKE:
                /* 唤醒 -- 回一句"收到" */
                Bsp_UartAsr_SendPlay(ASR_VOICE_RECEIVED);
                break;
            case ASR_EVT_DONE:
                /* 播报完成，暂不处理 */
                break;
            default: break;
            }
        }

        /* --- BLE 连接状态边沿检测 + 语音播报 --- */
        uint8_t ble_now = Bsp_UartBle_IsConnected();
        if (ble_now && !ble_was_connected) {
            Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_CONNECTED);   /* play=07 */
        } else if (!ble_now && ble_was_connected) {
            Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_LOST);        /* play=08 */
        }
        ble_was_connected = ble_now;

        /* --- BLE 数据接收 + 遥控帧解析 --- */
        uint8_t buf[REMOTE_FRAME_LEN * 2];
        uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
        if (n) {
            /* 收到数据翻转 LED2 做视觉确认（不回发，避免干扰遥控器） */
            Bsp_Led_Toggle(LED_MODE_SENSOR);

            /* 遥控协议：5A 97 98 0A C1 [10字节键值] CRC A5，共 16 字节
               在 buf 里滑动找帧头 0x5A，校验通过就处理 */
            for (uint16_t i = 0; i + REMOTE_FRAME_LEN <= n; i++) {
                if (buf[i] != REMOTE_FRAME_HEAD) continue;
                if (buf[i + 1] != 0x97 || buf[i + 2] != 0x98) continue;
                if (buf[i + 3] != 0x0A || buf[i + 4] != 0xC1) continue;
                if (buf[i + REMOTE_FRAME_LEN - 1] != REMOTE_FRAME_TAIL) continue;
                /* CRC：从帧头到数据位最后一位的累加和取低 8 位 */
                uint8_t crc = 0;
                for (uint16_t j = 0; j < REMOTE_FRAME_LEN - 2; j++) crc += buf[i + j];
                if (crc != buf[i + REMOTE_FRAME_LEN - 2]) continue;

                /* 帧有效，扫描 10 个键值位图 */
                uint8_t *keys = &buf[i + 5];
                if (keys[REMOTE_KEY_UP])    Bsp_Led_Toggle(LED_MODE_POWER);   /* 上 */
                if (keys[REMOTE_KEY_DOWN])  Bsp_Led_Toggle(LED_MODE_SENSOR); /* 下 */
                if (keys[REMOTE_KEY_LEFT])  Bsp_Led_Toggle(LED_MODE_REMOTE); /* 左 */
                if (keys[REMOTE_KEY_RIGHT]) Bsp_Led_Toggle(LED_MODE_VOICE);  /* 右 */
                /* 其它键（Y/A/X/B/R1/L1）暂不处理，业务层接入时再扩展 */

                i += REMOTE_FRAME_LEN - 1;   /* 跳过已消费的帧 */
            }
        }

        Bsp_Tick_DelayMs(5);
    }
}

static void APP_SystemClockConfig(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSIDiv = RCC_HSI_DIV1;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) APP_ErrorHandler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) APP_ErrorHandler();
}

void APP_ErrorHandler(void) { while (1) { } }
