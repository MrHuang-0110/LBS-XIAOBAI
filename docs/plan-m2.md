# M2 实施计划:动力模式 + 语音模式 + 电机速度档位

> 在 BSP 层(已完成)之上,把"积木"接起来让产品核心交互可用。
> 范围:动力模式、语音模式、电机 3 档速度。感应/遥控模式留 M3。

## 目标

1. `Bsp_Motor` 加速度档位 API(1=40% / 2=70% / 3=100%)
2. 动力模式:短按 KEY1 循环切动作(停止→前进→后退→左转→右转),每动作播报
3. 语音模式:响应 `cmd=XX` 驱动电机/切模式
4. 模式切换统一函数(按键/语音共用)

## 改动文件

### 1. `Bsp_Motor.h` / `Bsp_Motor.c` —— 加速度档位

**API 改动(向后兼容):**
```c
typedef enum {
    MOTOR_SPEED_LOW   = 1,   // 40%
    MOTOR_SPEED_MID   = 2,   // 70%
    MOTOR_SPEED_HIGH  = 3,   // 100%
} Bsp_Motor_Speed_t;

// 新增 speed 参数
void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed);
```

**实现:** 把固定 `MOTOR_DUTY_90` 改成按 speed 选 duty:
- LOW = 40% × 2400 = 960
- MID = 70% × 2400 = 1680
- HIGH = 100% × 2400 = 2400(原 90% 是 2160)

`Motor_Drive` 里按 speed 查表得 duty,其余逻辑不变。`Bsp_Motor_StopAll` 不变(停机不涉及速度)。

### 2. `main.c` —— 业务逻辑重构

**新增模块级状态:**
```c
typedef enum {
    APP_MODE_VOICE=0, APP_MODE_POWER, APP_MODE_SENSOR, APP_MODE_REMOTE, APP_MODE_COUNT
} App_Mode_t;
App_Mode_t g_mode;          // 当前模式
```

**新增统一函数:**

```c
// 切模式:停电机 + 播报 + 点 LED
static void SwitchMode(App_Mode_t new_mode) {
    Bsp_Motor_StopAll();
    g_mode = new_mode;
    Bsp_Led_AllOff();
    Bsp_Led_On(mode_led[g_mode]);
    // 播报对应模式语音
    static const uint8_t mode_voice[4] = {
        ASR_VOICE_ENTER_VOICE, ASR_VOICE_ENTER_POWER,
        ASR_VOICE_ENTER_SENSOR, ASR_VOICE_ENTER_REMOTE
    };
    Bsp_UartAsr_SendPlay(mode_voice[g_mode]);
}

// 动力模式执行动作(停止/前进/后退/左转/右转)
typedef enum {
    POWER_ACT_STOP=0, POWER_ACT_FWD, POWER_ACT_BACK, POWER_ACT_LEFT, POWER_ACT_RIGHT, POWER_ACT_COUNT
} Power_Action_t;
static void Power_Execute(Power_Action_t act) {
    switch (act) {
    case POWER_ACT_STOP:  Bsp_Motor_Set(MOTOR_LEFT, MOTOR_DIR_STOP, MOTOR_SPEED_HIGH);
                          Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP, MOTOR_SPEED_HIGH); break;
    case POWER_ACT_FWD:   Bsp_Motor_Set(MOTOR_LEFT, MOTOR_DIR_FORWARD, MOTOR_SPEED_HIGH);
                          Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_HIGH); break;
    case POWER_ACT_BACK:  Bsp_Motor_Set(MOTOR_LEFT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH);
                          Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
    case POWER_ACT_LEFT:  Bsp_Motor_Set(MOTOR_LEFT, MOTOR_DIR_STOP, MOTOR_SPEED_HIGH);
                          Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD, MOTOR_SPEED_HIGH); break;
    case POWER_ACT_RIGHT: Bsp_Motor_Set(MOTOR_LEFT, MOTOR_DIR_FORWARD, MOTOR_SPEED_HIGH);
                          Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP, MOTOR_SPEED_HIGH); break;
    }
    // 播报动作语音
    static const uint8_t act_voice[5] = {
        ASR_VOICE_STOP, ASR_VOICE_FORWARD, ASR_VOICE_BACKWARD, ASR_VOICE_LEFT, ASR_VOICE_RIGHT
    };
    Bsp_UartAsr_SendPlay(act_voice[act]);
}
```

