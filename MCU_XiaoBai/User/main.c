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

/* 模式 -> 对应 LED（KEY-LED 一一对应，2026-07-06 变更）：
     语音->LED1 / 感应->LED2 / 遥控->LED4 / 动力->LED3
   注：Bsp_Led 枚举名 LED_MODE_POWER 实际是 LED1(PB2)，LED_MODE_VOICE 是 LED4(PA12)，
   名称跟模式不对应，但 mode_led[] 按物理 LED 映射，逻辑正确。 */
static const Bsp_Led_Id_t mode_led[APP_MODE_COUNT] = {
    LED_MODE_POWER,   /* APP_MODE_VOICE  -> LED1 */
    LED_MODE_REMOTE,  /* APP_MODE_POWER  -> LED3 */
    LED_MODE_SENSOR,  /* APP_MODE_SENSOR -> LED2 */
    LED_MODE_VOICE,   /* APP_MODE_REMOTE -> LED4 */
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

/* 遥控模式状态 */
static Bsp_Motor_Speed_t g_remote_speed = MOTOR_SPEED_MID;  /* 3 档速度，默认 2 档 70% */
static uint32_t          g_last_remote_frame = 0;           /* 超时停机计时 */
static uint8_t           g_r1_was = 0, g_l1_was = 0;        /* 肩键边沿检测 */

/* 呼吸灯心跳状态（PA9） */
static int16_t  g_breath_val = 0;
static uint8_t  g_breath_dir = 1;     /* 1=上升 0=下降 */
static uint32_t g_breath_t   = 0;

/* ===== 统一函数 ===== */

/* 阻塞等待 ASRPRO 播报完成（done=voice_id），超时 timeout_ms 放弃。
 * 依赖 ASRPRO 固件主动上报 done=NN\r\n；若固件不发，会等满超时才返回。
 * 阻塞期间收到的其它 ASR 事件（cmd/wake）被丢弃——按键切动作/模式场景下
 * 用户不会同时说话，可接受。 */
static void Wait_VoiceDone(uint8_t voice_id, uint32_t timeout_ms)
{
    uint32_t start = Bsp_Tick_GetMs();
    while (Bsp_Tick_GetMs() - start < timeout_ms) {
        Bsp_UartAsr_Event_t e;
        if (Bsp_UartAsr_TryRecv(&e)) {
            if (e.type == ASR_EVT_DONE && e.arg == voice_id) return;
        }
    }
}

/* 切模式：停电机 + 点 LED + 可选播报。
 * play_voice=1（按键/开机）：播报后等播报完毕再返回，保证语音与动作同步。
 * play_voice=0（语音命令 cmd=50..53）：静默，避免用户刚说完又播报一遍。 */
static void SwitchMode(App_Mode_t new_mode, uint8_t play_voice)
{
    if (new_mode >= APP_MODE_COUNT) return;
    Bsp_Motor_StopAll();
    g_mode = new_mode;
    Bsp_Led_AllOff();
    Bsp_Led_On(mode_led[g_mode]);
    if (new_mode == APP_MODE_REMOTE) {
        g_remote_speed = MOTOR_SPEED_MID;  /* 进遥控模式默认 2 档 70% */
    }
    if (play_voice) {
        Bsp_UartAsr_SendPlay(mode_voice[g_mode]);
        Wait_VoiceDone(mode_voice[g_mode], 2000);
    }
}

/* 动力模式执行某动作：可选播报 + 电机。
 * play_voice=1（按键切动作）：先播报，等播报完毕再动电机，保证语音与动作同步。
 * play_voice=0（语音命令 cmd=30..34）：静默，避免用户刚说完又播报一遍。 */
static void Power_Execute(Power_Action_t act, uint8_t play_voice)
{
    if (act >= POWER_ACT_COUNT) return;
    g_power_action = act;
    /* 先播报，等播报完毕再动电机，保证语音与动作同步 */
    if (play_voice) {
        Bsp_UartAsr_SendPlay(act_voice[act]);
        Wait_VoiceDone(act_voice[act], 2000);
    }
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
        /* 坦克原地左转：左轮后退 + 右轮前进 */
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
        break;
    case POWER_ACT_RIGHT:
        /* 坦克原地右转：左轮前进 + 右轮后退 */
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
        break;
    }
}

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    BSP_Init();

    /* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
    Bsp_Tick_DelayMs(1500);
    SwitchMode(APP_MODE_VOICE, 1);   /* 点 LED1 + 播"进入语音模式"（兼作开机语） */

    /* PA8 常亮（系统运行指示），PA9 做心跳（主循环里更新）*/
    Bsp_LedPwm_Set(LEDPWM_1, 50);

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("LBS_XIAOBAI", 11);

    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    /* 遥控帧流缓冲（M3 遥控模式用，先保留解析框架） */
    static uint8_t stream[REMOTE_FRAME_LEN * 4];
    uint16_t stream_len = 0;

    /* 语音动作命令防抖时间戳：800ms 内只执行第一个动作命令，
       防 ASRPRO 误识别连续发 cmd 导致电机正反转交替抖动 */
    uint32_t last_cmd_time = 0;

    while (1) {
        /* --- 按键扫描（4 键各管一个模式，KEY1 长按关机） --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT) {
                switch (kid) {
                case KEY_ID_1: SwitchMode(APP_MODE_VOICE, 1);  break;  /* LED1 */
                case KEY_ID_2: SwitchMode(APP_MODE_SENSOR, 1); break;  /* LED2 */
                case KEY_ID_3: SwitchMode(APP_MODE_REMOTE, 1); break;  /* LED3 */
                case KEY_ID_4:  /* LED4 动力模式：已在动力模式则切动作 */
                    if (g_mode == APP_MODE_POWER) {
                        Power_Execute((Power_Action_t)((g_power_action + 1) % POWER_ACT_COUNT), 1);
                    } else {
                        SwitchMode(APP_MODE_POWER, 1);
                    }
                    break;
                default: break;
                }
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                Bsp_Motor_StopAll();
                Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
                Bsp_Power_ShutdownAnimation();   /* 关机动画 ~1.5s，期间语音播完 */
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
                    /* 动作类命令（30..34）+ 单电机命令（40..45）800ms 防抖，
                       防 ASRPRO 误识别连续发导致抖动；
                       模式切换命令（50..53）不防抖。 */
                    uint8_t is_action = (e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP);
                    uint32_t now = Bsp_Tick_GetMs();
                    if (is_action && (now - last_cmd_time < 800)) {
                        /* 忽略 */
                    } else {
                        if (is_action) last_cmd_time = now;
                        switch (e.arg) {
                        /* 组合动作命令（任何模式都执行电机，静默不播报）*/
                        case ASR_CMD_FORWARD:  Power_Execute(POWER_ACT_FWD, 0);   break;
                        case ASR_CMD_BACKWARD: Power_Execute(POWER_ACT_BACK, 0);  break;
                        case ASR_CMD_LEFT:     Power_Execute(POWER_ACT_LEFT, 0);  break;
                        case ASR_CMD_RIGHT:    Power_Execute(POWER_ACT_RIGHT, 0); break;
                        case ASR_CMD_STOP:     Power_Execute(POWER_ACT_STOP, 0);  break;
                        /* 单电机命令（静默执行）*/
                        case ASR_CMD_L_FWD:  Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_REV:  Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_STOP: Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_FWD:  Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_REV:  Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_STOP: Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        /* 模式切换命令 */
                        case ASR_CMD_ENTER_POWER:  SwitchMode(APP_MODE_POWER, 0);  break;
                        case ASR_CMD_ENTER_SENSOR: SwitchMode(APP_MODE_SENSOR, 0); break;
                        case ASR_CMD_ENTER_REMOTE: SwitchMode(APP_MODE_REMOTE, 0); break;
                        case ASR_CMD_ENTER_VOICE:  SwitchMode(APP_MODE_VOICE, 0);  break;
                        default: break;
                        }
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
                /* 帧有效 */
                if (g_mode == APP_MODE_REMOTE) {
                    uint8_t *keys = &stream[i + 5];
                    /* 肩键调速（边沿触发）：R1=速度+，L1=速度- */
                    if (keys[REMOTE_KEY_R1] && !g_r1_was) {
                        if (g_remote_speed < MOTOR_SPEED_HIGH) g_remote_speed++;
                    }
                    if (keys[REMOTE_KEY_L1] && !g_l1_was) {
                        if (g_remote_speed > MOTOR_SPEED_LOW) g_remote_speed--;
                    }
                    g_r1_was = keys[REMOTE_KEY_R1];
                    g_l1_was = keys[REMOTE_KEY_L1];
                    /* 方向键优先（坦克转向），否则单电机键 */
                    if (keys[REMOTE_KEY_UP]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                    } else if (keys[REMOTE_KEY_DOWN]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                    } else if (keys[REMOTE_KEY_LEFT]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                    } else if (keys[REMOTE_KEY_RIGHT]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                    } else {
                        /* 单电机：Y=L正转 A=L反转 X=R正转 B=R反转 */
                        if (keys[REMOTE_KEY_Y])      Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        else if (keys[REMOTE_KEY_A]) Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        else                         Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP,     g_remote_speed);
                        if (keys[REMOTE_KEY_X])      Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                        else if (keys[REMOTE_KEY_B]) Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                        else                         Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP,     g_remote_speed);
                    }
                    g_last_remote_frame = Bsp_Tick_GetMs();
                }
                i += REMOTE_FRAME_LEN;
            }
            if (i > 0) {
                uint16_t remain = stream_len - i;
                for (uint16_t k = 0; k < remain; k++) stream[k] = stream[i + k];
                stream_len = remain;
            }
        }

        /* --- PA9 心跳呼吸灯（0→100→0 匀速，周期 2s）--- */
        if (Bsp_Tick_GetMs() - g_breath_t >= 10) {
            g_breath_t = Bsp_Tick_GetMs();
            if (g_breath_dir) {
                g_breath_val++;
                if (g_breath_val >= 100) { g_breath_val = 100; g_breath_dir = 0; }
            } else {
                g_breath_val--;
                if (g_breath_val <= 0) { g_breath_val = 0; g_breath_dir = 1; }
            }
            Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)g_breath_val);
        }

        /* --- 遥控模式超时停机（1s 没收到帧才停，防断连电机狂转；
               正常遥控器持续发帧间隔远小于 1s，不会误触发）--- */
        if (g_mode == APP_MODE_REMOTE &&
            g_last_remote_frame != 0 &&
            (Bsp_Tick_GetMs() - g_last_remote_frame > 1000)) {
            Bsp_Motor_StopAll();
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
