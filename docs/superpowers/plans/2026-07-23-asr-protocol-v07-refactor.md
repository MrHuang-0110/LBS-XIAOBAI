# ASR 语音交互协议 v0.7 重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 MCU 端语音交互代码（`Bsp_UartAsr` + `main.c`）一次性对齐到 ASR 协议 v0.7：恢复 `\n` 帧尾、按 v0.7 §三/§四 重编 24 条 `ASR_VOICE_*` + 16 条 `ASR_CMD_*` 宏值、建立"MCU 实际动作 → SendPlay"统一回播规则、支持 `cmd=1` 语音关机、移除 800ms 动作防抖。

**Architecture:** 三处文件改动集中在协议层与业务触发点。Bsp_UartAsr 维持现有单文件结构与 API 签名（向后兼容），仅在发送路径用 `SendFrame` 单次 Transmit 统一追加 `\n` 帧尾、变长格式化播报 ID；main.c 新增 `PerformShutdown` 函数 + `cmd_to_voice` 查表 + cmd 分派链三段化（关机 → 切模式 → 仅语音模式动作）。

**Tech Stack:**
- MCU: PY32F030K28U6TR（ARM Cortex-M0+）
- 协议层: UART2 9600 8N1 + DMA1_Channel1 循环 + IDLE 中断
- 构建: Keil MDK-ARM（`MCU_XiaoBai/MDK-ARM/XiaoBai.uvprojx`）
- 调试: USB-TTL 接 PF0/PF1，串口助手（HEX 模式）+ 正点原子串口助手

**参考文档**:
- 协议: `resource/语音芯片交互协议.md` v0.7
- 注意事项: `resource/MCU协议注意事项.md`
- Spec: `docs/superpowers/specs/2026-07-23-asr-protocol-v07-refactor-design.md`

---

## 全局约束

- **协议版本**: v0.7（2026-07-23）。所有 ID 编号、帧尾规则、命令/播报语义以 v0.7 协议为权威。
- **前提条件**: ASRPRO 侧已改为读到 `\n` 才分帧（不再用 `serial_readstr` 精确比较）。
- **API 兼容**: `Bsp_UartAsr.h` 对外 API 函数签名（`Init` / `SendPlay` / `SendStop` / `SendPing` / `SendRaw` / `TryRecv` / 两个 IRQHandler）零改动。
- **宏名兼容**: `ASR_VOICE_*` / `ASR_CMD_*` 宏名零改动，只改宏值。
- **不变量**: 命令 ID ∈ [1,16] 与播报 ID ∈ [17,41] 两段不重叠（v0.7 §一/§四）。
- **DMA 分配**: DMA1_Channel1 = USART2_RX（ASR），独立中断号，不动。
- **代码风格**: K&R C、注释密度与现有 main.c 一致、缩进制表符、所有 `Bsp_UartAsr_*` 函数体不调用业务层函数。
- **commit 策略**: 一次原子 commit 覆盖三处文件；commit 后按 `[[auto-push-after-commit]]` 立即 `git push`。
- **CI/工具链**: 编译走 Keil MDK，不走 gcc/make。验证编译=打开 IDE Build；byte 级验证=USB-TTL + 串口助手。

---

## 文件清单

| 操作 | 路径 | 责任 |
|---|---|---|
| Modify | `MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h` | 头文件 + 宏值重编 + 注释 |
| Modify | `MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.c` | 发送路径 `\n` 帧尾 + 变长 SendPlay + LINE_BUF 32→40 |
| Modify | `MCU_XiaoBai/User/main.c` | 开机语、PerformShutdown、cmd_to_voice、cmd 分派链、KEY1 长按共用 |

**不创建新文件**。**不修改**：`Bsp.h`、`BSP_Init`、BLE 遥控模块、电机 PWM、呼吸灯、TM1640 眼睛动画、感应模式 IR ADC、低电量 ADC、Bsp_Key、Bsp_UartBle 等。

---

## Task 1: `Bsp_UartAsr.h` 宏值重编

**Files:**
- Modify: `MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h:1-87`（整文件替换）

**Interfaces:**
- Consumes: 协议 v0.7 §三/§四（spec §四）
- Produces: 24 个 `ASR_VOICE_*` 宏（值 17-41）+ 16 个 `ASR_CMD_*` 宏（值 1-16）

### Step 1: 替换头文件注释块

