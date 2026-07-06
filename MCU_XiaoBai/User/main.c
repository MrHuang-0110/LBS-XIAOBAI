#include "main.h"
#include "Bsp.h"

static void APP_SystemClockConfig(void);

/* 4 只模式 LED 同步闪 N 下（每下亮 100ms 灭 100ms） */
static void FlashLeds(uint8_t times)
{
    for (uint8_t t = 0; t < times; t++) {
        Bsp_Led_On(LED_MODE_POWER);
        Bsp_Led_On(LED_MODE_SENSOR);
        Bsp_Led_On(LED_MODE_REMOTE);
        Bsp_Led_On(LED_MODE_VOICE);
        Bsp_Tick_DelayMs(100);
        Bsp_Led_AllOff();
        Bsp_Tick_DelayMs(100);
    }
}

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    BSP_Init();

    /* 应用层时序：等 ASRPRO 启动 + 播开机语 + 默认语音模式指示 */
    Bsp_Tick_DelayMs(1500);
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
    Bsp_Led_On(LED_MODE_VOICE);

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("LBS_XIAOBAI", 11);

    /* TM1640 亮灭控制测试：循环 全亮1s -> 灭1s，每 4 轮降一档亮度 */
    uint8_t tm_state = 0;        /* 0=亮 1=灭 */
    uint8_t tm_bright = 7;       /* 0..7 */
    uint8_t tm_cycle = 0;        /* 计数到 8 降一档亮度 */
    uint32_t tm_t = Bsp_Tick_GetMs();
    static const uint8_t all_on[14] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                       0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    /* PF3 连接状态边沿检测：断→通 触发 play=07，通→断 触发 play=08 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    /* 模式状态机：开机默认语音模式，短按 KEY1 循环切换
       语音→动力→感应→遥控→语音（LED4→LED1→LED2→LED3→LED4） */
    typedef enum { APP_MODE_VOICE=0, APP_MODE_POWER, APP_MODE_SENSOR, APP_MODE_REMOTE, APP_MODE_COUNT } App_Mode_t;
    static const Bsp_Led_Id_t mode_led[APP_MODE_COUNT] = {
        LED_MODE_VOICE, LED_MODE_POWER, LED_MODE_SENSOR, LED_MODE_REMOTE,
    };
    App_Mode_t mode = APP_MODE_VOICE;
    Bsp_Led_AllOff();
    Bsp_Led_On(mode_led[mode]);

    /* 遥控帧流缓冲：BLE 透传会分段到达，把每次 TryRecv 的字节追加进来，
       在流里滑动找完整 17 字节帧，避免半帧被 TryRecv 切走。 */
    static uint8_t stream[REMOTE_FRAME_LEN * 4];
    uint16_t stream_len = 0;

    while (1) {
        /* --- 按键扫描（KEY1：短按切模式 / 长按关机） --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT && kid == KEY_ID_1) {
                mode = (App_Mode_t)((mode + 1) % APP_MODE_COUNT);
                Bsp_Led_AllOff();
                Bsp_Led_On(mode_led[mode]);
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                Bsp_Motor_StopAll();                              /* 1. 停电机 */
                Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);         /* 2. 请求播关机语 */
                Bsp_Tick_DelayMs(1500);                           /* 3. 等语音播完 */
                Bsp_Led_AllOff();                                 /* 4. 灭模式 LED */
                Bsp_LedPwm_Set(LEDPWM_1, 0);                      /*    灭呼吸灯1 */
                Bsp_LedPwm_Set(LEDPWM_2, 0);                      /*    灭呼吸灯2 */
                Bsp_Tm1640_Clear();                               /*    清点阵 */
                Bsp_Power_ShutDown();                             /* 5. PA15=0 断电，不返回 */
            }
        }

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

        /* --- BLE 数据接收 + 流式遥控帧解析 ---
           BLE 透传会分段到达（一次 IDLE 可能只收到半帧），所以把每次
           TryRecv 的字节追加到 stream 流缓冲，在流里滑动找完整 17 字节帧。 */
        {
            uint8_t buf[REMOTE_FRAME_LEN * 2];
            uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
            for (uint16_t k = 0; k < n; k++) {
                if (stream_len < sizeof(stream)) {
                    stream[stream_len++] = buf[k];
                }
                /* 满了就不再追加（丢新数据，正常不会发生） */
            }
        }

        /* 在流缓冲里滑动找完整帧；解析掉的就从流里移除 */
        {
            uint16_t i = 0;
            while (i + REMOTE_FRAME_LEN <= stream_len) {
                /* 找帧头：当前位置不是 0x5A 就跳过 */
                if (stream[i] != REMOTE_FRAME_HEAD) { i++; continue; }
                /* 校验固定字段 */
                if (stream[i+1]!=0x97 || stream[i+2]!=0x98 ||
                    stream[i+3]!=0x0A || stream[i+4]!=0xC1) { i++; continue; }
                if (stream[i+REMOTE_FRAME_LEN-1] != REMOTE_FRAME_TAIL) { i++; continue; }
                /* CRC：从帧头到数据位最后一位的累加和取低 8 位 */
                uint8_t crc = 0;
                for (uint16_t j = 0; j < REMOTE_FRAME_LEN - 2; j++) crc += stream[i + j];
                if (crc != stream[i + REMOTE_FRAME_LEN - 2]) { i++; continue; }

                /* 帧有效，扫描 10 个键值位图，按按键分级闪 LED */
                {
                    uint8_t *keys = &stream[i + 5];
                    uint8_t  flashes = 0;
                    if (keys[REMOTE_KEY_UP]    || keys[REMOTE_KEY_DOWN] ||
                        keys[REMOTE_KEY_LEFT]  || keys[REMOTE_KEY_RIGHT]) {
                        flashes = 1;
                    }
                    if (keys[REMOTE_KEY_Y] || keys[REMOTE_KEY_A] ||
                        keys[REMOTE_KEY_X] || keys[REMOTE_KEY_B]) {
                        flashes = 2;
                    }
                    if (keys[REMOTE_KEY_L1] || keys[REMOTE_KEY_R1]) {
                        flashes = 3;
                    }
                    if (flashes) FlashLeds(flashes);
                }

                i += REMOTE_FRAME_LEN;   /* 跳过已消费的帧 */
            }

            /* 把 i 之前已扫描过/丢弃的字节从流缓冲移除，保留 [i, stream_len) */
            if (i > 0) {
                uint16_t remain = stream_len - i;
                for (uint16_t k = 0; k < remain; k++) stream[k] = stream[i + k];
                stream_len = remain;
            }
        }

        /* TM1640 亮灭控制：每 1000ms 切换亮/灭，每 8 轮降一档亮度到 0 再回到 7 */
        {
            uint32_t now = Bsp_Tick_GetMs();
            if (now - tm_t >= 1000) {
                tm_t = now;
                tm_state = !tm_state;
                if (tm_state) {
                    Bsp_Tm1640_Clear();              /* 灭 */
                } else {
                    Bsp_Tm1640_SetBrightness(tm_bright);
                    Bsp_Tm1640_Refresh(all_on);      /* 亮，当前亮度 */
                    tm_cycle++;
                    if (tm_cycle >= 8) {
                        tm_cycle = 0;
                        tm_bright = (tm_bright == 0) ? 7 : (tm_bright - 1);
                    }
                }
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
