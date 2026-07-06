# 小白-Ai 主控 MCU (PY32F030K28U6TR) BSP 实施计划 v1

> **For agentic workers:** 本计划面向不了解本项目、也不熟悉 PY32 HAL 库的工程师。每一小步 (checkbox) 都是 2-5 分钟的独立动作，按顺序完成即可。`- [ ]` 复选框用于跟踪进度。

**Goal:** 从零建立小白-Ai 主控 MCU 的 Keil (MDK-ARM) 工程，完成板级初始化 (BSP) 与最小自检骨架，为后续业务层（模式状态机、遥控、感应、语音等）提供稳定驱动接口。

**Architecture:** 基于普冉半导体 PY32F0xx HAL 库，参照 SDK 里 `PY32F030 开机测试例程` 的 `User / BSP_Drivers / MDK-ARM / CMSIS / PY32F0xx_HAL_Driver` 五层目录组织。每个外设一个独立 `Bsp_XXX/` 子模块（`.c` + `.h`），一个 `BSP_Init()` 汇总。开机流程强制 "KEY1 长按 ≥ 2 秒 + 4 只模式 LED 逐段点亮做进度提示" 才锁存电源 (PA15 置高)。

**Tech Stack:**

- MCU：PY32F030K28U6TR (48 MHz HSI, 32 KB Flash / 4 KB SRAM, LQFP32/QFN32)
- 工具链：Keil MDK-ARM (ARMCC v5) — 与 SDK 例程一致
- 驱动库：`PY32F0xx_HAL_Driver` (SDK 附带)
- CMSIS：SDK 附带
- 烧录/调试：SWD (PA13/PA14)

---

## 一、硬件资源冻结表（本计划所有代码的唯一 IO 依据）

来自 `resource/小白IO分配.xlsx`（PY32F030K28U6TR 侧）：

| 引脚  | 用途                     | 电平 / 备注                                    |
| ----- | ------------------------ | --------------------------------------------- |
| PA0   | 电池 ADC (ADC_IN0)       | 只读原始 12-bit 值，不换算电压                |
| PA1   | 红外反射通道 1 (ADC_IN1) | 手势识别通道 A                                |
| PA2   | 红外反射通道 2 (ADC_IN2) | 距离检测                                      |
| PA3   | 红外反射通道 3 (ADC_IN3) | 手势识别通道 B                                |
| PA4   | TM1640 SCL               | 软件位翻转，输出                              |
| PA5   | TM1640 SDA               | 软件位翻转，输出                              |
| PA6   | 电机1 A (TIM3_CH1, AF1)  | PWM 20 kHz                                    |
| PA7   | 电机1 B (TIM3_CH2, AF1)  | PWM 20 kHz                                    |
| PA8   | 呼吸灯 1 (TIM1_CH1, AF2) | PWM 1 kHz，高电平亮                           |
| PA9   | 呼吸灯 2 (TIM1_CH2, AF2) | PWM 1 kHz，高电平亮                           |
| PA10  | 模式 LED2 (感应)         | GPIO 输出，低电平亮                           |
| PA11  | 模式 LED3 (遥控)         | GPIO 输出，低电平亮                           |
| PA12  | 模式 LED4 (语音)         | GPIO 输出，低电平亮                           |
| PA13  | SWDIO                    | 保留调试                                      |
| PA14  | SWCLK                    | 保留调试                                      |
| PA15  | PWR_CTRL 电源锁存        | GPIO 输出，高电平锁定开机                     |
| PB0   | 电机2 A (TIM3_CH3, AF1)  | PWM 20 kHz                                    |
| PB1   | 电机2 B (TIM3_CH4, AF1)  | PWM 20 kHz                                    |
| PB2   | 模式 LED1 (动力)         | GPIO 输出，低电平亮                           |
| PB3   | KEY1 (开机/多用键)       | GPIO 输入，外部已上拉，按下 = 低              |
| PB4   | KEY2                     | GPIO 输入，外部已上拉，按下 = 低              |
| PB5   | KEY3                     | GPIO 输入，外部已上拉，按下 = 低              |
| PB6   | USART1_TX → BLE ECB00    | AF0                                           |
| PB7   | USART1_RX → BLE ECB00    | AF0                                           |
| PB8   | KEY4                     | GPIO 输入，外部已上拉，按下 = 低              |
| PF0   | USART2_RX ← ASRPRO       | AF4（datasheet V2.5 §3.3）                    |
| PF1   | USART2_TX → ASRPRO       | AF4（datasheet V2.5 §3.3）                    |
| PF3   | BLE_STA                  | GPIO 输入（不用中断，主循环轮询）             |
| PF4   | 红外发射控制             | GPIO 输出，上电常高                           |

**注：** PA6/PA7/PB0/PB1 上 TIM3 的 AF 号统一是 **AF1**（datasheet V2.5 §3.1 / §3.2 已核对）；PA8/PA9 上 TIM1 的 AF 是 **AF2**；PF0/PF1 上 USART2 的 AF 是 **AF4**；PB6/PB7 上 USART1 的 AF 是 **AF0**。任务里的宏名以 SDK `py32f0xx_hal_gpio_ex.h` 为准。

---

## 二、时钟 & 定时器分配

- 系统时钟：HSI 48 MHz（HSI 24MHz × PLL2），SysTick 每 1 ms 中断一次
- 电机 PWM：TIM3 CH1..CH4 共用 20 kHz，`Prescaler = 0`，`Period = 2399`
- 呼吸灯 PWM：TIM1 CH1/CH2 共用 1 kHz，`Prescaler = 47`（1 MHz 计数）、`Period = 999`
- 未使用定时器：TIM14/16/17（保留给未来的红外调制、按键防抖等）

---

## 三、语音芯片交互协议 v0.1（本项目定义，需在 Task 2 落到 `resource/语音芯片交互协议.md`）

物理层：UART 115200 8N1 全双工。

帧格式（定长 6 字节）：

```
| 0xA5 | LEN | CMD | D0 | D1 | XOR |
```

- `0xA5` 帧头
- `LEN` 固定 `0x03`（CMD + D0 + D1 的字节数）
- `CMD` 命令字
- `D0/D1` 数据，未用填 0
- `XOR = LEN ^ CMD ^ D0 ^ D1`

**MCU → ASRPRO：**

| CMD  | 语义         | D0                |
| ---- | ------------ | ----------------- |
| 0x01 | 播报预置语音 | 语音 ID           |
| 0x02 | 停止当前播报 | 忽略              |
| 0x03 | 心跳         | 忽略              |

**ASRPRO → MCU：**

| CMD  | 语义         | D0                |
| ---- | ------------ | ----------------- |
| 0x81 | 语音命令     | 命令 ID           |
| 0x82 | 唤醒         | 忽略              |
| 0x83 | 播报完成     | 上次语音 ID       |

**语音 ID：**

```
0x01 开机语     0x02 关机语     0x03 进入动力
0x04 进入感应   0x05 进入遥控   0x06 进入语音
0x07 遥控连接   0x08 遥控断开   0x09 低电量
0x0A 收到       0x10..0x14 前进/后退/左转/右转/停止
0x20..0x23 靠近启动/遇障停止/挥手开关/明暗调速
```

**命令 ID：**

```
0x30 前进 0x31 后退 0x32 左转 0x33 右转 0x34 停止
0x40 左电机正转 0x41 左电机反转 0x42 左电机停止
0x43 右电机正转 0x44 右电机反转 0x45 右电机停止
0x50 进入动力 0x51 进入感应 0x52 进入遥控 0x53 进入语音
0x60 进入风扇玩法 0x61 打开风扇 0x62 关闭风扇
0x70 进入投石机玩法 0x71 发射
```

---

## 四、目标目录结构

```
MCU_XiaoBai/
├─ MDK-ARM/                      Keil 工程
├─ CMSIS/                        从 SDK 拷贝
├─ PY32F0xx_HAL_Driver/          从 SDK 拷贝
├─ BSP_Drivers/
│  ├─ Bsp_Tick/                  Bsp_Tick.c/.h    ms 计数封装
│  ├─ Bsp_Power/                 Bsp_Power.c/.h   PA15 锁存 + 长按 2s 确认
│  ├─ Bsp_Led/                   Bsp_Led.c/.h     4 只模式 LED
│  ├─ Bsp_LedPwm/                Bsp_LedPwm.c/.h  PA8/PA9 呼吸灯 (TIM1)
│  ├─ Bsp_Key/                   Bsp_Key.c/.h     4 键扫描 + 事件
│  ├─ Bsp_Motor/                 Bsp_Motor.c/.h   双电机 (TIM3)
│  ├─ Bsp_Adc/                   Bsp_Adc.c/.h     4 通道扫描 + DMA
│  ├─ Bsp_IR/                    Bsp_IR.c/.h      PF4 发射 + 读 ADC1/2/3
│  ├─ Bsp_Battery/               Bsp_Battery.c/.h 读 ADC0
│  ├─ Bsp_UartAsr/               Bsp_UartAsr.c/.h USART2 与 ASRPRO
│  ├─ Bsp_UartBle/               Bsp_UartBle.c/.h USART1 + PF3 与 BLE
│  ├─ Bsp_Tm1640/                Bsp_Tm1640.c/.h  8×14 眼睛点阵
│  └─ Bsp.c / Bsp.h              BSP_Init() 汇总
└─ User/
   ├─ main.c / main.h
   ├─ py32f0xx_hal_conf.h
   ├─ py32f0xx_hal_msp.c
   ├─ py32f0xx_it.c / .h
   └─ system_py32f0xx.c
```

**"文件为什么这样切"：**

- `Bsp_Adc` 独占 4 通道 DMA 扫描；`Bsp_IR`/`Bsp_Battery` 只是"读取者"。
- `Bsp_Led`（GPIO 4 灯）与 `Bsp_LedPwm`（PWM 2 灯）拆开：不同定时器、不同电平极性。
- `Bsp_UartAsr` 与 `Bsp_UartBle` 独立，各自 DMA 通道 + 缓冲区。
- `Bsp_Power` 承载开机确认关键逻辑，第一个被调用，确认不通过就死循环等硬件掉电。

---

## 五、开机流程规范

