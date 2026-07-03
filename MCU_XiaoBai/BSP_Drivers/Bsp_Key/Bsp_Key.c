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
        uint8_t raw = Key_ReadRaw((Bsp_Key_Id_t)i);
        g_st[i].raw_last     = raw;
        g_st[i].stable       = raw;             /* 用实际电平做初值 */
        g_st[i].change_t     = now;
        g_st[i].press_t      = raw ? now : 0;   /* 如果已按下，press_t 也设为 now */
        g_st[i].release_t    = 0;
        g_st[i].last_short_t = 0;
        g_st[i].wait_double  = 0;
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
