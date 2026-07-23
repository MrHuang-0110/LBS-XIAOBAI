# ASR 语音交互协议 v0.7 重构设计

- **日期**：2026-07-23
- **状态**：设计已定稿，待生成实施计划
- **权威协议**：[resource/语音芯片交互协议.md](../../../resource/语音芯片交互协议.md) v0.7
- **权威注意事项**：[resource/MCU协议注意事项.md](../../../resource/MCU协议注意事项.md)

---

## 一、背景

### 1.1 协议演进

- v0.6 (2026-07-09)：两方向统一带帧尾（MCU→ASR: `\n`；ASR→MCU: `\r\n`）
- v0.6 撤销（commit 91ddc51, 2026-07-11 左右）：ASRPRO 侧 `serial_readstr` 精确比较不接受帧尾，MCU→ASR 侧改回不发帧尾
- **v0.7 (2026-07-23)**：ID 表按实际录制资源重排；命令 ID 改为 1-16 连续、播报 ID 改为 17-41 连续、两段不重叠；帧格式回归 v0.6（两方向都带帧尾）

### 1.2 关键前提（本次重构成立的先决条件）

1. **ASRPRO 侧已改为读到 `\n` 才分帧**（不再走 serial_readstr 精确比较）。否则 MCU 加 `\n` 会导致全部播报失配。
2. **MCU 侧要恢复 v0.6 撤销的 `\n` 帧尾**，与 v0.7 §一对齐。
3. **保留现有宏名（`ASR_VOICE_*` / `ASR_CMD_*`）不变**，只重编宏值，减少 main.c 触点。

### 1.3 现状差异（v0.7 vs 代码）

| 项 | 当前代码 | v0.7 要求 |
|---|---|---|
| 播报 ID 编号 | 1–15 + 20–23（不连续） | 17–41 连续 |
| 命令 ID 编号 | 30–34 / 40–45 / 50–53 | 1–16 连续；wake=0 独立成 `wake\r\n` |
| MCU→ASR 帧尾 | 不发（v0.6 撤销后） | 必须带 `\n` |
| SendPlay 格式 | 定长 2 位零填充 `play=07` | 1-3 位不补零 `play=17` |
| `cmd=1` 关机 | 不存在（原命令表无此项） | 新增支持 |
| 单电机播报语 | 无对应宏 | 新增 6 条 (36..41) |

---

## 二、目标

- **一次性对齐 v0.7 协议**（传输层帧尾 + ID 值域 + 变长格式化）
- **建立"MCU 每次实际动作 → 对应 SendPlay"的一致规则**（含语音切模式、语音方向动作、语音单电机动作、语音关机）
- **顺手清一遍**：LINE_BUF 32→40（余量）、SendPlay 变长、播报宏表覆盖完整 v0.7 §三

---

## 三、播报触发规则（形式化）

| 触发源 | 事件 | 播报 ID | 状态 |
|---|---|:--:|:--:|
| 系统 | 开机完成 | 17 | **改动** |
| 系统 | KEY1 长按 / cmd=1 关机 | 18 | 现有 / **改动**（新增 cmd 路径） |
| 按键 KEY1..KEY4 短按切模式 | 进入语音/动力/感应/遥控 | 22/19/20/21 | 现有 |
| ASR cmd=2..5 语音切模式 | 同上 | 22/19/20/21 | **改动**：从静默改为回播 |
| BLE 边沿 | 连接 / 断开 | 23 / 24 | 现有 |
| 电池 | 低电量循环（30s） | 25 | 现有 |
| 动力模式按键切动作 | 前/后/左/右/停 | 27/28/29/30/31 | 现有 |
| **ASR cmd=6..10 语音方向动作** | 前/后/左/右/停 | 27/28/29/30/31 | **改动**：新增回播 |
| **ASR cmd=11..16 语音单电机动作** | L 正/反/停、R 正/反/停 | 36..41 | **改动**：新增回播 |
| 感应模式按键切玩法 | 靠近/遇障/挥手/明暗 | 32/33/34/35 | 现有 |
| `ASR_VOICE_RECEIVED` (26) | — | 26 | 保留宏，业务未引用 |

