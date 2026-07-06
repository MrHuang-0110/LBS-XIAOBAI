#include "main.h"
#include "Bsp.h"

/* ===== 模式与动作枚举 ===== */
typedef enum {
    APP_MODE_VOICE  = 0,
    APP_MODE_POWER  = 1,
    APP_MODE_SENSOR = 2,
    APP_MODE_REMOTE = 3,
    APP_MODE_COUNT
} App_Mode_t;

/* 动力模式的 5 个动作（文档 §8）*/
typedef enum {
    POWER_ACT_STOP  = 0,
    POWER_ACT_FWD   = 1,
    POWER_ACT_BACK  = 2,
    POWER_ACT_LEFT  = 3,
    POWER_ACT_RIGHT = 4,
    POWER_ACT_COUNT
} Power_Action_t;

/* 模式 -> 对应模式 LED（文档 §6：LED1=动力/LED2=感应/LED3=遥控/LED4=语音）*/
static const Bsp_Led_Id_t mode_led[APP_MODE_COUNT] = {
    LED_MODE_VOICE, LED_MODE_POWER, LED_MODE_SENSOR, LED_MODE_REMOTE,
};
/* 模式 -> 进入时播报的语音 ID */
static const uint8_t mode_voice[APP_MODE_COUNT] = {
    ASR_VOICE_ENTER_VOICE, ASR_VOICE_ENTER_POWER,
    ASR_VOICE_ENTER_SENSOR, ASR_VOICE_ENTER_REMOTE,
};
/* 动力动作 -> 语音 ID（文档 §7）*/
static const uint8_t act_voice[POWER_ACT_COUNT] = {
    ASR_VOICE_STOP, ASR_VOICE_FORWARD, ASR_VOICE_BACKWARD,
    ASR_VOICE_LEFT, ASR_VOICE_RIGHT,
};

/* ===== 全局状态 ===== */
static App_Mode_t      g_mode;
static Power_Action_t  g_power_action = POWER_ACT_STOP;

/* ===== 统一函数 ===== */

/* 切模式：停电机 + 点 LED + 播报。按键/语音切换都走这里。 */
static void SwitchMode(App_Mode_t new_mode)
{
    if (new_mode >= APP_MODE_COUNT) return;
    Bsp_Motor_StopAll();
    g_mode = new_mode;
    Bsp_Led_AllOff();
    Bsp_Led_On(mode_led[g_mode]);
    Bsp_UartAsr_SendPlay(mode_voice[g_mode]);
}