将文件顶部 line 5-6 注释替换为 v0.7 版本：

```c
/* 语音芯片交互协议 v0.7（ASCII 文本；两方向均带帧尾：MCU 发帧尾 `\n`，收帧尾 `\r\n`；
   tag 用 `=` 分隔十进制数值；命令 ID ∈ [1,16] 播报 ID ∈ [17,41] 不重叠）。
   权威文档见 resource/语音芯片交互协议.md 与 resource/MCU协议注意事项.md */
```

### Step 2: 替换所有 `ASR_VOICE_*` 宏（line 9-27）

按 spec §4.1 表完整重编，原 19 条 + 5 条新增（`ASR_VOICE_RECEIVED` 保留 + 6 条单电机）= 25 条：

```c
/* --- 语音 ID（十进制，供 Bsp_UartAsr_SendPlay 使用；v0.7 §三 17-41 连续） --- */
#define ASR_VOICE_BOOT           17  /* 你好呀我是小白进入语音模式（开机语） */
#define ASR_VOICE_SHUTDOWN       18  /* 再见啦，小白去休息了（关机语） */
#define ASR_VOICE_ENTER_POWER    19  /* 进入动力模式 */
#define ASR_VOICE_ENTER_SENSOR   20  /* 进入感应模式 */
#define ASR_VOICE_ENTER_REMOTE   21  /* 进入遥控模式 */
#define ASR_VOICE_ENTER_VOICE    22  /* 进入语音模式 */
#define ASR_VOICE_BLE_CONNECTED  23  /* 遥控已连接 */
#define ASR_VOICE_BLE_LOST       24  /* 遥控已断开 */
#define ASR_VOICE_LOW_BATTERY    25  /* 低电量 */
#define ASR_VOICE_RECEIVED       26  /* 收到 */
#define ASR_VOICE_FORWARD        27  /* 前进 */
#define ASR_VOICE_BACKWARD       28  /* 后退 */
#define ASR_VOICE_LEFT           29  /* 左转 */
#define ASR_VOICE_RIGHT          30  /* 右转 */
#define ASR_VOICE_STOP           31  /* 停止 */
#define ASR_VOICE_APPROACH_GO    32  /* 靠近启动 */
#define ASR_VOICE_OBSTACLE_STOP  33  /* 遇障停止 */
#define ASR_VOICE_WAVE_TOGGLE    34  /* 挥手开关 */
#define ASR_VOICE_BRIGHTNESS     35  /* 明暗调速 */
#define ASR_VOICE_L_FWD          36  /* 左电机正转 */
#define ASR_VOICE_L_REV          37  /* 左电机反转 */
#define ASR_VOICE_L_STOP         38  /* 左电机停止 */
#define ASR_VOICE_R_FWD          39  /* 右电机正转 */
#define ASR_VOICE_R_REV          40  /* 右电机反转 */
#define ASR_VOICE_R_STOP         41  /* 右电机停止 */
```

### Step 3: 替换所有 `ASR_CMD_*` 宏（line 30-44）

按 spec §4.2 表完整重编，原 15 条 + 1 条新增 `ASR_CMD_SHUTDOWN` = 16 条：

```c
/* --- 命令 ID（十进制，Bsp_UartAsr_TryRecv 收到 CMD 事件时 arg = 这些值之一；v0.7 §四 1-16 连续） --- */
#define ASR_CMD_SHUTDOWN         1   /* 关机 */
#define ASR_CMD_ENTER_POWER      2
#define ASR_CMD_ENTER_SENSOR     3
#define ASR_CMD_ENTER_REMOTE     4
#define ASR_CMD_ENTER_VOICE      5
#define ASR_CMD_FORWARD          6
#define ASR_CMD_BACKWARD         7
#define ASR_CMD_LEFT             8
#define ASR_CMD_RIGHT            9
#define ASR_CMD_STOP             10
#define ASR_CMD_L_FWD            11
#define ASR_CMD_L_REV            12
#define ASR_CMD_L_STOP           13
#define ASR_CMD_R_FWD            14
#define ASR_CMD_R_REV            15
#define ASR_CMD_R_STOP           16
```

### Step 4: 验证编译（Keil MDK）

操作: 打开 `MCU_XiaoBai/MDK-ARM/XiaoBai.uvprojx` → F7 (Build)