**规则不变量**：v0.7 命令 ID 段 1-16 与播报 ID 段 17-41 不重叠；业务层拿到 arg 值可以直接判范围。

---

## 四、改动一：`Bsp_UartAsr.h` 宏值重编

### 4.1 播报 ID 宏（`ASR_VOICE_*`，24 条）

按 v0.7 §三 全部重编（宏名不变，值全换）：

| 宏名 | 新值 | 语义 |
|---|:--:|---|
| `ASR_VOICE_BOOT` | 17 | 你好呀我是小白进入语音模式（开机语） |
| `ASR_VOICE_SHUTDOWN` | 18 | 再见啦，小白去休息了 |
| `ASR_VOICE_ENTER_POWER` | 19 | 进入动力模式 |
| `ASR_VOICE_ENTER_SENSOR` | 20 | 进入感应模式 |
| `ASR_VOICE_ENTER_REMOTE` | 21 | 进入遥控模式 |
| `ASR_VOICE_ENTER_VOICE` | 22 | 进入语音模式 |
| `ASR_VOICE_BLE_CONNECTED` | 23 | 遥控已连接 |
| `ASR_VOICE_BLE_LOST` | 24 | 遥控已断开 |
| `ASR_VOICE_LOW_BATTERY` | 25 | 低电量 |
| `ASR_VOICE_RECEIVED` | 26 | 收到 |
| `ASR_VOICE_FORWARD` | 27 | 前进 |
| `ASR_VOICE_BACKWARD` | 28 | 后退 |
| `ASR_VOICE_LEFT` | 29 | 左转 |
| `ASR_VOICE_RIGHT` | 30 | 右转 |
| `ASR_VOICE_STOP` | 31 | 停止 |
| `ASR_VOICE_APPROACH_GO` | 32 | 靠近启动 |
| `ASR_VOICE_OBSTACLE_STOP` | 33 | 遇障停止 |
| `ASR_VOICE_WAVE_TOGGLE` | 34 | 挥手开关 |
| `ASR_VOICE_BRIGHTNESS` | 35 | 明暗调速 |
| **新增** `ASR_VOICE_L_FWD` | 36 | 左电机正转 |
| **新增** `ASR_VOICE_L_REV` | 37 | 左电机反转 |
| **新增** `ASR_VOICE_L_STOP` | 38 | 左电机停止 |
| **新增** `ASR_VOICE_R_FWD` | 39 | 右电机正转 |
| **新增** `ASR_VOICE_R_REV` | 40 | 右电机反转 |
| **新增** `ASR_VOICE_R_STOP` | 41 | 右电机停止 |

### 4.2 命令 ID 宏（`ASR_CMD_*`，16 条）

按 v0.7 §四 全部重编（wake=0 独立成 `wake\r\n`，不占宏）：

| 宏名 | 新值 | 语义 |
|---|:--:|---|
| **新增** `ASR_CMD_SHUTDOWN` | 1 | 关机 |
| `ASR_CMD_ENTER_POWER` | 2 | 进入动力模式 |
| `ASR_CMD_ENTER_SENSOR` | 3 | 进入感应模式 |
| `ASR_CMD_ENTER_REMOTE` | 4 | 进入遥控模式 |
| `ASR_CMD_ENTER_VOICE` | 5 | 进入语音模式 |
| `ASR_CMD_FORWARD` | 6 | 前进 |
| `ASR_CMD_BACKWARD` | 7 | 后退 |
| `ASR_CMD_LEFT` | 8 | 左转 |
| `ASR_CMD_RIGHT` | 9 | 右转 |
| `ASR_CMD_STOP` | 10 | 停止 |
| `ASR_CMD_L_FWD` | 11 | 左电机正转 |
| `ASR_CMD_L_REV` | 12 | 左电机反转 |
| `ASR_CMD_L_STOP` | 13 | 左电机停止 |
| `ASR_CMD_R_FWD` | 14 | 右电机正转 |
| `ASR_CMD_R_REV` | 15 | 右电机反转 |
| `ASR_CMD_R_STOP` | 16 | 右电机停止 |

### 4.3 头部注释

更新到 v0.7；链接 `resource/语音芯片交互协议.md` 与 `resource/MCU协议注意事项.md`。

---