```
上电（用户按 KEY1 → 硬件给 MCU 供电）
  ↓
HAL_Init() → APP_SystemClockConfig() → SysTick 1ms
  ↓
Bsp_Power_Init：
  1. PA15 输出 = 0
  2. PB3 输入
  3. 4 只模式 LED 输出 = 1（灭）
  ↓
Bsp_Power_WaitConfirm（阻塞最多 2000ms）：
  每 500ms 亮下一只 LED（LED1 → LED2 → LED3 → LED4）
  KEY1 松开 → 返回 0
  满 2000ms → PA15 = 1，立即返回 1（不等 KEY1 释放）
  ↓
开机成功指示：4 只 LED 快速滚动 3 圈（每灯 80ms，共 ~1s）作为视觉反馈
  ↓
继续初始化其余 BSP
```

若确认失败：`while (1) { }` 死循环。**不要**主动关电源（PA15 未置高，一放手就自然掉电）。

---

## 六、Self-Review

- 每个 IO 都被显式配置：见后续 Task 3/4/5/6/8/10/12/14/16
- 每个功能表条目对应 Task：0 骨架 / 1 Tick / 2 协议文档 / 3 LED / 4 电源 / 5 电机 / 6 呼吸灯 / 7 按键 / 8 UART ASR / 9 UART BLE + PF3 / 10 ADC / 11 IR + 电池 / 12 TM1640 / 13 汇总 BSP_Init / 14 关机
- 关键 AF 宏 (`GPIO_AF1_TIM3`, `GPIO_AF2_TIM1`, `GPIO_AF4_USART2`, `GPIO_AF0_USART1`) 已在 datasheet V2.5 §3.1-§3.3 核对，SDK `py32f0xx_hal_gpio_ex.h` 中都存在

---

## 七、任务列表

嵌入式没有单元测试框架，"测试"等同于"在板上看到预期现象或串口打印"。每个 Task 结尾都有验证步骤 + commit。

---

### Task 0：预备 — 建立空工程骨架

**Files:**

- Create dir: `MCU_XiaoBai/`, `MCU_XiaoBai/User/`, `MCU_XiaoBai/BSP_Drivers/`, `MCU_XiaoBai/MDK-ARM/`
- Copy from SDK 例程: `CMSIS/`、`PY32F0xx_HAL_Driver/`、`User/py32f0xx_hal_conf.h`、`User/system_py32f0xx.c`、`MDK-ARM/Project.uvprojx`/`.uvoptx`
- Create: `MCU_XiaoBai/User/main.c`、`main.h`、`py32f0xx_it.c`、`py32f0xx_it.h`、`py32f0xx_hal_msp.c`

- [ ] **Step 1：建目录**

PowerShell：

```powershell
New-Item -ItemType Directory e:\LBS-XiaoBai\MCU_XiaoBai
New-Item -ItemType Directory e:\LBS-XiaoBai\MCU_XiaoBai\User
New-Item -ItemType Directory e:\LBS-XiaoBai\MCU_XiaoBai\BSP_Drivers
New-Item -ItemType Directory e:\LBS-XiaoBai\MCU_XiaoBai\MDK-ARM
```

- [ ] **Step 2：拷贝 SDK**

```powershell
$src = 'e:\LBS-XiaoBai\resource\ebf_py32f030_20250102\2-配套程序\PY32F030 开机测试例程'
Copy-Item -Recurse "$src\CMSIS" e:\LBS-XiaoBai\MCU_XiaoBai\CMSIS
Copy-Item -Recurse "$src\PY32F0xx_HAL_Driver" e:\LBS-XiaoBai\MCU_XiaoBai\PY32F0xx_HAL_Driver
Copy-Item "$src\User\py32f0xx_hal_conf.h" e:\LBS-XiaoBai\MCU_XiaoBai\User\py32f0xx_hal_conf.h
Copy-Item "$src\User\system_py32f0xx.c"   e:\LBS-XiaoBai\MCU_XiaoBai\User\system_py32f0xx.c
Copy-Item "$src\MDK-ARM\Project.uvprojx"  e:\LBS-XiaoBai\MCU_XiaoBai\MDK-ARM\XiaoBai.uvprojx
Copy-Item "$src\MDK-ARM\Project.uvoptx"   e:\LBS-XiaoBai\MCU_XiaoBai\MDK-ARM\XiaoBai.uvoptx
```

- [ ] **Step 3：写 main.h**

`MCU_XiaoBai/User/main.h`:

```c
#ifndef __MAIN_H
#define __MAIN_H
#ifdef __cplusplus
extern "C" {
#endif
#include "py32f0xx_hal.h"
void APP_ErrorHandler(void);
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 4：写最小 main.c**

`MCU_XiaoBai/User/main.c`:

```c
#include "main.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    while (1) { }
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
```

- [ ] **Step 5：写中断入口文件**

`MCU_XiaoBai/User/py32f0xx_it.h`:

```c
#ifndef __PY32F0xx_IT_H
#define __PY32F0xx_IT_H
#ifdef __cplusplus
extern "C" {
#endif
void NMI_Handler(void);
void HardFault_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
#ifdef __cplusplus
}
#endif
#endif
```

`MCU_XiaoBai/User/py32f0xx_it.c`:

```c
#include "main.h"
#include "py32f0xx_it.h"

void NMI_Handler(void)       { }
void HardFault_Handler(void) { while (1) { } }
void SVC_Handler(void)       { }
void PendSV_Handler(void)    { }
void SysTick_Handler(void)   { HAL_IncTick(); }
```

`MCU_XiaoBai/User/py32f0xx_hal_msp.c`:

```c
#include "main.h"
/* HAL_XXX_MspInit 由各 Bsp 模块自行提供 */
```

- [ ] **Step 6：在 Keil 中配置工程（手工一次性完成）**

用 Keil 打开 `MCU_XiaoBai/MDK-ARM/XiaoBai.uvprojx`：

1. Project → Options for Target
   - Device: `PY32F030K28U6`（下拉不到就装普冉 pack）
   - Output → Name of Executable: `XiaoBai`
   - C/C++ → Preprocessor Symbols: `USE_HAL_DRIVER, PY32F030x6`
   - C/C++ → Include Paths：
     - `..\CMSIS\Device\PY32F0xx\Include`
     - `..\CMSIS\Include`
     - `..\PY32F0xx_HAL_Driver\Inc`
     - `..\User`
     - `..\BSP_Drivers`
   - Debug → 选 CMSIS-DAP 或 J-Link
2. Project 树重建：
   - Group `User`：`../User/main.c` `py32f0xx_it.c` `py32f0xx_hal_msp.c` `system_py32f0xx.c`
   - Group `Startup`：`../CMSIS/Device/PY32F0xx/Source/arm/startup_py32f030x6.s`
   - Group `HAL`：`py32f0xx_hal.c` `py32f0xx_hal_cortex.c` `py32f0xx_hal_gpio.c` `py32f0xx_hal_rcc.c`
   - Group `BSP`：（先空着，后续 Task 逐个加）

- [ ] **Step 7：编译**

Keil F7 → 0 error 0 warning。

- [ ] **Step 8：烧录并观察**

按住 KEY1 上电 → 烧录 → 松开 KEY1 → MCU 掉电（因 PA15 未锁存）。**预期：**烧录能完成、松手后电流为 0。

- [ ] **Step 9：Commit**

```powershell
cd e:\LBS-XiaoBai
git init  # 若尚未初始化
git add MCU_XiaoBai
git commit -m "chore(mcu): scaffold empty project with HSI 48MHz clock"
```

---

### Task 1：Bsp_Tick — SysTick 毫秒节拍封装

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Tick/Bsp_Tick.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Tick/Bsp_Tick.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1：写 Bsp_Tick.h**

```c
#ifndef __BSP_TICK_H
#define __BSP_TICK_H
#include "py32f0xx_hal.h"

void     Bsp_Tick_Init(void);
uint32_t Bsp_Tick_GetMs(void);
void     Bsp_Tick_DelayMs(uint32_t ms);

#endif
```

- [ ] **Step 2：写 Bsp_Tick.c**

```c
#include "Bsp_Tick/Bsp_Tick.h"

void     Bsp_Tick_Init(void)          { /* HAL_Init 已配置 SysTick 1kHz */ }
uint32_t Bsp_Tick_GetMs(void)         { return HAL_GetTick(); }
void     Bsp_Tick_DelayMs(uint32_t ms){ HAL_Delay(ms); }
```

- [ ] **Step 3：加入 Keil Group `BSP`**

在 Keil `BSP` 组右键 → Add Existing Files → 选 `Bsp_Tick.c`。

- [ ] **Step 4：main.c 调用一次确认能编译**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    uint32_t t0 = Bsp_Tick_GetMs();
    while (1) {
        if (Bsp_Tick_GetMs() - t0 >= 1000) t0 = Bsp_Tick_GetMs();
    }
}
```

（`APP_SystemClockConfig` 和 `APP_ErrorHandler` 保持 Task 0 的定义。）

- [ ] **Step 5：Debug 单步观察 `HAL_GetTick()` 递增**

Keil F7 → Debug 会话 → 在 while 里下断点，观察 `HAL_GetTick()` 每次约 +1000。

- [ ] **Step 6：Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Tick MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): add SysTick ms wrapper"
```

---

### Task 2：写语音芯片交互协议文档

**Files:**

- Modify: `resource/语音芯片交互协议.md`（当前为空）

- [ ] **Step 1：写入协议**

把本计划"三、语音芯片交互协议 v0.1"的完整内容（含 CMD 表 + 语音 ID 表 + 命令 ID 表）复制到 `resource/语音芯片交互协议.md` 作为正式文档，文件开头加上标题 `# 小白-Ai MCU ↔ ASRPRO UART 交互协议 v0.1`。

- [ ] **Step 2：Commit**

```powershell
git add resource/语音芯片交互协议.md
git commit -m "docs(protocol): define MCU<->ASRPRO UART protocol v0.1"
```

---

### Task 3：Bsp_Led — 4 只模式 LED（PB2/PA10/PA11/PA12，低有效）

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Led/Bsp_Led.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Led/Bsp_Led.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1：Bsp_Led.h**

