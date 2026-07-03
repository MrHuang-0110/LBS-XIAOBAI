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
    KEY_EVT_NONE  = 0,
    KEY_EVT_SHORT = 1,   /* 松开时立即上报（无 double-click 等待） */
    KEY_EVT_LONG  = 2,   /* 按住超过 2000ms 触发一次 */
} Bsp_Key_Evt_t;

/** 初始化 4 个 KEY GPIO 输入（外部已上拉），采样实际电平做初值以吞掉开机时 KEY1 仍按着的假边沿 */
void Bsp_Key_Init(void);

/**
 * @brief 由主循环周期调用（建议每 10ms 一次）。
 *        每次返回一个键的一个事件；若无事件返回 KEY_EVT_NONE。
 * @param out_id 事件所属键 id（有事件时才有效）
 */
Bsp_Key_Evt_t Bsp_Key_Poll(Bsp_Key_Id_t *out_id);

#endif
