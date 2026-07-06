#ifndef __BSP_UART_BLE_H
#define __BSP_UART_BLE_H
#include "py32f0xx_hal.h"

/*
 * USART1 (PB6=TX / PB7=RX, AF0) <-> ECB00CV2 BLE 芯片
 *   波特率 9600 8N1（ECB00 默认，datasheet 第 10 页）
 *   PF3 = STA 引脚，下拉输入（datasheet 第 4 页要求）
 *
 * ECB00 工作模式：默认就是从机透传，无需 AT 配置主从。
 *   AT 命令以 "AT" 开头 + "\r\n" 结尾，非 AT 数据透传给主机。
 *   连接/断开时 TXD 会发 "CONNECT OK\r\n" / "DISCONNECT\r\n"，
 *   但本项目只用 PF3 电平检测连接状态。
 */

#define BLE_RX_BUF_SIZE   128U

/* --- 遥控器协议（resource/遥控协议.md） ---
 * 帧格式: 5A 97 98 0A C1 [10 字节键值位图] CRC A5
 *   5A    帧头
 *   97    源地址
 *   98    目标地址
 *   0A    数据长度（固定 10）
 *   C1    数据类型码
 *   xx*10 10 字节键值位图，每键 1 字节，0=未按 1=按下
 *   CRC   从帧头到数据位最后一位的累加和取低 8 位
 *   A5    帧尾
 * 总长 16 字节
 */
#define REMOTE_FRAME_HEAD   0x5AU
#define REMOTE_FRAME_TAIL   0xA5U
#define REMOTE_FRAME_LEN    16U
#define REMOTE_KEY_COUNT    10U

/* 按键枚举（跟遥控协议.md 的 enum 顺序一致，对应字节位图 [0..9]） */
typedef enum {
    REMOTE_KEY_UP    = 0,   /* KeyUp    方向上 */
    REMOTE_KEY_DOWN  = 1,   /* KeyDown  方向下 */
    REMOTE_KEY_LEFT  = 2,   /* KeyLeft  方向左 */
    REMOTE_KEY_RIGHT = 3,   /* KeyRight 方向右 */
    REMOTE_KEY_Y     = 4,
    REMOTE_KEY_A     = 5,
    REMOTE_KEY_X     = 6,
    REMOTE_KEY_B     = 7,
    REMOTE_KEY_R1    = 8,   /* R1Key */
    REMOTE_KEY_L1    = 9,   /* L1Key */
} Bsp_RemoteKey_t;

/** 初始化 USART1 9600 8N1 + DMA 收 + IDLE，PF3 下拉输入 */
void Bsp_UartBle_Init(void);

/**
 * @brief 发 AT 命令配置 BLE 名称（如 "LBS_XIAOBAI"）。
 *        阻塞发送 AT+NAME=<name>\r\n，不等回显。
 *        建议 BLE 上电稳定后调用。
 * @param name 名称字符串（ASCII，<=22 字节，不含 \0）
 * @param len  名称长度
 */
void Bsp_UartBle_ConfigName(const char *name, uint8_t len);

/** 发送若干字节（阻塞，超时 20ms） */
void Bsp_UartBle_Send(const uint8_t *data, uint16_t len);

/**
 * @brief 读一段收到的原始数据（尽可能多），复制到 out_buf。
 *        本函数不做协议解析，调用方自己处理。
 * @return 实际拷贝的字节数（0 表示无数据）。
 */
uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len);

/** PF3 BLE 连接状态：1 = 已连接（高电平），0 = 未连接（低电平） */
uint8_t  Bsp_UartBle_IsConnected(void);

void Bsp_UartBle_UART_IRQHandler(void);
void Bsp_UartBle_DMA_IRQHandler(void);

#endif