## 五、改动二：`Bsp_UartAsr.c` 传输层

### 5.1 发送路径：恢复 `\n` 帧尾 + 变长格式化 + 单次 Transmit

**新增静态 `SendFrame`**（拷贝 body + 追加 `\n` + 单次 HAL_UART_Transmit）：

```c
#define ASR_TX_TAIL_CHAR    '\n'    /* v0.7 §一：MCU→ASR 帧尾 = LF */

static void SendFrame(const char *body, uint16_t body_len)
{
    uint8_t frame[16];              /* v0.7 最长 "play=255\n" = 9；余量给到 16 */
    if (body_len + 1 > sizeof(frame)) return;
    for (uint16_t i = 0; i < body_len; i++) frame[i] = (uint8_t)body[i];
    frame[body_len] = ASR_TX_TAIL_CHAR;
    HAL_UART_Transmit(&huart, frame, (uint16_t)(body_len + 1), 1000);
}
```

**`Bsp_UartAsr_SendPlay` 改成变长**：

```c
void Bsp_UartAsr_SendPlay(uint8_t voice_id)
{
    char buf[9];   /* "play=" (5) + "255" (3) + '\0' (1) = 9 */
    int n = snprintf(buf, sizeof(buf), "play=%u", (unsigned)voice_id);
    if (n <= 0 || n >= (int)sizeof(buf)) return;
    SendFrame(buf, (uint16_t)n);
}

void Bsp_UartAsr_SendStop(void) { SendFrame("stop", 4); }
void Bsp_UartAsr_SendPing(void) { SendFrame("ping", 4); }
```

**`SendRaw` 保持不变**（调试透传，绕过协议格式化）。

**注意事项 §一 #2**：这里用 `snprintf` 到 buf、再 `HAL_UART_Transmit` 发 buf，不是 `printf` 直发。newlib 的 `\n → \r\n` 转换只发生在 `printf` 的 stdout backend，不会污染帧。

### 5.2 接收路径

**`LINE_BUF_SIZE` 32 → 40**（MCU 注意事项 §四 #6 "建议 40-64 留余量"）。

**其余保持不变**：
- DMA1_Channel1 独立中断号（不涉及 [[py32-dma-channel2-3-shared-irq]] 共享 IRQ 问题）
- USART2 IDLE 中断 + `HAL_UART_IdleFrameDetectCpltCallback`
- `Frame_FeedByte` 字符状态机：`\n` 触发帧结束、末字节是 `\r` 顺便剥
- `Frame_FeedRange` 环形缓冲 wrap
- `Line_Dispatch` 分派：wake / cmd= / done= / 其它静默丢
- `Parse_EqDec` 十进制解析：0..255 值域 + 1..3 位数字

**顶部注释块**：v0.6 → v0.7，加"接收方向兼容有无帧尾"一句。

### 5.3 API 签名

**全部保持不变**：

```c
void    Bsp_UartAsr_Init(void);
void    Bsp_UartAsr_SendPlay(uint8_t voice_id);
void    Bsp_UartAsr_SendStop(void);
void    Bsp_UartAsr_SendPing(void);
void    Bsp_UartAsr_SendRaw(const uint8_t *data, uint16_t len);
uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Event_t *out);
void    Bsp_UartAsr_UART_IRQHandler(void);
void    Bsp_UartAsr_DMA_IRQHandler(void);
```

`Bsp_UartAsr_Event_t` / `Bsp_UartAsr_EvtType_t` 不动。

### 5.4 改动清单

| # | 位置 | 改动 |
|:-:|---|---|
| 1 | 顶部注释块 | v0.6 → v0.7 |
| 2 | `LINE_BUF_SIZE` | 32 → 40 |
| 3 | 新增 `SendFrame` | 拷贝 body + 追加 `\n` + 单次 Transmit |
| 4 | `SendPlay` | 定长 2 位零填充 → snprintf 变长；删掉 `>99` 截断 |
| 5 | `SendStop` / `SendPing` | 改调 `SendFrame` |
| 6 | 删除 `SendStr` | 不再引用 |

接收路径除 `LINE_BUF_SIZE` 外**零改动**。

---

## 六、改动三：`main.c` 业务接入

### 6.1 开机语（line 204）