```c
#ifndef __BSP_LED_H
#define __BSP_LED_H
#include "py32f0xx_hal.h"

typedef enum {
    LED_MODE_POWER  = 0,  /* LED1 - 动力 - PB2  */
    LED_MODE_SENSOR = 1,  /* LED2 - 感应 - PA10 */
    LED_MODE_REMOTE = 2,  /* LED3 - 遥控 - PA11 */
    LED_MODE_VOICE  = 3,  /* LED4 - 语音 - PA12 */
    LED_MODE_COUNT
} Bsp_Led_Id_t;

void Bsp_Led_Init(void);
void Bsp_Led_On(Bsp_Led_Id_t id);
void Bsp_Led_Off(Bsp_Led_Id_t id);
void Bsp_Led_Toggle(Bsp_Led_Id_t id);
void Bsp_Led_AllOff(void);

#endif
```

- [ ] **Step 2：Bsp_Led.c**

```c
#include "Bsp_Led/Bsp_Led.h"

typedef struct { GPIO_TypeDef *port; uint16_t pin; } LedPin_t;

static const LedPin_t g_leds[LED_MODE_COUNT] = {
    { GPIOB, GPIO_PIN_2  },  /* LED1 PB2  */
    { GPIOA, GPIO_PIN_10 },  /* LED2 PA10 */
    { GPIOA, GPIO_PIN_11 },  /* LED3 PA11 */
    { GPIOA, GPIO_PIN_12 },  /* LED4 PA12 */
};

void Bsp_Led_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    gi.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &gi);
    gi.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOB, &gi);

    Bsp_Led_AllOff();
}

void Bsp_Led_On(Bsp_Led_Id_t id)
{
    if (id >= LED_MODE_COUNT) return;
    HAL_GPIO_WritePin(g_leds[id].port, g_leds[id].pin, GPIO_PIN_RESET);
}

void Bsp_Led_Off(Bsp_Led_Id_t id)
{
    if (id >= LED_MODE_COUNT) return;
    HAL_GPIO_WritePin(g_leds[id].port, g_leds[id].pin, GPIO_PIN_SET);
}

void Bsp_Led_Toggle(Bsp_Led_Id_t id)
{
    if (id >= LED_MODE_COUNT) return;
    HAL_GPIO_TogglePin(g_leds[id].port, g_leds[id].pin);
}

void Bsp_Led_AllOff(void)
{
    for (int i = 0; i < LED_MODE_COUNT; i++) Bsp_Led_Off((Bsp_Led_Id_t)i);
}
```

- [ ] **Step 3：加入 Keil**

Add `Bsp_Led.c` 到 `BSP` group。

- [ ] **Step 4：main.c 跑马灯自检**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();

    while (1) {
        for (int i = 0; i < LED_MODE_COUNT; i++) {
            Bsp_Led_AllOff();
            Bsp_Led_On((Bsp_Led_Id_t)i);
            Bsp_Tick_DelayMs(200);
        }
    }
}
```

（注意：电源未锁存，需要按住 KEY1 才能持续供电。）

- [ ] **Step 5：编译烧录，按住 KEY1 观察跑马灯**

**预期：**LED1 → LED2 → LED3 → LED4 依次点亮，每灯 200ms 循环。

- [ ] **Step 6：Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Led MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): 4 mode LEDs (active-low) with running-light self-test"
```

---

### Task 4：Bsp_Power — KEY1 长按 2s 电源锁存 + 进度指示

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Power/Bsp_Power.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Power/Bsp_Power.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1：Bsp_Power.h**

```c
#ifndef __BSP_POWER_H
#define __BSP_POWER_H
#include "py32f0xx_hal.h"

/**
 * @brief 上电确认 + 电源锁存。阻塞最多 2000ms。
 *        返回 1 = 锁存成功；返回 0 = 用户提前松手，调用方应死循环等硬件掉电。
 *        依赖 Bsp_Led_Init / Bsp_Tick_Init 已完成。
 */
uint8_t Bsp_Power_Init_WaitConfirm(void);

/** 主动关机：PA15 = 0，不返回 */
void    Bsp_Power_ShutDown(void);

/** 读 KEY1(PB3)：按下 = 1，未按 = 0 */
uint8_t Bsp_Power_IsKey1Down(void);

#endif
```

- [ ] **Step 2：Bsp_Power.c**

```c
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"

#define PWR_CTRL_PORT   GPIOA
#define PWR_CTRL_PIN    GPIO_PIN_15
#define KEY1_PORT       GPIOB
#define KEY1_PIN        GPIO_PIN_3

#define WAIT_TOTAL_MS   2000
#define WAIT_STEP_MS    500     /* 4 段进度 */

static void Power_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = PWR_CTRL_PIN;
    HAL_GPIO_Init(PWR_CTRL_PORT, &gi);
    HAL_GPIO_WritePin(PWR_CTRL_PORT, PWR_CTRL_PIN, GPIO_PIN_RESET);

    gi.Mode = GPIO_MODE_INPUT;   /* PB3 外部已上拉 */
    gi.Pin  = KEY1_PIN;
    HAL_GPIO_Init(KEY1_PORT, &gi);
}

uint8_t Bsp_Power_IsKey1Down(void)
{
    return HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) == GPIO_PIN_RESET ? 1 : 0;
}

uint8_t Bsp_Power_Init_WaitConfirm(void)
{
    Power_GpioInit();
    Bsp_Led_AllOff();

    uint32_t t0 = Bsp_Tick_GetMs();
    uint8_t stage = 0;

    while (1) {
        if (!Bsp_Power_IsKey1Down()) {
            Bsp_Led_AllOff();
            return 0;
        }

        uint32_t elapsed = Bsp_Tick_GetMs() - t0;

        uint8_t want_stage = (uint8_t)(elapsed / WAIT_STEP_MS);
        if (want_stage > LED_MODE_COUNT) want_stage = LED_MODE_COUNT;
        while (stage < want_stage) {
            Bsp_Led_On((Bsp_Led_Id_t)stage);
            stage++;
        }

        if (elapsed >= WAIT_TOTAL_MS) {
            HAL_GPIO_WritePin(PWR_CTRL_PORT, PWR_CTRL_PIN, GPIO_PIN_SET);
            /* 等 KEY1 稳定释放 100ms */
            uint32_t rel_t = Bsp_Tick_GetMs();
            while (1) {
                if (Bsp_Power_IsKey1Down()) rel_t = Bsp_Tick_GetMs();
                else if (Bsp_Tick_GetMs() - rel_t >= 100) break;
            }
            Bsp_Led_AllOff();
            return 1;
        }
    }
}

void Bsp_Power_ShutDown(void)
{
    HAL_GPIO_WritePin(PWR_CTRL_PORT, PWR_CTRL_PIN, GPIO_PIN_RESET);
    while (1) { }
}
```

- [ ] **Step 3：加入 Keil**

- [ ] **Step 4：main.c 用起来**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();

    if (!Bsp_Power_Init_WaitConfirm()) while (1) { }

    /* 锁存成功，LED4 慢闪表示存活 */
    while (1) {
        Bsp_Led_Toggle(LED_MODE_VOICE);
        Bsp_Tick_DelayMs(500);
    }
}
```

- [ ] **Step 5：3 种场景实机验证**

- 场景 A：按 KEY1 立即松开 → LED 全灭，MCU 掉电 ✅
- 场景 B：按 KEY1 保持 ~1s 后松开 → LED1/LED2 已亮但未到 2s，松手掉电 ✅
- 场景 C：按 KEY1 保持 ≥ 2s → LED1..4 依次点亮 → 全灭 → LED4 慢闪 → 松开 KEY1 不掉电 ✅

- [ ] **Step 6：Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Power MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): power latching w/ 2s long-press + LED progress"
```

---

### Task 5: Bsp_Motor - Dual Motor 4-channel PWM (TIM3, 20 kHz)

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Motor/Bsp_Motor.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Motor/Bsp_Motor.c`
- Modify: `MCU_XiaoBai/User/main.c`
- Keil: 加入 `py32f0xx_hal_tim.c` 与 `py32f0xx_hal_tim_ex.c`

- [ ] **Step 1: Bsp_Motor.h**

```c
#ifndef __BSP_MOTOR_H
#define __BSP_MOTOR_H
#include "py32f0xx_hal.h"

typedef enum { MOTOR_LEFT = 0, MOTOR_RIGHT = 1 } Bsp_Motor_Id_t;

void Bsp_Motor_Init(void);
void Bsp_Motor_Set(Bsp_Motor_Id_t id, int8_t speed);   /* -100..+100 */
void Bsp_Motor_StopAll(void);

#endif
```

- [ ] **Step 2: Bsp_Motor.c**

```c
#include "Bsp_Motor/Bsp_Motor.h"

/*
 * TIM3 clock = 48MHz. Prescaler=0, Period=2399 -> 20 kHz.
 * PA6=CH1(AF1)  PA7=CH2(AF1)  PB0=CH3(AF1)  PB1=CH4(AF1)
 */
#define MOTOR_TIM_PERIOD  2399U

static TIM_HandleTypeDef htim3;

static void Motor_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_AF_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;

    gi.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    gi.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(GPIOA, &gi);

    gi.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    gi.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(GPIOB, &gi);
}

void Bsp_Motor_Init(void)
{
    Motor_GpioInit();
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler         = 0;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = MOTOR_TIM_PERIOD;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim3);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_4);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

    /* 关键：HAL_TIM_PWM_ConfigChannel 默认打开 OCxPE (CCR preload)，
     * Start 不触发 UG，CCMR/CCER 更新停在 preload 不落地到工作寄存器 → PWM 永远不出。
     * 手动生成一次 UG 让配置生效。 */
    htim3.Instance->EGR = TIM_EGR_UG;
}

static void Motor_WritePair(uint32_t ch_a, uint32_t ch_b, int8_t speed)
{
    uint32_t abs_s = (uint32_t)(speed >= 0 ? speed : -speed);
    uint32_t duty  = abs_s * (MOTOR_TIM_PERIOD + 1) / 100U;
    if (duty > MOTOR_TIM_PERIOD) duty = MOTOR_TIM_PERIOD;

    if (speed > 0) {
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, duty);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, 0);
    } else if (speed < 0) {
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, 0);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, duty);
    } else {
        __HAL_TIM_SET_COMPARE(&htim3, ch_a, 0);
        __HAL_TIM_SET_COMPARE(&htim3, ch_b, 0);
    }
}

