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

/* 感应模式的 4 个玩法（文档 §9）*/
typedef enum {
    SENSOR_PLAY_APPROACH   = 0,  /* 靠近启动 */
    SENSOR_PLAY_OBSTACLE   = 1,  /* 遇障停止 */
    SENSOR_PLAY_WAVE       = 2,  /* 挥手开关 */
    SENSOR_PLAY_BRIGHTNESS = 3,  /* 明暗调速 */
    SENSOR_PLAY_COUNT
} Sensor_Play_t;

/* 红外反射阈值：ADC < 3000 = 有反射（遮挡时~200，无反射~4000）。
   方案A硬编码，量产不需要用户校准。 */
#define IR_THRESHOLD  3000U

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
/* 感应玩法 -> 语音 ID（文档 §7）*/
static const uint8_t sensor_voice[SENSOR_PLAY_COUNT] = {
    ASR_VOICE_APPROACH_GO, ASR_VOICE_OBSTACLE_STOP,
    ASR_VOICE_WAVE_TOGGLE, ASR_VOICE_BRIGHTNESS,
};
/* 语音命令 ID (6..16) → 播报 ID (27..31 / 36..41) 映射。
   索引 = cmd_id - ASR_CMD_FORWARD (6)。宏值由 v0.7 协议 §三/§四 决定；
   若协议再调 ID，此表须同步核对。 */
static const uint8_t cmd_to_voice[11] = {
    ASR_VOICE_FORWARD,   /* cmd=6  → play=27 */
    ASR_VOICE_BACKWARD,  /* cmd=7  → play=28 */
    ASR_VOICE_LEFT,      /* cmd=8  → play=29 */
    ASR_VOICE_RIGHT,     /* cmd=9  → play=30 */
    ASR_VOICE_STOP,      /* cmd=10 → play=31 */
    ASR_VOICE_L_FWD,     /* cmd=11 → play=36 */
    ASR_VOICE_L_REV,     /* cmd=12 → play=37 */
    ASR_VOICE_L_STOP,    /* cmd=13 → play=38 */
    ASR_VOICE_R_FWD,     /* cmd=14 → play=39 */
    ASR_VOICE_R_REV,     /* cmd=15 → play=40 */
    ASR_VOICE_R_STOP,    /* cmd=16 → play=41 */
};

/* ===== TM1640 眼睛图案（8×14 点阵，左眼列0-6 / 右眼列7-13，各 7×8）=====
   椭圆形空心轮廓，眨眼=行3一条横线。 */
static const uint8_t eye_box[14] = {
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C,   /* 左眼 列0-6 椭圆轮廓 */
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C    /* 右眼 列7-13 椭圆轮廓 */
};
static const uint8_t eye_closed[14] = {
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,   /* 左眼闭 列0-6 行3 */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08    /* 右眼闭 列7-13 行3 */
};
/* 未连接：双眨（睁2s → 闭100ms → 睁150ms → 闭100ms → 睁2s）*/
static const struct { uint16_t ms; uint8_t closed; } blink_idle[] = {
    {2000, 0}, {100, 1}, {150, 0}, {100, 1}, {2000, 0}
};
#define BLINK_IDLE_LEN (sizeof(blink_idle)/sizeof(blink_idle[0]))
/* 已连接：瞳孔移动（看左上 → 回中 → 看右上 → 回中）。
   瞳孔 3×3 实心方块：上方=行1-3(bit0x0E)，中=行3-5(bit0x38)，叠加 3 列宽。 */
static const uint8_t pupil_bit[3] = {0x0E, 0x38, 0x0E};
static const uint8_t pupil_lc[3]  = {1, 2, 4};       /* 左眼瞳孔起始列(0-6内) */
static const uint8_t pupil_rc[3]  = {8, 9, 11};      /* 右眼瞳孔起始列(7-13内) */
static const struct { uint16_t ms; uint8_t pos; } look_seq[] = {
    {500, 0}, {200, 1}, {500, 2}, {200, 1}
};
#define LOOK_LEN (sizeof(look_seq)/sizeof(look_seq[0]))