**按键处理(主循环):**
```c
Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
if (ke == KEY_EVT_SHORT && kid == KEY_ID_1) {
    if (g_mode == APP_MODE_POWER) {
        // 动力模式:短按切动作
        g_power_action = (g_power_action + 1) % POWER_ACT_COUNT;
        Power_Execute(g_power_action);
    } else {
        // 其它模式:短按切模式
        SwitchMode((g_mode + 1) % APP_MODE_COUNT);
    }
}
else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
    // 关机流程(保留 Task 14 的逻辑)
    ...
}
```

**语音命令处理(主循环 ASRPRO 事件):**
```c
case ASR_EVT_CMD:
    switch (e.arg) {
    case ASR_CMD_FORWARD:  // 30
        Power_Execute(POWER_ACT_FWD); break;
    case ASR_CMD_BACKWARD: // 31
        Power_Execute(POWER_ACT_BACK); break;
    case ASR_CMD_LEFT:     // 32
        Power_Execute(POWER_ACT_LEFT); break;
    case ASR_CMD_RIGHT:    // 33
        Power_Execute(POWER_ACT_RIGHT); break;
    case ASR_CMD_STOP:     // 34
        Power_Execute(POWER_ACT_STOP); break;
    case ASR_CMD_ENTER_POWER:  // 50
        SwitchMode(APP_MODE_POWER); break;
    case ASR_CMD_ENTER_SENSOR: // 51
        SwitchMode(APP_MODE_SENSOR); break;
    case ASR_CMD_ENTER_REMOTE: // 52
        SwitchMode(APP_MODE_REMOTE); break;
    case ASR_CMD_ENTER_VOICE:  // 53
        SwitchMode(APP_MODE_VOICE); break;
    // 单电机 cmd=40..45:M3 再接(遥控模式一起)
    }
    break;
```

**注意:**
- 语音命令在**任何模式**都响应(文档§12:语音模式响应具体控制,但模式切换语音任何模式都响应)。简化处理:动作类命令(30-34)任何模式都执行电机;模式切换命令(50-53)任何模式都切。
- `g_power_action` 记录动力模式当前动作,切回动力模式时恢复该动作。

### 3. 清理临时调试代码

- **删 TM1640 亮灭测试**(主循环那段)——M2 让 TM1640 显示模式相关图案,不再做亮灭测试
- **删遥控帧解析的 FlashLeds 演示**(M3 遥控模式再接)
- **保留** BLE 连接/断开语音播报(PF3 边沿)

## 实施步骤(TDD 心态,每步可验证)

### Step 1: Bsp_Motor 加速度档位
- 改 `Bsp_Motor.h`:加 `Bsp_Motor_Speed_t` 枚举,`Bsp_Motor_Set` 加 speed 参数
- 改 `Bsp_Motor.c`:`Motor_Drive` 按 speed 选 duty,3 档查表
- **验证**:main.c 里所有 `Bsp_Motor_Set` 调用要补 speed 参数(关机流程那处),编译过

### Step 2: 模式状态机 + SwitchMode
- main.c 加 `g_mode` 全局变量、`SwitchMode` 函数
- 开机后默认 `APP_MODE_VOICE`,调 `SwitchMode(APP_MODE_VOICE)` 播开机语(替换原来手写的 SendPlay)
- 短按 KEY1:非动力模式切模式,动力模式切动作
- **验证**:短按 KEY1 能切模式 + 播报 + LED 切换

### Step 3: 动力模式 Power_Execute
- main.c 加 `g_power_action` 变量、`Power_Execute` 函数
- 动力模式下短按 KEY1 循环切动作 + 电机执行 + 播报
- **验证**:进动力模式后,短按能循环 停→进→退→左→右,电机动 + 语音播报

### Step 4: 语音命令响应
- 主循环 ASR_EVT_CMD 分支:动作命令(30-34)调 Power_Execute,模式命令(50-53)调 SwitchMode
- **验证**:说"小白前进"→ 电机前进 + 播报;说"进入动力模式"→ 切动力模式 + 播报

### Step 5: 清理调试代码
- 删 TM1640 亮灭测试段
- 删遥控帧 FlashLeds 演示(保留流缓冲解析框架,M3 用)
- **验证**:编译过,整机功能不退化

### Step 6: Commit + push(每步可单独 commit)

## 待澄清/留 M3

- 感应模式:等红外硬件修好
- 遥控模式:电机档位 API 已就绪,M3 接遥控器按键 → 电机 + 肩键调速
- 单电机命令(cmd=40..45):M3 跟遥控模式一起
- TM1640 眼睛动画:M2 先显示静态图案(如模式对应表情),动画留后续
- 低电量告警:等电池电压换算,M3/M4