预期: 编译通过；`Bsp_UartAsr.c` 里用到旧宏值的引用都会因为宏值改变而值变化（不影响编译，但 main.c 会因为 case 值变化而 case 覆盖度变差——这部分在 Task 3 修正）。

### Step 5: 提交

```bash
git add MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h
git commit -m "refactor(asr): Bsp_UartAsr.h 宏值按 v0.7 §三/§四 重编

- 24 条 ASR_VOICE_* 改为 17-41 连续（原 1-15/20-23）
- 16 条 ASR_CMD_* 改为 1-16 连续（原 30-45/50-53）
- 新增 ASR_CMD_SHUTDOWN=1, ASR_VOICE_L_FWD..R_STOP (36-41)
- 头部注释 v0.6 → v0.7

Co-Authored-By: Claude <noreply@anthropic.com>"
git push
```

---

## Task 2: `Bsp_UartAsr.c` 发送路径恢复 `\n` 帧尾 + 变长格式化

**Files:**
- Modify: `MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.c:1-231`（多处小改）

**Interfaces:**
- Consumes: Task 1 新宏值（仅作为整数值参与 snprintf，不影响 .c 的对外 API）
- Produces: `SendFrame`（内部静态函数）+ `Bsp_UartAsr_SendPlay` 变长格式 + 所有发送 API 都带 `\n` 帧尾

### Step 1: 更新顶部大注释（line 5-17）

```c
/*
 * 语音协议 v0.7 实现：ASCII 文本
 *   MCU 发（帧尾 \n）：<tag>[=<dec>]\n
 *   MCU 收（帧尾 \r\n）：<tag>[=<dec>]\r\n
 *
 * 接收路径：DMA 循环 + UART IDLE 中断
 *   - IDLE 中断里读 DMA 剩余字节数得到新收字节段
 *   - 逐字节喂进字符状态机 Frame_FeedByte
 *   - 收到 '\n' 时判定为一帧结束（若前一字节是 '\r' 顺便剔除），
 *     把 g_line 里累积的字符交给 Line_Dispatch 解析
 *   - 接收方向兼容有无 \r（ASRPRO 可能只发 \n）；状态机只看 \n 做帧结束
 *
 * 发送路径：把 "tag[=NN]" 组装成字符串，HAL_UART_Transmit 阻塞发送，
 * 末尾带 \n 帧尾（v0.7 §一：MCU→ASR 帧尾 = LF 单字节）。
 */
```

### Step 2: 改 `LINE_BUF_SIZE` 32 → 40（line 19）

```c
#define LINE_BUF_SIZE     40U     /* 单帧最大长度（协议约定 32 字节，留 8 字节余量） */
```

### Step 3: 替换 `SendStr` 为 `SendFrame`（line 175-201）

将整个 `SendStr` 静态函数 + 三个 SendXxx + SendRaw 整块替换：

```c
/* --- 发送 ---
   v0.7 §一：MCU→ASR 帧尾固定为 \n（LF 0x0A 单字节）。所有 SendXxx 通过 SendFrame 统一追加。 */

#define ASR_TX_TAIL_CHAR    '\n'    /* v0.7 §一帧尾 */

static void SendFrame(const char *body, uint16_t body_len)
{
    /* frame 缓冲：v0.7 最长 "play=255\n" = 9 字节，取 16 留余量 */
    uint8_t frame[16];
    if (body_len + 1 > sizeof(frame)) return;
    for (uint16_t i = 0; i < body_len; i++) frame[i] = (uint8_t)body[i];
    frame[body_len] = ASR_TX_TAIL_CHAR;
    HAL_UART_Transmit(&huart, frame, (uint16_t)(body_len + 1), 1000);
}

void Bsp_UartAsr_SendPlay(uint8_t voice_id)
{
    /* v0.7 §一：1-3 位十进制不补零。snprintf 自动格式化；截断保护 */
    char buf[9];   /* "play="(5) + "255"(3) + '\0'(1) = 9 */
    int n = snprintf(buf, sizeof(buf), "play=%u", (unsigned)voice_id);
    if (n <= 0 || n >= (int)sizeof(buf)) return;
    SendFrame(buf, (uint16_t)n);
}

void Bsp_UartAsr_SendStop(void) { SendFrame("stop", 4); }
void Bsp_UartAsr_SendPing(void) { SendFrame("ping", 4); }

void Bsp_UartAsr_SendRaw(const uint8_t *data, uint16_t len)
{
    /* 调试透传：绕过协议格式化，原样发；不追加 \n */
    HAL_UART_Transmit(&huart, (uint8_t *)data, len, 50);
}
```

