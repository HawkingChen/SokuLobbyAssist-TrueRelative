#ifndef TRUE_RELATIVE_PAD_SHORTCUT_H
#define TRUE_RELATIVE_PAD_SHORTCUT_H
#include <stdbool.h>
#include <stdint.h>

/* XInput START (0x10) and BACK/Select (0x20), on the same controller. */
static inline bool pad_shortcut_buttons(uint16_t buttons)
{
    return (buttons & 0x30u) == 0x30u;
}
/* Consume the edge even when chat blocks it, requiring release to retry. */
static inline bool pad_shortcut_pressed(bool *was_down, bool combo,
                                        bool foreground, bool chatting)
{
    bool down = foreground && combo;
    bool pressed = down && !*was_down && !chatting;
    *was_down = down;
    return pressed;
}
#endif