void Bsp_Motor_Set(Bsp_Motor_Id_t id, int8_t speed)
{
    if (speed >  100) speed =  100;
    if (speed < -100) speed = -100;

    if (id == MOTOR_LEFT) Motor_WritePair(TIM_CHANNEL_1, TIM_CHANNEL_2, speed);
    else                  Motor_WritePair(TIM_CHANNEL_3, TIM_CHANNEL_4, speed);
}

void Bsp_Motor_StopAll(void)
{
    Bsp_Motor_Set(MOTOR_LEFT,  0);
    Bsp_Motor_Set(MOTOR_RIGHT, 0);
}
```

- [ ] **Step 3: 把 `py32f0xx_hal_tim.c` `py32f0xx_hal_tim_ex.c` 和 `Bsp_Motor.c` 加入 Keil**

- [ ] **Step 4: main.c 演示循环**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_Motor/Bsp_Motor.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();
    if (!Bsp_Power_Init_WaitConfirm()) while (1) { }

    Bsp_Motor_Init();

    while (1) {
        Bsp_Motor_Set(MOTOR_LEFT,  +30); Bsp_Motor_Set(MOTOR_RIGHT, +30);
        Bsp_Tick_DelayMs(2000);
        Bsp_Motor_StopAll();
        Bsp_Tick_DelayMs(500);
        Bsp_Motor_Set(MOTOR_LEFT,  -30); Bsp_Motor_Set(MOTOR_RIGHT, -30);
        Bsp_Tick_DelayMs(2000);
        Bsp_Motor_StopAll();
        Bsp_Tick_DelayMs(1500);
    }
}
```

- [ ] **Step 5: 接电机验证**

预期：电机 "正转 2s -> 停 0.5s -> 反转 2s -> 停 1.5s" 循环。

- [ ] **Step 6: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Motor MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): dual motor PWM on TIM3 (20kHz, 4 channels)"
```

---

### Task 6: Bsp_LedPwm - PA8/PA9 呼吸灯 (TIM1 CH1/CH2, 1 kHz) + 启动动画

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_LedPwm/Bsp_LedPwm.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_LedPwm/Bsp_LedPwm.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1: Bsp_LedPwm.h**

```c
#ifndef __BSP_LEDPWM_H
#define __BSP_LEDPWM_H
#include "py32f0xx_hal.h"

typedef enum { LEDPWM_1 = 0, LEDPWM_2 = 1 } Bsp_LedPwm_Id_t;

void Bsp_LedPwm_Init(void);
void Bsp_LedPwm_Set(Bsp_LedPwm_Id_t id, uint8_t duty_percent);  /* 0..100 */
void Bsp_LedPwm_PlayStartupBreath(void);                        /* blocking ~2s */

#endif
```

- [ ] **Step 2: Bsp_LedPwm.c**

```c
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Tick/Bsp_Tick.h"

/* TIM1 clk=48MHz. Prescaler=47 -> 1MHz cnt, Period=999 -> 1kHz.
 * PA8=CH1(AF2)  PA9=CH2(AF2)
 */
#define LEDPWM_TIM_PERIOD  999U

static TIM_HandleTypeDef htim1;

static void LedPwm_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF2_TIM1;
    gi.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOA, &gi);
}

void Bsp_LedPwm_Init(void)
{
    LedPwm_GpioInit();
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance = TIM1;
    htim1.Init.Prescaler         = 47;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = LEDPWM_TIM_PERIOD;
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.Pulse        = 0;
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
}

void Bsp_LedPwm_Set(Bsp_LedPwm_Id_t id, uint8_t duty_percent)
{
    if (duty_percent > 100) duty_percent = 100;
    uint32_t duty = (uint32_t)duty_percent * (LEDPWM_TIM_PERIOD + 1) / 100U;
    if (duty > LEDPWM_TIM_PERIOD) duty = LEDPWM_TIM_PERIOD;
    __HAL_TIM_SET_COMPARE(&htim1,
        (id == LEDPWM_1) ? TIM_CHANNEL_1 : TIM_CHANNEL_2,
        duty);
}

void Bsp_LedPwm_PlayStartupBreath(void)
{
    for (int p = 0; p <= 100; p += 2) {
        Bsp_LedPwm_Set(LEDPWM_1, (uint8_t)p);
        Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)p);
        Bsp_Tick_DelayMs(20);   /* 50 * 20ms = 1s */
    }
    for (int p = 100; p >= 0; p -= 2) {
        Bsp_LedPwm_Set(LEDPWM_1, (uint8_t)p);
        Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)p);
        Bsp_Tick_DelayMs(20);
    }
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
}
```

- [ ] **Step 3: 加入 Keil**

- [ ] **Step 4: main.c 电源锁存后播放启动呼吸**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"

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

    Bsp_Led_On(LED_MODE_VOICE);
    while (1) { }
}
```

- [ ] **Step 5: 烧录、长按开机观察**

预期：开机 2s 确认过后，PA8/PA9 两只灯从暗到亮再回暗（约 2s），最后 LED4 常亮。

- [ ] **Step 6: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_LedPwm MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): breathing LEDs on TIM1 CH1/CH2 + startup anim"
```

---

### Task 7: Bsp_Key - 4 键扫描 + 短/长/双击事件（KEY1 为主，其余 GPIO 预留）

> **实施后变更（2026-07-03）：**
> 1. 双击事件（`KEY_EVT_DOUBLE`）实施时被取消 —— 用户反馈"不需要且短按会因等 double-timeout 卡 300ms"。最终实现只有 `KEY_EVT_SHORT` / `KEY_EVT_LONG`，短按立即上报。
> 2. `Bsp_Key_Init` 用实际 GPIO 电平做 `stable` 初值，并把 `long_reported` 初值也设为当前电平：这样 Bsp_Power 长按 2s 锁存后（KEY1 可能还按着）不会产生假短按/假长按事件。以最终代码为准。

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Key/Bsp_Key.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Key/Bsp_Key.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1: Bsp_Key.h**

```c
#ifndef __BSP_KEY_H
#define __BSP_KEY_H
#include "py32f0xx_hal.h"

typedef enum {
    KEY_ID_1 = 0,   /* PB3 */
    KEY_ID_2 = 1,   /* PB4 */
    KEY_ID_3 = 2,   /* PB5 */
    KEY_ID_4 = 3,   /* PB8 */
    KEY_ID_COUNT
} Bsp_Key_Id_t;

typedef enum {
    KEY_EVT_NONE   = 0,
    KEY_EVT_SHORT  = 1,   /* 松开时判定的短按 */
    KEY_EVT_LONG   = 2,   /* 按住超过 2000ms 触发一次 */
    KEY_EVT_DOUBLE = 3,   /* 两次短按间隔 < 300ms */
} Bsp_Key_Evt_t;

/** 初始化 4 个 KEY GPIO 输入（外部已上拉） */
void Bsp_Key_Init(void);

/**
 * @brief 由主循环周期调用（建议每 10ms 一次）。
 *        每次返回一个键的一个事件；若无事件返回 KEY_EVT_NONE。
 * @param out_id 事件所属键 id（有事件时才有效）
 */
Bsp_Key_Evt_t Bsp_Key_Poll(Bsp_Key_Id_t *out_id);

#endif
```

- [ ] **Step 2: Bsp_Key.c**

```c
#include "Bsp_Key/Bsp_Key.h"
#include "Bsp_Tick/Bsp_Tick.h"

#define KEY_DEBOUNCE_MS   20
#define KEY_LONG_MS       2000
#define KEY_DOUBLE_GAP_MS 300

typedef struct { GPIO_TypeDef *port; uint16_t pin; } KeyPin_t;

static const KeyPin_t g_keys[KEY_ID_COUNT] = {
    { GPIOB, GPIO_PIN_3 },   /* KEY1 */
    { GPIOB, GPIO_PIN_4 },   /* KEY2 */
    { GPIOB, GPIO_PIN_5 },   /* KEY3 */
    { GPIOB, GPIO_PIN_8 },   /* KEY4 */
};

typedef struct {
    uint8_t  raw_last;      /* 上次原始电平：1=按下 */
    uint8_t  stable;        /* 稳定态：1=按下 */
    uint32_t change_t;      /* 上次电平翻转时刻 */
    uint32_t press_t;       /* 稳定按下时刻 */
    uint32_t release_t;     /* 稳定释放时刻 */
    uint8_t  long_reported; /* 长按事件本次按下期间是否已上报 */
    uint8_t  wait_double;   /* 上一次短按后正在等 double */
    uint32_t last_short_t;  /* 上次短按判定时刻 */
} KeyState_t;

static KeyState_t g_st[KEY_ID_COUNT];

static uint8_t Key_ReadRaw(Bsp_Key_Id_t id)
{
    return HAL_GPIO_ReadPin(g_keys[id].port, g_keys[id].pin) == GPIO_PIN_RESET ? 1 : 0;
}

void Bsp_Key_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOB, &gi);

    uint32_t now = Bsp_Tick_GetMs();
    for (int i = 0; i < KEY_ID_COUNT; i++) {
        g_st[i].raw_last = 0;
        g_st[i].stable = 0;
        g_st[i].change_t = now;
        g_st[i].last_short_t = 0;
        g_st[i].wait_double = 0;
        g_st[i].long_reported = 0;
    }
}