```c
/* 现: SwitchMode(APP_MODE_VOICE, 1);    // 播 ENTER_VOICE 兼作开机语
   改: */
Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);    /* v0.7 ID 17：开机语（合并"进入语音模式"） */
SwitchMode(APP_MODE_VOICE, 0);           /* 点 LED1，静默切模式 */
```

### 6.2 关机流程抽函数

新增静态函数 `PerformShutdown()`，KEY1 长按分支与 `cmd=1` case 共用：

```c
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

### 6.3 语音切模式：静默 → 回播（line 286-292）

```c
if (e.arg >= ASR_CMD_ENTER_POWER && e.arg <= ASR_CMD_ENTER_VOICE) {
    switch (e.arg) {
    case ASR_CMD_ENTER_POWER:  SwitchMode(APP_MODE_POWER, 1);  break;   /* 0→1 */
    case ASR_CMD_ENTER_SENSOR: SwitchMode(APP_MODE_SENSOR, 1); break;
    case ASR_CMD_ENTER_REMOTE: SwitchMode(APP_MODE_REMOTE, 1); break;
    case ASR_CMD_ENTER_VOICE:  SwitchMode(APP_MODE_VOICE, 1);  break;
    }
}
else if (e.arg == ASR_CMD_SHUTDOWN) {   /* 新增：任何模式响应 */
    PerformShutdown();
}
```

### 6.4 语音方向/单电机命令回播（line 295-330）

**移除**防抖包裹（`is_action` / `now` / `last_cmd_time` 全删；line 221-223 的 `uint32_t last_cmd_time` 也删）。

**新增查表**（放在顶部数据表区，紧邻 `mode_voice[]` / `act_voice[]`）：

```c
/* 语音命令 ID (6..16) → 播报 ID (27..31 / 36..41) 映射
   注：宏值由 v0.7 协议 §三/§四 决定；若协议再调 ID，此表须同步核对 */
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

**动作命令分支改成**（去防抖 + 加回播）：

```c
else if (g_mode == APP_MODE_VOICE) {
    /* v0.7 §四：cmd=6..16 是动作命令，只在语音模式响应 */
    if (e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP) {
        /* 回播：MCU 实际动作 → 对应播报语（规则统一） */
        Bsp_UartAsr_SendPlay(cmd_to_voice[e.arg - ASR_CMD_FORWARD]);
        switch (e.arg) {
        case ASR_CMD_FORWARD:
            Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
            Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH);
            break;
        /* ... case ASR_CMD_BACKWARD..ASR_CMD_R_STOP 原样保留 ... */
        }
    }
}
```

### 6.5 KEY1 长按（line 267-277）

原 6 行内联流程 → 单行 `PerformShutdown();`。

### 6.6 改动清单

| # | 位置 | 改动 |
|:-:|---|---|
| 1 | 顶部静态函数区 | 新增 `PerformShutdown()` |
| 2 | 顶部数据表区 | 新增 `cmd_to_voice[11]` |
| 3 | line 204 | 开机语单独 `SendPlay(BOOT)` + `SwitchMode(VOICE, 0)` |
| 4 | line 221-223 | 删除 `uint32_t last_cmd_time = 0;` 与相关注释 |
| 5 | line 267-277 | KEY1 长按 → `PerformShutdown()` 单行 |
| 6 | line 286-292 | 语音切模式 `play_voice`: 0 → 1；新增 `cmd=1` case |
| 7 | line 295-330 | 移除防抖包裹；动作命令 switch 前加一句回播 |

**未改动**：BLE 遥控帧解析、眼睛动画、呼吸灯、动力模式循环、感应模式循环、遥控超时、低电量循环。

---

## 七、测试计划

### 7.1 T1：传输层字节级（USB-TTL + 串口助手）

1. flash 新固件；串口助手接 PF0/PF1，9600 8N1，HEX 模式
2. 按 KEY1..KEY4，观察收到：`play=22\n` / `play=19\n` / `play=20\n` / `play=21\n`
3. 长按 KEY1，观察收到：`play=18\n`
4. **通过条件**：每条帧末字节是 `0A`，前面**无** `0D`

### 7.2 T2：接收路径（串口助手手打给 MCU）