/* ===== 全局状态 ===== */
static App_Mode_t      g_mode;
static Power_Action_t  g_power_action = POWER_ACT_STOP;
static Sensor_Play_t   g_sensor_play = SENSOR_PLAY_APPROACH;
static uint8_t         g_wave_on = 0;            /* 挥手开关的当前开/关状态 */
static uint8_t         g_ir1_was = 0, g_ir2_was = 0, g_ir3_was = 0;  /* 挥手边沿检测 */
static uint8_t         g_mode_paused = 0;        /* 1=进入模式后暂停，第二次按键才启动 */

/* 遥控模式状态 */
static Bsp_Motor_Speed_t g_remote_speed = MOTOR_SPEED_MID;  /* 3 档速度，默认 2 档 70% */
static uint32_t          g_last_remote_frame = 0;           /* 超时停机计时 */
static uint8_t           g_r1_was = 0, g_l1_was = 0;        /* 肩键边沿检测 */

/* 呼吸灯状态（PA9，由 wake 唤醒启动，15s 超时自动关闭） */
static uint8_t  g_breathing = 0;       /* 1=呼吸中 0=关闭 */
static int16_t  g_breath_val = 0;
static uint8_t  g_breath_dir = 1;      /* 1=上升 0=下降 */
static uint32_t g_breath_t   = 0;      /* 呼吸动画步进时间戳 */
static uint32_t g_breath_start = 0;    /* 呼吸灯启动时刻，用于 15s 超时关灯 */

/* ===== 统一函数 ===== */

/* 低电量播报（带 30s 冷却，避免刷屏）。
   主循环周期检测：Bsp_Battery_IsLow() 为真时调这里，30s 播一次。 */
static void App_ReportLowBattery(void)
{
    static uint32_t last_report = 0;
    static uint8_t  reported = 0;
    uint32_t now = Bsp_Tick_GetMs();
    if (!reported || (now - last_report >= 30000)) {
        Bsp_UartAsr_SendPlay(ASR_VOICE_LOW_BATTERY);
        last_report = now;
        reported = 1;
    }
}

/* 完整关机流程：停电机 → 播关机语 → 关机动画 → 关 LED/TM1640 → 延时 1s → 断电。
   KEY1 长按和语音命令 cmd=1 共用。 */
static void PerformShutdown(void)
{
    Bsp_Motor_StopAll();
    Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
    Bsp_Power_ShutdownAnimation();
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
    Bsp_Tm1640_Clear();
    Bsp_Tick_DelayMs(1000);
    Bsp_Power_ShutDown();
}

/* 切模式：停电机 + 点 LED + 可选播报。
 * play_voice=1（按键 / 语音命令 cmd=2..5 / 模式内子动作）：播"进入 XX 模式"，
 *             与"MCU 实际动作 → SendPlay"统一规则一致。
 * play_voice=0（开机默认进入语音模式）：静默，开机语已由 Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT) 单独发，
 *             避免与 BOOT 合并句重复。 */
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
    if (new_mode == APP_MODE_SENSOR) {
        g_sensor_play = SENSOR_PLAY_APPROACH;  /* 进感应模式默认玩法1 */
        g_wave_on = 0;
        g_ir1_was = 0;
        g_ir3_was = 0;
    }
    if (new_mode == APP_MODE_POWER || new_mode == APP_MODE_SENSOR) {
        g_mode_paused = 1;   /* 进入模式后暂停，第二次按键才启动 */
    }
    if (play_voice) {
        Bsp_UartAsr_SendPlay(mode_voice[g_mode]);
        /* 不等 done，异步播报，保证按键灵敏（跟感应模式一致）*/
    }
}