Bsp_Key_Evt_t Bsp_Key_Poll(Bsp_Key_Id_t *out_id)
{
    uint32_t now = Bsp_Tick_GetMs();

    for (int i = 0; i < KEY_ID_COUNT; i++) {
        uint8_t raw = Key_ReadRaw((Bsp_Key_Id_t)i);
        KeyState_t *s = &g_st[i];

        /* 去抖 */
        if (raw != s->raw_last) {
            s->raw_last = raw;
            s->change_t = now;
        }
        if ((now - s->change_t) >= KEY_DEBOUNCE_MS && s->stable != raw) {
            s->stable = raw;
            if (raw) {
                s->press_t = now;
                s->long_reported = 0;
            } else {
                s->release_t = now;
                if (s->long_reported) {
                    /* 长按已上报，忽略这次释放 */
                    s->wait_double = 0;
                } else {
                    uint32_t held = now - s->press_t;
                    if (held < KEY_LONG_MS) {
                        if (s->wait_double && (now - s->last_short_t) <= KEY_DOUBLE_GAP_MS) {
                            s->wait_double = 0;
                            *out_id = (Bsp_Key_Id_t)i;
                            return KEY_EVT_DOUBLE;
                        }
                        s->wait_double = 1;
                        s->last_short_t = now;
                    }
                }
            }
        }

        /* 长按检测：稳定按下且尚未上报 */
        if (s->stable && !s->long_reported && (now - s->press_t) >= KEY_LONG_MS) {
            s->long_reported = 1;
            *out_id = (Bsp_Key_Id_t)i;
            return KEY_EVT_LONG;
        }

        /* 等 double 超时 -> 上报短按 */
        if (s->wait_double && (now - s->last_short_t) > KEY_DOUBLE_GAP_MS) {
            s->wait_double = 0;
            *out_id = (Bsp_Key_Id_t)i;
            return KEY_EVT_SHORT;
        }
    }

    return KEY_EVT_NONE;
}
```

- [ ] **Step 3: 加入 Keil**

- [ ] **Step 4: main.c 用 LED 反馈按键**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Key/Bsp_Key.h"

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
    Bsp_Key_Init();

    while (1) {
        Bsp_Key_Id_t id;
        Bsp_Key_Evt_t e = Bsp_Key_Poll(&id);
        if (e == KEY_EVT_SHORT)  Bsp_Led_Toggle(LED_MODE_POWER);
        if (e == KEY_EVT_DOUBLE) Bsp_Led_Toggle(LED_MODE_SENSOR);
        if (e == KEY_EVT_LONG)   { Bsp_Led_On(LED_MODE_REMOTE); Bsp_Tick_DelayMs(500); Bsp_Led_Off(LED_MODE_REMOTE); }
        Bsp_Tick_DelayMs(10);
    }
}
```

- [ ] **Step 5: 板上验证**

- 短按 KEY1 → LED1 翻转
- 双击 KEY1 → LED2 翻转
- 长按 KEY1（≥2s）→ LED3 亮 500ms
- KEY2/3/4 短按同样触发（`id` 会不同，本 Task 只用 KEY1 事件驱动 LED，其余 3 键会短按翻转 LED1 因为共用 handler；下版本业务层再区分）

- [ ] **Step 6: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Key MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): 4-key scan with short/long/double-click events"
```

---

### Task 8: Bsp_UartAsr - USART2 (PF0/PF1) + DMA + IDLE 中断 + 帧解析

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.c`
- Modify: `MCU_XiaoBai/User/py32f0xx_it.c`（加 USART2 与 DMA 中断入口）
- Modify: `MCU_XiaoBai/User/main.c`
- Keil: 加入 `py32f0xx_hal_uart.c` `py32f0xx_hal_uart_ex.c` `py32f0xx_hal_dma.c`

- [ ] **Step 1: Bsp_UartAsr.h**

```c
#ifndef __BSP_UART_ASR_H
#define __BSP_UART_ASR_H
#include "py32f0xx_hal.h"

/* 语音芯片交互协议 v0.1 */
#define ASR_FRAME_HEAD   0xA5U
#define ASR_FRAME_LEN    0x03U
#define ASR_FRAME_SIZE   6U

typedef struct {
    uint8_t cmd;
    uint8_t d0;
    uint8_t d1;
} Bsp_UartAsr_Frame_t;

/** 初始化 USART2 (115200 8N1) + DMA 收发 + IDLE 中断 */
void Bsp_UartAsr_Init(void);

/** 发送一帧（阻塞，超时 10ms） */
void Bsp_UartAsr_Send(uint8_t cmd, uint8_t d0, uint8_t d1);

/**
 * @brief 尝试取出一帧。有帧返回 1 并填充 out；无返回 0。
 *        供主循环轮询。
 */
uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Frame_t *out);

/* 中断入口（由 py32f0xx_it.c 转发） */
void Bsp_UartAsr_UART_IRQHandler(void);
void Bsp_UartAsr_DMA_IRQHandler(void);

#endif
```

- [ ] **Step 2: Bsp_UartAsr.c**

```c
#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include <string.h>

#define RX_BUF_SIZE   64U

static UART_HandleTypeDef huart;
static DMA_HandleTypeDef  hdma_rx;
static uint8_t g_rx_buf[RX_BUF_SIZE];

/* 简单的帧环形队列（最多缓 4 帧） */
#define FRAME_Q_SIZE  4U
static Bsp_UartAsr_Frame_t g_frame_q[FRAME_Q_SIZE];
static volatile uint8_t g_qw = 0, g_qr = 0;

static uint8_t Frame_Check(const uint8_t *p6)
{
    if (p6[0] != ASR_FRAME_HEAD) return 0;
    if (p6[1] != ASR_FRAME_LEN)  return 0;
    uint8_t x = p6[1] ^ p6[2] ^ p6[3] ^ p6[4];
    return x == p6[5];
}

static void Frame_Feed(const uint8_t *buf, uint16_t len)
{
    /* 简单实现：在 buf 中搜 0xA5，找到就尝试解 6 字节。多余的丢弃。 */
    uint16_t i = 0;
    while (i + ASR_FRAME_SIZE <= len) {
        if (buf[i] != ASR_FRAME_HEAD) { i++; continue; }
        if (Frame_Check(&buf[i])) {
            uint8_t next = (uint8_t)((g_qw + 1) % FRAME_Q_SIZE);
            if (next != g_qr) {
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

static void UartAsr_GpioClkInit(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF4_USART2;
    gi.Pin       = GPIO_PIN_0 | GPIO_PIN_1;   /* PF0=RX2, PF1=TX2 */
    HAL_GPIO_Init(GPIOF, &gi);
}

void Bsp_UartAsr_Init(void)
{
    UartAsr_GpioClkInit();

    huart.Instance          = USART2;
    huart.Init.BaudRate     = 115200;
    huart.Init.WordLength   = UART_WORDLENGTH_8B;
    huart.Init.StopBits     = UART_STOPBITS_1;
    huart.Init.Parity       = UART_PARITY_NONE;
    huart.Init.Mode         = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart);

    /* DMA 接收通道（PY32F030 DMA1 channel 1..3；这里选 channel 1 给 USART2_RX；
       具体 request 编号见 SDK 的 py32f0xx_hal_dma_ex.h 的 DMA_REQUEST_x 宏。
       如果 pack 未定义 DMA_REQUEST_USART2_RX，用当前 SDK 里对应宏名替换。） */
    hdma_rx.Instance                 = DMA1_Channel1;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_rx);
    __HAL_LINKDMA(&huart, hdmarx, hdma_rx);

    /* 把 USART2 的 DMA 请求映射到 channel 1（SYSCFG_CFGR3 或 DMA_REQ 映射，
       按 SDK API 名，例如 HAL_SYSCFG_SetDMARemap(SYSCFG_DMA_MAP_USART2_RX, DMA_CHANNEL_1)；
       实际宏名以本 SDK 头为准）。 */

    HAL_NVIC_SetPriority(USART2_IRQn,     1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&huart, g_rx_buf, RX_BUF_SIZE);
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

/* IDLE 中断：把 DMA 缓冲当前未处理段送去帧解析器 */
void Bsp_UartAsr_UART_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart);

        uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_rx);
        uint16_t recv_len = RX_BUF_SIZE - ndtr;

        static uint16_t last_pos = 0;
        if (recv_len != last_pos) {
            if (recv_len > last_pos) {
                Frame_Feed(&g_rx_buf[last_pos], (uint16_t)(recv_len - last_pos));
            } else {
                Frame_Feed(&g_rx_buf[last_pos], (uint16_t)(RX_BUF_SIZE - last_pos));
                if (recv_len) Frame_Feed(&g_rx_buf[0], recv_len);
            }
            last_pos = recv_len;
        }
    }
    HAL_UART_IRQHandler(&huart);
}

void Bsp_UartAsr_DMA_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_rx); }
```

**注：**PY32F030 DMA request mapping 的具体 API 与宏名以 SDK `py32f0xx_hal_dma.h` 为准。若头文件里是 `HAL_SYSCFG_DMAChannelRemapConfig`/`HAL_SYSCFG_SetDMARemap` 之类的名字，就在 `Bsp_UartAsr_Init` 里紧挨着 `HAL_DMA_Init` 之后调用它把 USART2_RX 映射到 DMA1_Channel1。SDK `USART_IDLE_IT` 例程可以直接参考。

- [ ] **Step 3: 修改 py32f0xx_it.c，把中断转发给 Bsp**

在 `py32f0xx_it.c` 末尾加：

```c
#include "Bsp_UartAsr/Bsp_UartAsr.h"

void USART2_IRQHandler(void)         { Bsp_UartAsr_UART_IRQHandler(); }
void DMA1_Channel1_IRQHandler(void)  { Bsp_UartAsr_DMA_IRQHandler();  }
```

在 `py32f0xx_it.h` 加入声明。

- [ ] **Step 4: 加入 Keil HAL UART/DMA 源文件**

Add `py32f0xx_hal_uart.c` `py32f0xx_hal_uart_ex.c` `py32f0xx_hal_dma.c` 到 Group `HAL`。Add `Bsp_UartAsr.c` 到 Group `BSP`。

- [ ] **Step 5: main.c 回显 loopback 自检**

```c
#include "main.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_UartAsr/Bsp_UartAsr.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    Bsp_Tick_Init();
    Bsp_Led_Init();
    if (!Bsp_Power_Init_WaitConfirm()) while (1) { }

    Bsp_UartAsr_Init();
    Bsp_UartAsr_Send(0x03, 0x00, 0x00);   /* 发一条心跳给 ASR 端 */

    while (1) {
        Bsp_UartAsr_Frame_t f;
        if (Bsp_UartAsr_TryRecv(&f)) {
            Bsp_Led_Toggle(LED_MODE_VOICE);
            Bsp_UartAsr_Send(f.cmd, f.d0, f.d1);  /* 回显 */
        }
    }
}
```

- [ ] **Step 6: 用 USB-TTL 或 ASRPRO 端联调**

用 PC 上的 USB-TTL 接 PF0/PF1（注意交叉：TTL_TX ↔ PF0_RX, TTL_RX ↔ PF1_TX），115200 8N1，发 `A5 03 01 05 00 07`：