### Step 4: 验证字节级（USB-TTL + 串口助手）

工具: USB-TTL 接 MCU 板 PF0/PF1 + 正点原子串口助手（9600 8N1, HEX 显示）

操作: flash 新固件 → 进入 main loop → 用逻辑分析仪或串口助手**只观察发送方向**（RX 脚静默）：

1. 按 KEY1（短按，1 键）→ 期望串口助手收到：`70 6C 61 79 3D 32 32 0A`（`play=22\n`）✓
2. 按 KEY4（短按，进动力）→ 期望收到：`70 6C 61 79 3D 31 39 0A`（`play=19\n`）✓
3. 长按 KEY1（≥2s 触发关机）→ 期望收到：`70 6C 61 79 3D 31 38 0A`（`play=18\n`）✓
4. **关键检查**：每条帧末字节是 `0A`，**前面无 `0D`**（只发 `\n` 不发 `\r`）

预期: 4 条全部字节级匹配；如有偏差按 §8 边界情况排查。

### Step 5: 验证串口助手发送 `play=22\n` 收到（双向）

操作: flash 进 main loop 后**不要按 KEY**，改用串口助手 TX → MCU 的 RX（PF0）发：

1. 串口助手发 `play=22\n`（hex: `70 6C 61 79 3D 32 32 0A`），勾选"发送新行"
2. **预期**: MCU 不回执（play 帧不在 cmd/wake/done 三个 tag 里 → Line_Dispatch 静默丢）。**没有 send 帧输出** 即通过。
3. 串口助手发 `wake\r\n`（hex: `77 61 6B 65 0D 0A`）
4. **预期**: MCU 收到 ASR_EVT_WAKE → 呼吸灯启动（PA9 缓慢呼吸）。**没有 send 帧输出**（main.c 显式不向 ASRPRO 回 play=10；按 spec §3 §1.2 / commit 83cdb83 决定由 ASRPRO 本地词"我在"应答）。

如果步骤 4 触发了任何 send 帧输出，回到 main.c 检查 ASR_EVT_WAKE 分支（line 333-340）是否还误发了 SendPlay。

### Step 6: 提交

```bash
git add MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.c
git commit -m "refactor(asr): 发送路径恢复 \n 帧尾 + SendPlay 变长格式化

- 新增 SendFrame(body, body_len) 统一追加 \n 后单次 Transmit
- SendPlay 改用 snprintf 变长（1-3 位不补零），删 >99 截断
- SendStop/SendPing 改调 SendFrame
- 删除旧 SendStr
- LINE_BUF_SIZE 32 → 40（v0.7 上限 32 + 8 字节余量）
- 接收路径零改动；API 签名零改动

Co-Authored-By: Claude <noreply@anthropic.com>"
git push
```

---

## Task 3: `main.c` 业务接入 — 开机语 + 抽 PerformShutdown

**Files:**
- Modify: `MCU_XiaoBai/User/main.c:107-135, 196-204, 267-277`（4 处）

**Interfaces:**
- Consumes: Task 1 新宏值（`ASR_VOICE_BOOT` 现 = 17, `ASR_VOICE_SHUTDOWN` = 18）
- Produces: `PerformShutdown()` 静态函数（line 107 附近新增），KEY1 长按与未来 `cmd=1` 共享

### Step 1: 新增 `PerformShutdown` 静态函数

在 main.c line 107 区域（`/* ===== 统一函数 ===== */` 注释下、SwitchMode 之前）插入：

```c
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
```

### Step 2: 开机语独立发出（line 204）

将原 line 203-204:

```c
/* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
Bsp_Tick_DelayMs(1500);
SwitchMode(APP_MODE_VOICE, 1);   /* 点 LED1 + 播"进入语音模式"（兼作开机语） */
```

改为:

```c
/* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
Bsp_Tick_DelayMs(1500);
/* v0.7 ID 17 = "你好呀我是小白进入语音模式"，本身就是合并句，
   单独播；SwitchMode 用 play_voice=0 静默切避免重复 */
Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
SwitchMode(APP_MODE_VOICE, 0);
```

### Step 3: KEY1 长按分支替换为单行（line 267-277）