/* 动力模式执行某动作：电机 + 播报。动力模式短按/语音动作命令都走这里。 */
static void Power_Execute(Power_Action_t act)
{
    if (act >= POWER_ACT_COUNT) return;
    g_power_action = act;
    /* 动力模式固定 100% 速度 */
    switch (act) {
    case POWER_ACT_STOP:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH);
        break;
    case POWER_ACT_FWD:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
        break;
    case POWER_ACT_BACK:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
        break;
    case POWER_ACT_LEFT:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
        break;
    case POWER_ACT_RIGHT:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH);
        break;
    }
    Bsp_UartAsr_SendPlay(act_voice[act]);
}

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    BSP_Init();

    /* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
    Bsp_Tick_DelayMs(1500);
    SwitchMode(APP_MODE_VOICE);   /* 点 LED4 + 播"进入语音模式"（兼作开机语） */

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("LBS_XIAOBAI", 11);

    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    /* 遥控帧流缓冲（M3 遥控模式用，先保留解析框架） */
    static uint8_t stream[REMOTE_FRAME_LEN * 4];
    uint16_t stream_len = 0;

    while (1) {
        /* --- 按键扫描 --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT && kid == KEY_ID_1) {
                if (g_mode == APP_MODE_POWER) {
                    /* 动力模式：短按循环切动作 停→进→退→左→右→停 */
                    Power_Execute((Power_Action_t)((g_power_action + 1) % POWER_ACT_COUNT));
                } else {
                    /* 其它模式：短按切模式 */
                    SwitchMode((App_Mode_t)((g_mode + 1) % APP_MODE_COUNT));
                }
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                Bsp_Motor_StopAll();
                Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
                Bsp_Tick_DelayMs(1500);
                Bsp_Led_AllOff();
                Bsp_LedPwm_Set(LEDPWM_1, 0);
                Bsp_LedPwm_Set(LEDPWM_2, 0);
                Bsp_Tm1640_Clear();
                Bsp_Power_ShutDown();
            }
        }

        /* --- ASRPRO 语音命令 --- */
        {
            Bsp_UartAsr_Event_t e;
            if (Bsp_UartAsr_TryRecv(&e)) {
                if (e.type == ASR_EVT_CMD) {
                    switch (e.arg) {
                    /* 动作类命令（任何模式都执行电机）*/
                    case ASR_CMD_FORWARD:  Power_Execute(POWER_ACT_FWD);   break;
                    case ASR_CMD_BACKWARD: Power_Execute(POWER_ACT_BACK);  break;
                    case ASR_CMD_LEFT:     Power_Execute(POWER_ACT_LEFT);  break;
                    case ASR_CMD_RIGHT:    Power_Execute(POWER_ACT_RIGHT); break;
                    case ASR_CMD_STOP:     Power_Execute(POWER_ACT_STOP);  break;
                    /* 模式切换命令 */
                    case ASR_CMD_ENTER_POWER:  SwitchMode(APP_MODE_POWER);  break;
                    case ASR_CMD_ENTER_SENSOR: SwitchMode(APP_MODE_SENSOR); break;
                    case ASR_CMD_ENTER_REMOTE: SwitchMode(APP_MODE_REMOTE); break;
                    case ASR_CMD_ENTER_VOICE:  SwitchMode(APP_MODE_VOICE);  break;
                    /* 单电机 cmd=40..45：M3 接入 */
                    default: break;
                    }
                }
                else if (e.type == ASR_EVT_WAKE) {
                    Bsp_UartAsr_SendPlay(ASR_VOICE_RECEIVED);
                }
                /* ASR_EVT_DONE 暂不处理 */
            }
        }

        /* --- BLE 连接状态边沿 + 语音播报 --- */
        {
            uint8_t ble_now = Bsp_UartBle_IsConnected();
            if (ble_now && !ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_CONNECTED);
            } else if (!ble_now && ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_LOST);
            }
            ble_was_connected = ble_now;
        }

        /* --- BLE 遥控帧解析（M3 接电机，先解析占位） --- */
        {
            uint8_t buf[REMOTE_FRAME_LEN * 2];
            uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
            for (uint16_t k = 0; k < n; k++) {
                if (stream_len < sizeof(stream)) stream[stream_len++] = buf[k];
            }
            uint16_t i = 0;
            while (i + REMOTE_FRAME_LEN <= stream_len) {
                if (stream[i] != REMOTE_FRAME_HEAD) { i++; continue; }
                if (stream[i+1]!=0x97 || stream[i+2]!=0x98 ||
                    stream[i+3]!=0x0A || stream[i+4]!=0xC1) { i++; continue; }
                if (stream[i+REMOTE_FRAME_LEN-1] != REMOTE_FRAME_TAIL) { i++; continue; }
                uint8_t crc = 0;
                for (uint16_t j = 0; j < REMOTE_FRAME_LEN - 2; j++) crc += stream[i + j];
                if (crc != stream[i + REMOTE_FRAME_LEN - 2]) { i++; continue; }
                /* 帧有效：M3 在此根据按键驱动电机/调速。先空过。 */
                i += REMOTE_FRAME_LEN;
            }
            if (i > 0) {
                uint16_t remain = stream_len - i;
                for (uint16_t k = 0; k < remain; k++) stream[k] = stream[i + k];
                stream_len = remain;
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