- 预期：MCU 回显完全相同的 6 字节；LED4 每收一帧翻转一次。
- 若无回显，先在 Debug 里断点 `Frame_Feed` 检查 DMA 缓冲是否有数据；如果 DMA 缓冲空，说明 DMA request 未映射到 channel 1，需要调用 SDK 的 remap API。

- [ ] **Step 7: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_UartAsr MCU_XiaoBai/User
git commit -m "feat(bsp): USART2 to ASRPRO with DMA+IDLE and 6-byte frame parser"
```

---

### Task 9: Bsp_UartBle - USART1 (PB6/PB7) + DMA + IDLE 中断 + PF3 状态

> **实施后变更（2026-07-06）：** 根据 ECB00CV2 数据手册（V1.4）和遥控协议.md 调整：
> 1. **波特率改为 9600**（ECB00 默认，手册第 10 页；手册未提供改波特率的 AT 命令）
> 2. **PF3 配下拉输入**（手册第 4 页要求；STA 高=连接、低=断开）
> 3. **连接状态只用 PF3**（芯片虽会在 TXD 发 CONNECT OK/DISCONNECT，但本项目不解析）
> 4. **BLE 名称配置**：新增 `Bsp_UartBle_ConfigName()`，发 `AT+NAME=<name>\r\n`，ECB00 默认从机透传无需配主从
> 5. **遥控协议解析**：帧 `5A 97 98 0A C1 [10字节键值位图] CRC A5` 共 16 字节，10 字节位图每键 1 字节，顺序跟 enum 一致（Up/Down/Left/Right/Y/A/X/B/R1/L1）。CRC = 帧头到数据位末位的累加和低 8 位
> 6. **业务联动**：PF3 断→通 触发 `play=07`（蓝牙已连接），通→断 触发 `play=08`（蓝牙已断开）
>
> 以下 plan 原文保留作历史参考，以最终代码为准。

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_UartBle/Bsp_UartBle.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_UartBle/Bsp_UartBle.c`
- Modify: `MCU_XiaoBai/User/py32f0xx_it.c`（加 USART1、DMA1_Channel2/3 中断入口）
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1: Bsp_UartBle.h**

```c
#ifndef __BSP_UART_BLE_H
#define __BSP_UART_BLE_H
#include "py32f0xx_hal.h"

#define BLE_RX_BUF_SIZE   128U

/** 初始化 USART1 115200 8N1 + DMA 收 + IDLE，PF3 输入（BLE_STA） */
void Bsp_UartBle_Init(void);

/** 发送若干字节（阻塞，超时 20ms） */
void Bsp_UartBle_Send(const uint8_t *data, uint16_t len);

/**
 * @brief 读一段收到的数据（尽可能多），复制到 out_buf。
 * @return 实际拷贝的字节数（0 表示无数据）。
 */
uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len);

/** PF3 BLE 连接状态：1 = 已连接（视电路极性，若相反在此取反） */
uint8_t  Bsp_UartBle_IsConnected(void);

void Bsp_UartBle_UART_IRQHandler(void);
void Bsp_UartBle_DMA_IRQHandler(void);

#endif
```

- [ ] **Step 2: Bsp_UartBle.c**

```c
#include "Bsp_UartBle/Bsp_UartBle.h"
#include <string.h>

static UART_HandleTypeDef huart;
static DMA_HandleTypeDef  hdma_rx;
static uint8_t g_rx[BLE_RX_BUF_SIZE];
static volatile uint16_t g_last_pos = 0;

/* 简单读取队列（覆盖式环形，读方与中断方共享 last_pos） */
static uint8_t  g_out[BLE_RX_BUF_SIZE];
static volatile uint16_t g_out_len = 0;

static void Ble_GpioClkInit(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PB6=TX1, PB7=RX1 AF0 */
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF0_USART1;
    gi.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gi);

    /* PF3 BLE_STA 输入 */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_NOPULL;
    gi.Pin  = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOF, &gi);
}

void Bsp_UartBle_Init(void)
{
    Ble_GpioClkInit();

    huart.Instance          = USART1;
    huart.Init.BaudRate     = 115200;
    huart.Init.WordLength   = UART_WORDLENGTH_8B;
    huart.Init.StopBits     = UART_STOPBITS_1;
    huart.Init.Parity       = UART_PARITY_NONE;
    huart.Init.Mode         = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart);

    hdma_rx.Instance                 = DMA1_Channel2;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    HAL_DMA_Init(&hdma_rx);
    __HAL_LINKDMA(&huart, hdmarx, hdma_rx);

    /* 与 Task 8 同注：SYSCFG 里把 USART1_RX 映射到 DMA1_Channel2 */

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&huart, g_rx, BLE_RX_BUF_SIZE);
}

void Bsp_UartBle_Send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart, (uint8_t *)data, len, 20);
}

uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len)
{
    __disable_irq();
    uint16_t n = g_out_len;
    if (n > max_len) n = max_len;
    if (n) memcpy(out_buf, g_out, n);
    g_out_len = 0;
    __enable_irq();
    return n;
}

uint8_t Bsp_UartBle_IsConnected(void)
{
    /* 假设 PF3 = 高 表示已连接。若极性相反，改成 == RESET。 */
    return HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_3) == GPIO_PIN_SET ? 1 : 0;
}

/* IDLE 中断把 [last_pos..cur) 追加到 g_out */
void Bsp_UartBle_UART_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart);

        uint16_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_rx);
        uint16_t cur  = BLE_RX_BUF_SIZE - ndtr;

        if (cur != g_last_pos) {
            uint16_t space = (uint16_t)(BLE_RX_BUF_SIZE - g_out_len);
            uint16_t seg1, seg2;
            if (cur > g_last_pos) {
                seg1 = (uint16_t)(cur - g_last_pos); seg2 = 0;
            } else {
                seg1 = (uint16_t)(BLE_RX_BUF_SIZE - g_last_pos);
                seg2 = cur;
            }
            uint16_t need = seg1 + seg2;
            if (need > space) need = space;
            uint16_t take1 = need > seg1 ? seg1 : need;
            if (take1) { memcpy(&g_out[g_out_len], &g_rx[g_last_pos], take1); g_out_len += take1; }
            uint16_t take2 = need - take1;
            if (take2) { memcpy(&g_out[g_out_len], &g_rx[0], take2); g_out_len += take2; }
            g_last_pos = cur;
        }
    }
    HAL_UART_IRQHandler(&huart);
}

void Bsp_UartBle_DMA_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_rx); }
```

- [ ] **Step 3: 修改 py32f0xx_it.c**

在 py32f0xx_it.c 加：

```c
#include "Bsp_UartBle/Bsp_UartBle.h"

void USART1_IRQHandler(void)             { Bsp_UartBle_UART_IRQHandler(); }
void DMA1_Channel2_3_IRQHandler(void)    { Bsp_UartBle_DMA_IRQHandler();  }
```

在 py32f0xx_it.h 加声明。

- [ ] **Step 4: 加入 Keil**

Add `Bsp_UartBle.c`。

- [ ] **Step 5: main.c 回显 loopback**

```c
#include "Bsp_UartBle/Bsp_UartBle.h"

/* 上面已有的初始化流程后 */
Bsp_UartBle_Init();
uint8_t buf[64];
while (1) {
    uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
    if (n) {
        Bsp_UartBle_Send(buf, n);            /* echo */
        Bsp_Led_Toggle(LED_MODE_REMOTE);
    }
    if (Bsp_UartBle_IsConnected()) Bsp_Led_On(LED_MODE_SENSOR);
    else                           Bsp_Led_Off(LED_MODE_SENSOR);
}
```

- [ ] **Step 6: 用 USB-TTL 接 PB6/PB7 联调 + 拨动 PF3 观察 LED2**

预期：PC 发任意字节，MCU 逐字节 echo；PF3 拉高 LED2 亮，拉低 LED2 灭。

- [ ] **Step 7: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_UartBle MCU_XiaoBai/User
git commit -m "feat(bsp): USART1 to BLE ECB00 with DMA+IDLE + PF3 status"
```

---

### Task 10: Bsp_Adc - PA0/PA1/PA2/PA3 4 通道扫描 + DMA

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Adc/Bsp_Adc.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Adc/Bsp_Adc.c`
- Modify: `MCU_XiaoBai/User/main.c`
- Keil: 加入 `py32f0xx_hal_adc.c` `py32f0xx_hal_adc_ex.c`

- [ ] **Step 1: Bsp_Adc.h**

```c
#ifndef __BSP_ADC_H
#define __BSP_ADC_H
#include "py32f0xx_hal.h"

typedef enum {
    ADC_CH_BATTERY = 0,   /* PA0 IN0 */
    ADC_CH_IR1     = 1,   /* PA1 IN1 */
    ADC_CH_IR2     = 2,   /* PA2 IN2 */
    ADC_CH_IR3     = 3,   /* PA3 IN3 */
    ADC_CH_COUNT
} Bsp_Adc_Channel_t;

/** 初始化 ADC + DMA 连续扫描 4 通道，DMA 循环写入内部缓冲 */
void     Bsp_Adc_Init(void);

/** 读取某通道最新原始 12-bit 值 */
uint16_t Bsp_Adc_Read(Bsp_Adc_Channel_t ch);

#endif
```

- [ ] **Step 2: Bsp_Adc.c**