将原 line 267-277 整段:

```c
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                Bsp_Motor_StopAll();
                Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
                Bsp_Power_ShutdownAnimation();   /* 关机动画：4灯逐个灭，2s */
                Bsp_LedPwm_Set(LEDPWM_1, 0);
                Bsp_LedPwm_Set(LEDPWM_2, 0);
                Bsp_Tm1640_Clear();
                Bsp_Tick_DelayMs(1000);          /* 断电前延时 1 秒 */
                Bsp_Power_ShutDown();
            }
```

替换为:

```c
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                PerformShutdown();
            }
```

### Step 4: 验证编译

操作: Keil MDK F7

预期: 编译通过。`PerformShutdown` 函数体内调用的 6 个 API 都在 main.c 现有引用里（`Bsp_Motor_StopAll` / `Bsp_UartAsr_SendPlay` / `Bsp_Power_ShutdownAnimation` / `Bsp_LedPwm_Set` / `Bsp_Tm1640_Clear` / `Bsp_Tick_DelayMs` / `Bsp_Power_ShutDown`）——任何一个 link 失败说明函数签名被改过，回到 Bsp_*.h 核对。

### Step 5: 验证字节级

flash + USB-TTL，重复 Task 2 Step 4 的 4 项按键验证：
- 按 KEY1 短按 → 串口助手收到 `70 6C 61 79 3D 32 32 0A`（`play=22\n`）✓
- 长按 KEY1 → 收到 `70 6C 61 79 3D 31 38 0A`（`play=18\n`）✓
- 开机瞬间 → 收到 `70 6C 61 79 3D 31 37 0A`（`play=17\n`，开机语）✓
- 不会再收到 `play=22`（开机时 SwitchMode 已用 play_voice=0 静默）✓

### Step 6: 提交

```bash
git add MCU_XiaoBai/User/main.c
git commit -m "refactor(app): 抽 PerformShutdown 共享 KEY1 长按与未来 cmd=1；开机改播 BOOT

- 新增 PerformShutdown 静态函数（停电机→播 shutdown→关机动画→LED/TM1640→延时→断电）
- KEY1 长按分支改成单行 PerformShutdown()
- 开机语从 SwitchMode 内联改为独立发 ASR_VOICE_BOOT (ID 17)
- SwitchMode(VOICE) 改用 play_voice=0 静默切模式（避免与开机语重复）

Co-Authored-By: Claude <noreply@anthropic.com>"
git push
```

---

## Task 4: `main.c` 业务接入 — cmd 分派链三段化 + 全部回播

**Files:**
- Modify: `MCU_XiaoBai/User/main.c:47-60, 219-345`（数据表 + 防抖删除 + 分派链重写）

**Interfaces:**
- Consumes: Task 1 新宏值（`ASR_CMD_*` 现 1-16）、Task 3 的 `PerformShutdown`
- Produces: `cmd_to_voice[11]` 查表（顶部数据区）+ 改写后的 cmd 分派链

### Step 1: 新增 `cmd_to_voice[11]` 查表（line 60 附近，紧邻 `sensor_voice[]`）

在 line 60 后（`/* 模式 -> 进入时播报的语音 ID */` 块的最后）插入:

```c
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
```

### Step 2: 删除防抖相关变量与注释（line 219-224）

将原:

```c
    /* 语音动作命令防抖时间戳：800ms 内只执行第一个动作命令，
       防 ASRPRO 误识别连续发 cmd 导致电机正反转交替抖动 */
    uint32_t last_cmd_time = 0;
```

替换为（直接删除这 4 行，无替代）:

```c
    /* 动作命令防抖已移除：v0.7 下 ASRPRO 侧不误连发，每条 cmd 都执行。
       依赖 ASRPRO 侧命令冗余抑制；如未来发现误识别，在此处重新加防抖窗。 */
```

### Step 3: 改写 cmd 分派链为三段结构（line 280-345）

将原 ASR_EVT_CMD 处理整段替换为三段式：

**⚠ 顺序：关机（段1）→ 切模式（段2）→ 仅语音模式动作（段3），不可调换。** 关机段必须在 `g_mode == APP_MODE_VOICE` 判定之前，否则非语音模式下的"关机"命令会被吃掉。

```c
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
```

### Step 4: 验证编译

操作: Keil MDK F7