/* TM1640 眼睛动画：椭圆空心轮廓。
   未连接 → 双眨（灵动）
   已连接 → 瞳孔移动看左上/右上（AI 生命力）
   非阻塞，主循环周期调用。 */
static void Eye_Update(void)
{
    static uint32_t last_t = 0;
    static uint8_t  frame = 0;
    static uint8_t  was_connected = 0xFF;
    uint8_t connected = Bsp_UartBle_IsConnected();
    uint32_t now = Bsp_Tick_GetMs();

    /* 连接状态切换时重置，立刻显示第一帧 */
    if (connected != was_connected) {
        last_t = now;
        frame = 0;
        was_connected = connected;
        if (connected) {
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[0] + c] |= pupil_bit[0];
                buf[pupil_rc[0] + c] |= pupil_bit[0];
            }
            Bsp_Tm1640_Refresh(buf);
        } else {
            Bsp_Tm1640_Refresh(eye_box);
        }
        return;
    }

    if (connected) {
        /* 瞳孔移动：看左上 → 回中 → 看右上 → 回中 */
        if (now - last_t >= look_seq[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % LOOK_LEN);
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            uint8_t p = look_seq[frame].pos;
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[p] + c] |= pupil_bit[p];
                buf[pupil_rc[p] + c] |= pupil_bit[p];
            }
            Bsp_Tm1640_Refresh(buf);
        }
    } else {
        /* 双眨 */
        if (now - last_t >= blink_idle[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % BLINK_IDLE_LEN);
            if (blink_idle[frame].closed) Bsp_Tm1640_Refresh(eye_closed);
            else                          Bsp_Tm1640_Refresh(eye_box);
        }
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
    /* v0.7 ID 17 = "你好呀我是小白进入语音模式"，本身就是合并句，
       单独播；SwitchMode 用 play_voice=0 静默切避免重复 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
    SwitchMode(APP_MODE_VOICE, 0);

    /* PA9 呼吸灯默认关闭，由 wake 唤醒启动 / 15s 超时自动关 */
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("LBS_XIAOBAI", 11);

    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    /* 遥控帧流缓冲（M3 遥控模式用，先保留解析框架） */
    static uint8_t stream[REMOTE_FRAME_LEN * 4];
    uint16_t stream_len = 0;

    /* 动作命令防抖已移除：v0.7 下 ASRPRO 侧不误连发，每条 cmd 都执行。
       依赖 ASRPRO 侧命令冗余抑制；如未来发现误识别，在此处重新加防抖窗。 */

    while (1) {
        /* --- 按键扫描（4 键各管一个模式，KEY1 长按关机） --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT) {
                switch (kid) {
                case KEY_ID_1: SwitchMode(APP_MODE_VOICE, 1);  break;  /* LED1 */
                case KEY_ID_2:  /* LED2 感应模式：已在感应模式则切玩法 */
                    if (g_mode == APP_MODE_SENSOR) {
                        if (g_mode_paused) {
                            g_mode_paused = 0;   /* 第一次按键：启动第一个玩法 */
                            Bsp_UartAsr_SendPlay(sensor_voice[g_sensor_play]);
                        } else {
                            g_sensor_play = (Sensor_Play_t)((g_sensor_play + 1) % SENSOR_PLAY_COUNT);
                            Bsp_Motor_StopAll();
                            g_wave_on = 0;
                            Bsp_UartAsr_SendPlay(sensor_voice[g_sensor_play]);
                        }
                    } else {
                        SwitchMode(APP_MODE_SENSOR, 1);
                    }
                    break;
                case KEY_ID_3: SwitchMode(APP_MODE_REMOTE, 1); break;  /* LED3 */
                case KEY_ID_4:  /* LED4 动力模式：已在动力模式则切动作 */
                    if (g_mode == APP_MODE_POWER) {
                        if (g_mode_paused) {
                            g_mode_paused = 0;                       /* 第一次按键：启动 */
                            g_power_action = POWER_ACT_FWD;          /* 从前进开始 */
                        } else {
                            g_power_action = (Power_Action_t)((g_power_action + 1) % POWER_ACT_COUNT);
                        }
                        Bsp_Motor_StopAll();
                        Bsp_UartAsr_SendPlay(act_voice[g_power_action]);
                         
                    } else {
                        SwitchMode(APP_MODE_POWER, 1);
                    }
                    break;
                default: break;
                }
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                PerformShutdown();
            }
        }

        /* --- ASRPRO 语音命令 --- */
        {
            Bsp_UartAsr_Event_t e;
            if (Bsp_UartAsr_TryRecv(&e)) {
                if (e.type == ASR_EVT_CMD) {
                    /* 段1: 任何模式响应（关机） */
                    if (e.arg == ASR_CMD_SHUTDOWN) {
                        PerformShutdown();
                    }
                    /* 段2: 任何模式响应（切模式 4 选 1） */
                    else if (e.arg >= ASR_CMD_ENTER_POWER && e.arg <= ASR_CMD_ENTER_VOICE) {
                        switch (e.arg) {
                        case ASR_CMD_ENTER_POWER:  SwitchMode(APP_MODE_POWER, 1);  break;
                        case ASR_CMD_ENTER_SENSOR: SwitchMode(APP_MODE_SENSOR, 1); break;
                        case ASR_CMD_ENTER_REMOTE: SwitchMode(APP_MODE_REMOTE, 1); break;
                        case ASR_CMD_ENTER_VOICE:  SwitchMode(APP_MODE_VOICE, 1);  break;
                        }
                    }
                    /* 段3: 仅语音模式响应（动作命令 11 条） */
                    else if (g_mode == APP_MODE_VOICE &&
                             e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP) {
                        /* 统一回播规则：MCU 实际动作 → 对应播报语 */
                        Bsp_UartAsr_SendPlay(cmd_to_voice[e.arg - ASR_CMD_FORWARD]);
                        switch (e.arg) {
                        case ASR_CMD_FORWARD:
                            Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
                            Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
                            break;
                        case ASR_CMD_BACKWARD:
                            Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                            Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                            break;
                        case ASR_CMD_LEFT:
                            Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                            Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
                            break;
                        case ASR_CMD_RIGHT:
                            Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
                            Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                            break;
                        case ASR_CMD_STOP:
                            Bsp_Motor_StopAll();
                            break;
                        case ASR_CMD_L_FWD:  Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_REV:  Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_STOP: Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_FWD:  Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_REV:  Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_STOP: Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        default: break;
                        }
                    }
                }
                else if (e.type == ASR_EVT_WAKE) {
                    /* 唤醒：启动呼吸灯（15s 后主循环超时自动关）。
                       不再发 play=10，唤醒应答由 ASRPRO 本地回复词"我在"承担，响应更快 */
                    g_breathing = 1;
                    g_breath_val = 0;
                    g_breath_dir = 1;
                    g_breath_t = Bsp_Tick_GetMs();
                    g_breath_start = g_breath_t;
                }
                else if (e.type == ASR_EVT_DONE) {
                    /* done=NN：播报完成回执，仅供参考，无需动作 */
                }
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

        /* --- TM1640 眼睛动画（未连接慢闪 / 连接后转动）--- */
        Eye_Update();

        /* --- PA9 呼吸灯：wake 唤醒启动，15s 超时自动关 --- */
        if (g_breathing && (Bsp_Tick_GetMs() - g_breath_start >= 15000)) {
            g_breathing = 0;
            g_breath_val = 0;
            g_breath_dir = 1;
            Bsp_LedPwm_Set(LEDPWM_2, 0);
        }
        if (g_breathing && (Bsp_Tick_GetMs() - g_breath_t >= 10)) {
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

        /* --- 动力模式电机驱动（主循环持续驱动，跟感应模式架构一致）--- */
        if (g_mode == APP_MODE_POWER && !g_mode_paused) {
            switch (g_power_action) {
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
                Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
                break;
            case POWER_ACT_RIGHT:
                Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
                Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                break;
            }
        }

        /* --- 感应模式执行（文档 §9）--- */
        if (g_mode == APP_MODE_SENSOR && !g_mode_paused) {
            uint16_t ir1 = Bsp_IR_ReadCh1();
            uint16_t ir2 = Bsp_IR_ReadCh2();
            uint16_t ir3 = Bsp_IR_ReadCh3();
            uint8_t ir1_trig = (ir1 < IR_THRESHOLD);  /* 有反射=遮挡 */
            uint8_t ir2_trig = (ir2 < IR_THRESHOLD);
            uint8_t ir3_trig = (ir3 < IR_THRESHOLD);

            switch (g_sensor_play) {
            case SENSOR_PLAY_APPROACH:
                /* 靠近启动：有物体前进，无物体停 */
                if (ir2_trig) {
                    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                } else {
                    Bsp_Motor_StopAll();
                }
                break;
            case SENSOR_PLAY_OBSTACLE:
                /* 遇障停止：前进，遇障碍停 */
                if (ir2_trig) {
                    Bsp_Motor_StopAll();
                } else {
                    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                }
                break;
            case SENSOR_PLAY_WAVE:
                /* 挥手开关：IR1/IR2/IR3 任一检测到手（下降沿）→ 切换，500ms 消抖 */
                {
                    static uint32_t last_wave = 0;
                    uint8_t any_edge = (ir1_trig && !g_ir1_was) ||
                                       (ir2_trig && !g_ir2_was) ||
                                       (ir3_trig && !g_ir3_was);
                    if (any_edge && (Bsp_Tick_GetMs() - last_wave > 500)) {
                        g_wave_on = !g_wave_on;
                        last_wave = Bsp_Tick_GetMs();
                    }
                }
                if (g_wave_on) {
                    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                } else {
                    Bsp_Motor_StopAll();
                }
                break;
            case SENSOR_PLAY_BRIGHTNESS:
                /* 明暗调速：反射越强（值越小）速度越快 */
                if (ir2 < 500) {
                    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD, MOTOR_SPEED_HIGH);
                    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_HIGH);
                } else if (ir2 < 1000) {
                    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_MID);
                } else if (ir2 < IR_THRESHOLD) {
                    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD, MOTOR_SPEED_LOW);
                    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_LOW);
                } else {
                    Bsp_Motor_StopAll();
                }
                break;
            }
            g_ir1_was = ir1_trig;
            g_ir2_was = ir2_trig;
            g_ir3_was = ir3_trig;
        }

        /* --- 遥控模式超时停机（1s 没收到帧才停，防断连电机狂转；
               正常遥控器持续发帧间隔远小于 1s，不会误触发）--- */
        if (g_mode == APP_MODE_REMOTE &&
            g_last_remote_frame != 0 &&
            (Bsp_Tick_GetMs() - g_last_remote_frame > 1000)) {
            Bsp_Motor_StopAll();
        }

        /* --- 电池采样：10ms 一次入滤波窗口 --- */
        {
            static uint32_t last_batt = 0;
            if (Bsp_Tick_GetMs() - last_batt >= 10) {
                last_batt = Bsp_Tick_GetMs();
                Bsp_Battery_Poll();
            }
        }

        /* --- 低电量周期播报：滤波+迟滞后仍处于低电量态，由 App_ReportLowBattery 的
               30s 冷却控制播报频率；电压恢复到非低电量态后，reported 保持 1，等 30s
               冷却期过后若再进入低电量能立刻播（可接受，只在真的抖回来才响）。 */
        if (Bsp_Battery_IsLow()) {
            App_ReportLowBattery();
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