```c
#include "Bsp_Adc/Bsp_Adc.h"

static ADC_HandleTypeDef hadc;
static DMA_HandleTypeDef hdma_adc;
static volatile uint16_t g_val[ADC_CH_COUNT];

static void Adc_GpioClkInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Mode = GPIO_MODE_ANALOG;
    gi.Pull = GPIO_NOPULL;
    gi.Pin  = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gi);
}

void Bsp_Adc_Init(void)
{
    Adc_GpioClkInit();

    hadc.Instance = ADC1;
    hadc.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode          = ADC_SCAN_DIRECTION_FORWARD;
    hadc.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    hadc.Init.LowPowerAutoWait      = DISABLE;
    hadc.Init.ContinuousConvMode    = ENABLE;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc.Init.DMAContinuousRequests = ENABLE;
    hadc.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc.Init.SamplingTimeCommon    = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_Init(&hadc);

    ADC_ChannelConfTypeDef sc = {0};
    sc.Rank    = ADC_RANK_CHANNEL_NUMBER;
    sc.Channel = ADC_CHANNEL_0; HAL_ADC_ConfigChannel(&hadc, &sc);
    sc.Channel = ADC_CHANNEL_1; HAL_ADC_ConfigChannel(&hadc, &sc);
    sc.Channel = ADC_CHANNEL_2; HAL_ADC_ConfigChannel(&hadc, &sc);
    sc.Channel = ADC_CHANNEL_3; HAL_ADC_ConfigChannel(&hadc, &sc);

    /* DMA */
    hdma_adc.Instance                 = DMA1_Channel3;
    hdma_adc.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc.Init.Mode                = DMA_CIRCULAR;
    hdma_adc.Init.Priority            = DMA_PRIORITY_MEDIUM;
    HAL_DMA_Init(&hdma_adc);
    __HAL_LINKDMA(&hadc, DMA_Handle, hdma_adc);

    /* SYSCFG DMA remap：把 ADC 请求映射到 DMA1_Channel3（若 SDK 头默认已经映射到某个 channel，
       与本文件所选 channel 匹配即可，否则要显式 remap） */

    HAL_ADCEx_Calibration_Start(&hadc);
    HAL_ADC_Start_DMA(&hadc, (uint32_t *)g_val, ADC_CH_COUNT);
}

uint16_t Bsp_Adc_Read(Bsp_Adc_Channel_t ch)
{
    if (ch >= ADC_CH_COUNT) return 0;
    return g_val[ch];
}
```

- [ ] **Step 3: 加入 Keil**

Add `py32f0xx_hal_adc.c` `py32f0xx_hal_adc_ex.c` `Bsp_Adc.c`。

- [ ] **Step 4: main.c 通过 UART 打印 4 通道值**

```c
#include "Bsp_Adc/Bsp_Adc.h"

/* 假定 Bsp_UartBle 或 Bsp_UartAsr 已初始化，用其中一个当调试口 */
Bsp_Adc_Init();
char line[64];
uint32_t t0 = Bsp_Tick_GetMs();
while (1) {
    if (Bsp_Tick_GetMs() - t0 >= 200) {
        t0 = Bsp_Tick_GetMs();
        int n = snprintf(line, sizeof line, "BAT=%u IR1=%u IR2=%u IR3=%u\r\n",
                         Bsp_Adc_Read(ADC_CH_BATTERY),
                         Bsp_Adc_Read(ADC_CH_IR1),
                         Bsp_Adc_Read(ADC_CH_IR2),
                         Bsp_Adc_Read(ADC_CH_IR3));
        if (n > 0) Bsp_UartBle_Send((uint8_t*)line, (uint16_t)n);
    }
}
```

- [ ] **Step 5: 用串口助手抓 200ms 一行的四路值**

预期：4 个数值在 0..4095 之间。手指遮挡红外反射管时对应通道 IR1/IR2/IR3 数值变化明显；电池电压稳定不动。

- [ ] **Step 6: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Adc MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): ADC scan of 4 channels via DMA (battery + 3x IR)"
```

---

### Task 11: Bsp_IR + Bsp_Battery - 红外发射控制 + 读通道封装

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_IR/Bsp_IR.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_IR/Bsp_IR.c`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Battery/Bsp_Battery.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Battery/Bsp_Battery.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1: Bsp_IR.h**

```c
#ifndef __BSP_IR_H
#define __BSP_IR_H
#include "py32f0xx_hal.h"

/** 初始化 PF4 = 高（三路红外发射常亮）。ADC 通道由 Bsp_Adc 提供。 */
void     Bsp_IR_Init(void);

/** 读 3 路红外反射 12-bit 原始值 */
uint16_t Bsp_IR_ReadCh1(void);   /* PA1 */
uint16_t Bsp_IR_ReadCh2(void);   /* PA2 */
uint16_t Bsp_IR_ReadCh3(void);   /* PA3 */

/** 关闭 / 打开红外发射 */
void     Bsp_IR_Emit(uint8_t on);

#endif
```

- [ ] **Step 2: Bsp_IR.c**

```c
#include "Bsp_IR/Bsp_IR.h"
#include "Bsp_Adc/Bsp_Adc.h"

#define IR_PORT   GPIOF
#define IR_PIN    GPIO_PIN_4

void Bsp_IR_Init(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = IR_PIN;
    HAL_GPIO_Init(IR_PORT, &gi);
    HAL_GPIO_WritePin(IR_PORT, IR_PIN, GPIO_PIN_SET);   /* 上电常亮 */
}

void Bsp_IR_Emit(uint8_t on)
{
    HAL_GPIO_WritePin(IR_PORT, IR_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint16_t Bsp_IR_ReadCh1(void) { return Bsp_Adc_Read(ADC_CH_IR1); }
uint16_t Bsp_IR_ReadCh2(void) { return Bsp_Adc_Read(ADC_CH_IR2); }
uint16_t Bsp_IR_ReadCh3(void) { return Bsp_Adc_Read(ADC_CH_IR3); }
```

- [ ] **Step 3: Bsp_Battery.h**

```c
#ifndef __BSP_BATTERY_H
#define __BSP_BATTERY_H
#include "py32f0xx_hal.h"

void     Bsp_Battery_Init(void);      /* 目前无 GPIO 需初始化，占位便于后续扩展 */
uint16_t Bsp_Battery_ReadRaw(void);   /* 12-bit 原始 ADC 值 */

#endif
```

- [ ] **Step 4: Bsp_Battery.c**

```c
#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_Adc/Bsp_Adc.h"

void     Bsp_Battery_Init(void)      { /* ADC 已由 Bsp_Adc 管理 */ }
uint16_t Bsp_Battery_ReadRaw(void)   { return Bsp_Adc_Read(ADC_CH_BATTERY); }
```

- [ ] **Step 5: 加入 Keil**

- [ ] **Step 6: main.c 遮挡红外验证**

```c
#include "Bsp_IR/Bsp_IR.h"
#include "Bsp_Battery/Bsp_Battery.h"

Bsp_Adc_Init();
Bsp_IR_Init();
Bsp_Battery_Init();

while (1) {
    if (Bsp_IR_ReadCh2() > 3000) Bsp_Led_On(LED_MODE_SENSOR);
    else                          Bsp_Led_Off(LED_MODE_SENSOR);
    Bsp_Tick_DelayMs(50);
}
```

- [ ] **Step 7: 手指靠近中间红外发射管的对应接收位置**

预期：手指靠近时 LED2 亮，移开熄灭。阈值 3000 是原始值参考，实际据板子微调。

- [ ] **Step 8: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_IR MCU_XiaoBai/BSP_Drivers/Bsp_Battery MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): IR emit control + IR/Battery reader wrappers"
```

---

### Task 12: Bsp_Tm1640 - 8×14 眼睛点阵驱动（软件位翻转）

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Tm1640/Bsp_Tm1640.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp_Tm1640/Bsp_Tm1640.c`
- Modify: `MCU_XiaoBai/User/main.c`

TM1640 协议要点（详见协议规划文档"TM1640 驱动方式"）：

- 二线制：CLK=PA4，DIN=PA5，空闲时都高
- Start：CLK 高时 DIN 由高变低；Stop：CLK 高时 DIN 由低变高
- 每位数据：CLK 下降沿改 DIN，CLK 上升沿被采样，LSB 先发
- 命令：`0x40`=数据自增写；`0xC0|addr`=起始地址；`0x88|bright(0..7)`=显示亮度（`0x8F`最亮开）
- 显存 16 字节 × 8 位，每字节对应一列（GRID）8 行（SEG）

- [ ] **Step 1: Bsp_Tm1640.h**

```c
#ifndef __BSP_TM1640_H
#define __BSP_TM1640_H
#include "py32f0xx_hal.h"

#define TM1640_COLS  14        /* 我们只用 14 列 */
#define TM1640_ROWS  8

/** 初始化 GPIO + 清屏 + 打开显示 */
void Bsp_Tm1640_Init(void);

/** 全屏刷新，data[0..13] = 14 列，每列 bit0..bit7 = 8 行像素 */
void Bsp_Tm1640_Refresh(const uint8_t data[TM1640_COLS]);

/** 清屏 */
void Bsp_Tm1640_Clear(void);

/** 显示亮度 0..7；0=最暗、7=最亮 */
void Bsp_Tm1640_SetBrightness(uint8_t level);

#endif
```

- [ ] **Step 2: Bsp_Tm1640.c**

```c
#include "Bsp_Tm1640/Bsp_Tm1640.h"

#define CLK_PORT  GPIOA
#define CLK_PIN   GPIO_PIN_4
#define DIN_PORT  GPIOA
#define DIN_PIN   GPIO_PIN_5

#define CLK_H()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_SET)
#define CLK_L()   HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_RESET)
#define DIN_H()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_SET)
#define DIN_L()   HAL_GPIO_WritePin(DIN_PORT, DIN_PIN, GPIO_PIN_RESET)

static void Delay_Bit(void) { for (volatile int i = 0; i < 20; i++) __NOP(); }

static void tm_Start(void) { CLK_H(); DIN_H(); Delay_Bit(); DIN_L(); Delay_Bit(); CLK_L(); }
static void tm_Stop(void)  { CLK_L(); DIN_L(); Delay_Bit(); CLK_H(); Delay_Bit(); DIN_H(); }

static void tm_WriteByte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        CLK_L();
        if (b & 0x01) DIN_H(); else DIN_L();
        Delay_Bit();
        CLK_H();
        Delay_Bit();
        b >>= 1;
    }
}

static void Tm_GpioInit(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    gi.Pin   = CLK_PIN | DIN_PIN;
    HAL_GPIO_Init(GPIOA, &gi);
    CLK_H(); DIN_H();
}

void Bsp_Tm1640_Init(void)
{
    Tm_GpioInit();
    Bsp_Tm1640_Clear();
    Bsp_Tm1640_SetBrightness(7);
}

void Bsp_Tm1640_Refresh(const uint8_t data[TM1640_COLS])
{
    /* 1. 数据命令：自增 */
    tm_Start(); tm_WriteByte(0x40); tm_Stop();
    /* 2. 地址 0 起，连写 16 字节（14 有效 + 2 补 0） */
    tm_Start();
    tm_WriteByte(0xC0);
    for (int i = 0; i < TM1640_COLS; i++) tm_WriteByte(data[i]);
    tm_WriteByte(0x00); tm_WriteByte(0x00);
    tm_Stop();
}

void Bsp_Tm1640_Clear(void)
{
    uint8_t z[TM1640_COLS] = {0};
    Bsp_Tm1640_Refresh(z);
}

void Bsp_Tm1640_SetBrightness(uint8_t level)
{
    if (level > 7) level = 7;
    tm_Start(); tm_WriteByte((uint8_t)(0x88 | level)); tm_Stop();
}
```