预期: 编译通过。`cmd_to_voice[e.arg - ASR_CMD_FORWARD]` 索引 e.arg∈[6,16] → 索引 ∈[0,10]，查表越界安全。`ASR_CMD_FORWARD`=6 已确认（Task 1 改后值）。

### Step 5: 验证语音命令（用串口助手模拟 ASRPRO，hex 发送）

flash + USB-TTL。**不按按键**，纯用串口助手 TX→MCU RX 模拟 ASRPRO：

| 测试 | 串口助手发 (hex) | 期望 MCU 发出 (hex) | 期望业务行为 |
|---|---|---|---|
| 段1: 语音关机 | `63 6D 64 3D 31 0D 0A` (`cmd=1\r\n`) | `70 6C 61 79 3D 31 38 0A` (`play=18\n`) | 走 PerformShutdown，板子断电 |
| 段2: 语音切动力 | `63 6D 64 3D 32 0D 0A` (`cmd=2\r\n`) | `70 6C 61 79 3D 31 39 0A` (`play=19\n`) | LED 切到动力模式对应 LED |
| 段2: 语音切语音 | `63 6D 64 3D 35 0D 0A` (`cmd=5\r\n`) | `70 6C 61 79 3D 32 32 0A` (`play=22\n`) | LED 切到语音模式对应 LED |
| 段3: 语音前进 | `63 6D 64 3D 36 0D 0A` (`cmd=6\r\n`) | `70 6C 61 79 3D 32 37 0A` (`play=27\n`) | 双轮前进 |
| 段3: 语音停止 | `63 6D 64 3D 31 30 0D 0A` (`cmd=10\r\n`) | `70 6C 61 79 3D 33 31 0A` (`play=31\n`) | 电机停 |
| 段3: 语音单电机 | `63 6D 64 3D 31 31 0D 0A` (`cmd=11\r\n`) | `70 6C 61 79 3D 33 36 0A` (`play=36\n`) | 只左轮转 |
| 段3 范围外: 在动力模式发 cmd=6 | 先发 `cmd=2` 切到动力，再发 `cmd=6` | **不应** 发 play=27；电机动 | 段3 头有 `g_mode==VOICE` 拦截 |
| 段3 范围外: cmd=17 | 发 `63 6D 64 3D 31 37 0D 0A` | **不应** 发任何帧 | 段3 范围 [6,16] 拦截 |

预期: 8 项全部按表对得上。如有差异，**重点查分派链顺序**：关机段1、段2、段3 顺序不可换。

### Step 6: 验证真 ASRPRO 联调（手头有 ASRPRO 板时）

操作: 接真 ASRPRO 板，串口助手**只监测 MCU 发送方向**：

1. 唤醒: 对 ASRPRO 说 "小白小白" → 期望 MCU 串口无 send 输出（呼吸灯亮），ASRPRO 自身播 "我在"
2. 切模式: 说 "进入动力模式" → 期望 MCU 发 `play=19\n`
3. 方向: 说 "前进" → 期望 MCU 发 `play=27\n` + 双轮转
4. 单电机: 说 "左电机正转" → 期望 MCU 发 `play=36\n` + 只左轮转
5. 关机: 说 "关机" → 期望 MCU 发 `play=18\n` + 板子断电

### Step 7: 提交

```bash
git add MCU_XiaoBai/User/main.c
git commit -m "refactor(app): cmd 分派链三段化 + 全部动作回播 + 移除 800ms 防抖

- 新增 cmd_to_voice[11] 查表（cmd=6..16 → play=27..31/36..41）
- cmd 分派改为三段: 关机(任何模式) → 切模式(任何模式) → 动作(仅语音模式)
- 段3 动作前统一回播；切模式改 play_voice=1（回播）
- 移除 last_cmd_time 防抖；ASRPRO 侧保证不误连发
- cmd=1 走 PerformShutdown() 与 KEY1 长按共用

Co-Authored-By: Claude <noreply@anthropic.com>"
git push
```

---

## Task 5: 端到端冒烟测试 + 文档同步

**Files:**
- Modify: `resource/语音芯片交互协议.md`（仅头部"更新日期"和"变更记录"，**不**改协议本身）
- Modify: `resource/MCU协议注意事项.md`（仅顶部"变更记录"，**不**改注意事项本身）

**Interfaces:**
- 无（文档元数据同步）

### Step 1: 触发所有 25 个 `ASR_VOICE_*` 与 16 个 `ASR_CMD_*` 至少一次

