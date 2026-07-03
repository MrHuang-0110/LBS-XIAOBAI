#include "Bsp_Key/Bsp_Key.h"
#include "Bsp_Tick/Bsp_Tick.h"

#define KEY_DEBOUNCE_MS   20
#define KEY_LONG_MS       2000

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
    uint8_t  long_reported; /* 长按事件本次按下期间是否已上报 */
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

    /* 用实际电平做 stable 初值：如果 KEY1 还按着（长按 2s 开机后是常态），
     * 这里 stable=1、press_t=now，Poll 会直接进"稳定按下"状态，用户松开时
     * 走"释放但 long_reported==?"分支；这样避免开机那次按下被识别为假事件。 */
    uint32_t now = Bsp_Tick_GetMs();
    for (int i = 0; i < KEY_ID_COUNT; i++) {
        uint8_t raw = Key_ReadRaw((Bsp_Key_Id_t)i);
        g_st[i].raw_last      = raw;
        g_st[i].stable        = raw;
        g_st[i].change_t      = now;
        g_st[i].press_t       = raw ? now : 0;
        /* 关键：如果开机时还按着，直接把 long_reported 置 1，
         * 让"开机那次按下"整段被吞掉——无论后续松开还是继续按满 2s，都不上报事件。 */
        g_st[i].long_reported = raw;
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
                /* 稳定按下 */
                s->press_t = now;
                s->long_reported = 0;
            } else {
                /* 稳定释放 */
                if (s->long_reported) {
                    /* 长按已上报（或开机吞事件标记），忽略这次释放 */
                    /* nothing */
                } else if ((now - s->press_t) < KEY_LONG_MS) {
                    /* 短按：立即上报，无需等 double-click 超时 */
                    *out_id = (Bsp_Key_Id_t)i;
                    return KEY_EVT_SHORT;
                }
            }
        }

        /* 长按检测：稳定按下且尚未上报 */
        if (s->stable && !s->long_reported && (now - s->press_t) >= KEY_LONG_MS) {
            s->long_reported = 1;
            *out_id = (Bsp_Key_Id_t)i;
            return KEY_EVT_LONG;
        }
    }

    return KEY_EVT_NONE;
}