- [ ] **Step 3: 加入 Keil**

- [ ] **Step 4: main.c 显示一个"笑脸"位图**

```c
#include "Bsp_Tm1640/Bsp_Tm1640.h"

Bsp_Tm1640_Init();

/* 14 列 × 8 行 的 "" 位图（示例：两个 3×3 眼睛 + 一条嘴巴） */
static const uint8_t g_smile[14] = {
    0x00, 0x1C, 0x14, 0x1C, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1C, 0x14, 0x1C, 0x00, 0x00,
};
Bsp_Tm1640_Refresh(g_smile);
while (1) { }
```

- [ ] **Step 5: 上电观察点阵**

预期：14×8 点阵亮起两个方块作为眼睛。位图细节允许日后调整，本 Task 只要求点阵能亮、能刷、无鬼影。

- [ ] **Step 6: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp_Tm1640 MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): TM1640 8x14 LED matrix driver (bit-bang) + smile bitmap"
```

---

### Task 13: Bsp - BSP_Init() 汇总入口

**Files:**

- Create: `MCU_XiaoBai/BSP_Drivers/Bsp.h`
- Create: `MCU_XiaoBai/BSP_Drivers/Bsp.c`
- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1: Bsp.h**

```c
#ifndef __BSP_H
#define __BSP_H
#include "py32f0xx_hal.h"

#include "Bsp_Tick/Bsp_Tick.h"
#include "Bsp_Led/Bsp_Led.h"
#include "Bsp_Power/Bsp_Power.h"
#include "Bsp_LedPwm/Bsp_LedPwm.h"
#include "Bsp_Key/Bsp_Key.h"
#include "Bsp_Motor/Bsp_Motor.h"
#include "Bsp_Adc/Bsp_Adc.h"
#include "Bsp_IR/Bsp_IR.h"
#include "Bsp_Battery/Bsp_Battery.h"
#include "Bsp_UartAsr/Bsp_UartAsr.h"
#include "Bsp_UartBle/Bsp_UartBle.h"
#include "Bsp_Tm1640/Bsp_Tm1640.h"

/**
 * @brief 汇总初始化。
 *        内部第一步就调用 Bsp_Power_Init_WaitConfirm，未通过则直接死循环，不返回。
 */
void BSP_Init(void);

#endif
```

- [ ] **Step 2: Bsp.c**

```c
#include "Bsp.h"

void BSP_Init(void)
{
    /* 时钟节拍先行，Power 依赖 Tick */
    Bsp_Tick_Init();

    /* 4 只模式 LED（Power 阶段做进度指示需要） */
    Bsp_Led_Init();

    /* 电源锁存 —— 关键步骤，2s 长按未通过就不再前进 */
    if (!Bsp_Power_Init_WaitConfirm()) {
        while (1) { }
    }

    /* 剩余外设按依赖顺序初始化 */
    Bsp_LedPwm_Init();
    Bsp_LedPwm_PlayStartupBreath();

    Bsp_Key_Init();
    Bsp_Motor_Init();
    Bsp_Motor_StopAll();

    Bsp_Adc_Init();
    Bsp_IR_Init();
    Bsp_Battery_Init();

    Bsp_UartAsr_Init();
    Bsp_UartBle_Init();

    Bsp_Tm1640_Init();
}
```

- [ ] **Step 3: 加入 Keil，`Bsp.c` 放 `BSP` 组顶部**

- [ ] **Step 4: main.c 精简为一行 BSP_Init**

```c
#include "main.h"
#include "Bsp.h"

static void APP_SystemClockConfig(void);

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    BSP_Init();

    /* 开机语：告诉 ASR 端播放 0x01 */
    Bsp_UartAsr_Send(0x01, 0x01, 0x00);

    while (1) {
        /* 后续业务层挂在这 */
    }
}
```

- [ ] **Step 5: 整机烧录 + 完整流程验证**

- 按 KEY1 不到 2s → 掉电
- 按 KEY1 ≥ 2s → LED1..4 逐段点亮 → 全灭 → PA8/PA9 呼吸一次 → ASRPRO 收到 0x01 → 板上一切静止但 MCU 运行
- 期间用 PC 串口助手接 PB6/PB7，能收发；接 PF0/PF1，能收发；把手指靠近红外，用 Debug 观察 `Bsp_Adc_Read(ADC_CH_IR2)` 变化

- [ ] **Step 6: Commit**

```powershell
git add MCU_XiaoBai/BSP_Drivers/Bsp.c MCU_XiaoBai/BSP_Drivers/Bsp.h MCU_XiaoBai/User/main.c
git commit -m "feat(bsp): unified BSP_Init entry aggregating all modules"
```

---

### Task 14: 关机流程接线（业务层 stub）

关机语义："KEY1 长按 ≥ 2 秒（开机后）"→ 由业务层触发 Bsp_Power_ShutDown。本 Task 只在 main.c 挂一个 stub，验证 `Bsp_Power_ShutDown` 能拉低 PA15 让硬件掉电。

**Files:**

- Modify: `MCU_XiaoBai/User/main.c`

- [ ] **Step 1: main.c 加最小状态机**

```c
int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    BSP_Init();

    Bsp_UartAsr_Send(0x01, 0x01, 0x00);   /* 开机语 */

    while (1) {
        Bsp_Key_Id_t id;
        Bsp_Key_Evt_t e = Bsp_Key_Poll(&id);
        if (e == KEY_EVT_LONG && id == KEY_ID_1) {
            Bsp_UartAsr_Send(0x01, 0x02, 0x00);   /* 关机语 */
            Bsp_Tick_DelayMs(1500);               /* 等语音播完（v2 会等 0x83 回执） */
            Bsp_Motor_StopAll();
            Bsp_Led_AllOff();
            Bsp_LedPwm_Set(LEDPWM_1, 0);
            Bsp_LedPwm_Set(LEDPWM_2, 0);
            Bsp_Tm1640_Clear();
            Bsp_Power_ShutDown();
        }
        Bsp_Tick_DelayMs(10);
    }
}
```

- [ ] **Step 2: 板上验证**

开机后长按 KEY1 ≥ 2s → 播关机语 → 所有灯灭 → 电流为 0（PA15 拉低）✅

- [ ] **Step 3: Commit**

```powershell
git add MCU_XiaoBai/User/main.c
git commit -m "feat(app): power-off on KEY1 long-press (stub state machine)"
```

---

## 八、Self-Review（写完自检）

### 8.1 Spec coverage

| 规格条目 | 覆盖 Task |
|----------|-----------|
| PA0 电池 ADC | Task 10 + Task 11 (Bsp_Battery) |
| PA1/PA2/PA3 红外 ADC | Task 10 + Task 11 (Bsp_IR) |
| PA4/PA5 TM1640 | Task 12 |
| PA6/PA7 电机1 PWM | Task 5 |
| PA8/PA9 呼吸灯 PWM | Task 6 |
| PA10/PA11/PA12 模式 LED2/3/4 | Task 3 |
| PA13/PA14 SWD | 保留不动 |
| PA15 电源锁存 | Task 4 (Bsp_Power) |
| PB0/PB1 电机2 PWM | Task 5 |
| PB2 模式 LED1 | Task 3 |
| PB3 KEY1 | Task 4 + Task 7 |
| PB4/PB5/PB8 KEY2/3/4 | Task 7 |
| PB6/PB7 USART1 BLE | Task 9 |
| PF0/PF1 USART2 ASRPRO | Task 8 |
| PF3 BLE_STA | Task 9 |
| PF4 红外发射 | Task 11 |
| 开机 KEY1 长按 2s + 进度 LED | Task 4 |
| 呼吸灯 0→100→0 启动动画 | Task 6 |
| 语音协议 v0.1 | Task 2 + Task 8 |
| 关机（长按开机后） | Task 14 |
| BSP_Init 汇总 | Task 13 |

### 8.2 已知需现场核对的点（不阻塞主流程）

- DMA request 到 channel 的 SYSCFG remap 具体 API 名（Task 8/9/10 已注明）
- `GPIO_AF13_TIM3` / `GPIO_AF1_TIM3` / `GPIO_AF2_TIM1` / `GPIO_AF1_USART2` / `GPIO_AF0_USART1` 宏名在 SDK `py32f0xx_hal_gpio_ex.h` 中需现场确认（PY32 SDK 各版本略有差异）
- PF3 BLE_STA 电平极性（Task 9 里默认高=已连接，反之改一行）
- ADC 采样时间 71.5 周期是保守值，若刷新率不够可调低
- Task 7 里 KEY2/3/4 的业务映射（切模式、切玩法等）在业务层完成，不在 BSP 内

### 8.3 类型/命名一致性

- LED id 全程用 `Bsp_Led_Id_t` (`LED_MODE_POWER/SENSOR/REMOTE/VOICE`)
- 电机 id 全程用 `Bsp_Motor_Id_t` (`MOTOR_LEFT/RIGHT`)
- ADC 通道 id 全程用 `Bsp_Adc_Channel_t` (`ADC_CH_BATTERY/IR1/IR2/IR3`)
- 按键 id 用 `Bsp_Key_Id_t` (`KEY_ID_1..4`)，事件 `Bsp_Key_Evt_t`
- 帧结构 `Bsp_UartAsr_Frame_t` 全程复用

---

## 九、后续里程碑（超出本计划范围）

- M2：模式状态机（动力/感应/遥控/语音）+ 模式 LED 显示业务层
- M3：动力模式电机动作循环
- M4：感应模式（挥手、遇障、明暗、靠近）
- M5：语音模式响应（0x81 → 电机 / 模式切换 / 玩法）
- M6：BLE 遥控协议解析 + 遥控模式
- M7：低电量告警（Bsp_Battery 阈值判断）
- M8：眼睛点阵动画（等待/连接/表情）