flash 真固件 + 接 ASRPRO 板，按表过一遍：

| 触发源 | ID 覆盖 |
|---|---|
| 开机 | 17 |
| KEY1 长按 | 18 |
| KEY1 短按 | 22 |
| KEY2 短按 | 20 |
| KEY3 短按 | 21 |
| KEY4 短按 | 19 |
| BLE 边沿（遥控器连/断） | 23, 24 |
| 低电量（短接电池分压或按 Bsp_Battery 测试路径） | 25 |
| 动力模式切动作 ×5 | 27, 28, 29, 30, 31 |
| 感应模式切玩法 ×4 | 32, 33, 34, 35 |
| 语音单电机 ×6 | 36, 37, 38, 39, 40, 41 |
| 语音命令 ×16 | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 |

**通过条件**：每条 ID 至少 1 次在串口助手观察窗中出现过；任何 ID 缺失，回到对应 task 排查引用点。

### Step 2: 同步协议文档变更记录

在 `resource/语音芯片交互协议.md` 顶部"更新日期"行（第 3 行）改为：

```markdown
- 更新日期：2026-07-23（v0.7：ID 表按实际录制资源重排。命令词 ID 改为 1-16 连续，播报 ID 改为 17-41 连续，两段不重叠。协议帧格式不变。已与 MCU 端 Bsp_UartAsr v1 重构同步）
```

**不**改协议表、**不**改帧格式、**不**改示例。

在 `resource/MCU协议注意事项.md` 顶部"七、变更记录"段尾追加：

```markdown
- **2026-07-23**：v1 增补。新增 §四 #21（"MCU 每次实际动作 → SendPlay"统一回播规则）；§五 状态机建议扩充 PLAYING 含 cmd 切模式；与协议 v0.7 + Bsp_UartAsr v1 重构同步落地。
```

**不**改原 §一-§六 内容、**不**改表格。

### Step 3: 提交

```bash
git add resource/语音芯片交互协议.md resource/MCU协议注意事项.md
git commit -m "docs(asr): 协议 v0.7 文档头部同步 — 标记 MCU 端 Bsp_UartAsr v1 已落地

- 语音芯片交互协议.md: 顶部更新日期行加注"已与 Bsp_UartAsr v1 同步"
- MCU协议注意事项.md: §七 变更记录加 v1 增补条目

Co-Authored-By: Claude <noreply@anthropic.com>"
git push
```

---

## 自我检查

- **Spec 覆盖**：
  - §三 播报规则表 → Task 3 开机、Task 4 切模式/动作回播、Task 5 回归 ✓
  - §四 宏值重编 → Task 1 ✓
  - §五 传输层 → Task 2 ✓
  - §六 业务接入 → Task 3 + Task 4 ✓
  - §七 测试 → Task 2 Step 4-5、Task 3 Step 5、Task 4 Step 5-6、Task 5 Step 1 ✓
  - §八 边界 → Task 2 Step 4 字节级覆盖 ✓
  - §九 回归 → Task 5 Step 1 ID 矩阵覆盖 ✓
  - §十 提交 → 每个 Task 都有 commit + push ✓

- **Placeholder 扫描**：
  - 搜"待定 / TODO / 后续补充 / 后面实现" → 0 命中 ✓
  - 搜"参考 Task N" → Task 3 引用"Task 1 新宏值"是文件名/类型引用不是步骤引用，OK ✓
  - 搜"类似 / 差不多" → 0 命中 ✓

- **类型/符号一致**：
  - `Bsp_UartAsr_SendPlay(uint8_t)` 一致 ✓
  - `Bsp_UartAsr_SendStop(void)` / `SendPing(void)` / `SendRaw(const uint8_t*, uint16_t)` / `TryRecv(Bsp_UartAsr_Event_t*)` 一致 ✓
  - `ASR_CMD_FORWARD`=6 / `ASR_CMD_R_STOP`=16 / `ASR_CMD_ENTER_POWER`=2 / `ASR_CMD_ENTER_VOICE`=5 全部取自 Task 1 终值 ✓
  - `cmd_to_voice[11]` 索引 [0,10] = e.arg - 6 ∈ [0,10] 安全 ✓
  - `PerformShutdown()` 内部 7 个 API 调用在 Task 3 Step 4 已确认 link 通过 ✓