1. 手打 `wake\r\n`（HEX: `77 61 6B 65 0D 0A`），MCU 应收到 `ASR_EVT_WAKE` → 呼吸灯启动
2. 手打 `cmd=6\r\n`（语音模式下），MCU 应回播 `play=27\n` + 电机前进
3. 手打 `cmd=1\r\n`，MCU 应走关机流程
4. 手打 `cmd=11\r\n`，MCU 应回播 `play=36\n` + 只左轮转
5. 手打 `cmd=2\r\n`，MCU 应回播 `play=19\n` + 切到动力模式（LED3 亮）

### 7.3 T3：真 ASRPRO 联调

1. 说 "小白小白" → 呼吸灯亮 15s
2. 说 "前进" → 听 MCU 回播 "前进" → 双轮前进
3. 说 "进入动力模式" → 听 MCU 回播 "进入动力模式" → LED 切
4. 说 "关机" → 听 "再见啦小白去休息了" → 断电
5. 说 "左电机正转" → 听 "左电机正转" → 只左轮转

### 7.4 T4：ID 边界回归

- 25 个 `ASR_VOICE_*` 至少各触发一次（KEY / CMD / BLE 边沿 / IR / 低电量组合）
- 16 个 `ASR_CMD_*` 至少各触发一次（真 ASRPRO 或串口助手手打）

---

## 八、边界情况

| 场景 | 期望行为 |
|---|---|
| `cmd=17\r\n`（越界） | `EVT_CMD` 入队，业务层 switch 无匹配 case，静默丢 |
| `done=17\r\n` | `EVT_DONE` 入队，业务层空分支处理 |
| `play=17\r\n`（对方回环） | `Line_Dispatch` tag 不匹配 → 静默丢 |
| MCU 发帧时 ASRPRO 未上电 | `HAL_UART_Transmit` 阻塞发出，无回执，业务层无阻塞 |
| 超长帧（>40 字节） | `g_line_ovf=1` → 本帧丢，下一 `\n` 复位 |
| 空帧 `\n` | `Line_Dispatch(line, 0)` 顶部 `if (len==0) return` |
| 连发两条 cmd 且冲突（如 "前进"→"后退"） | 两次都执行，电机会突变。**依赖 ASRPRO 侧不误连发**；如果实测发现问题，未来在 main.c 再加一层 200ms 短防抖 |
| 单帧内 body 长度接近 8 字节（`play=255`） | `SendFrame` 内部 frame[16] 缓冲够，通过 |

---

## 九、回归风险

**未改但可能受影响的模块**：
- BLE 遥控帧解析（`stream[]` / KEY 映射）：无接口变动，风险低
- 感应模式 IR ADC：无接口变动，风险低
- 电机 PWM（TIM3）：无接口变动，风险低
- 呼吸灯（TIM1）：无接口变动，风险低
- TM1640 眼睛动画：无接口变动，风险低
- BSP_Init 顺序：无变动

**回归测试**：M1/M2 已通过场景过一遍（按键切模式、动力五动作、感应四玩法、遥控方向键 + 肩键调速、低电量播报、BLE 连接/断开播报）。

---

## 十、提交计划

**单个原子 commit**：

```
refactor(asr): 语音交互层对齐协议 v0.7（帧尾 \n / ID 重编 / 全动作回播 / 语音关机）

- Bsp_UartAsr.h: 24 条 VOICE 宏 + 16 条 CMD 宏值按 v0.7 §三/§四 全部重编
  * 新增 ASR_CMD_SHUTDOWN=1
  * 新增 ASR_VOICE_L_FWD..R_STOP (36..41)
- Bsp_UartAsr.c: 发送恢复 \n 帧尾（v0.7 §一）；SendPlay 变长格式；LINE_BUF 32→40
- main.c: 开机播 BOOT(17)；语音切模式改回播；cmd=1 走关机；cmd=6..16 全部回播；移除 800ms 防抖
- 参考文档 resource/语音芯片交互协议.md v0.7 + resource/MCU协议注意事项.md
```

commit 后按 [[auto-push-after-commit]] 立即 `git push`。

---

## 变更记录

- 2026-07-23：初稿，对应协议 v0.7 与 MCU 注意事项 v1
