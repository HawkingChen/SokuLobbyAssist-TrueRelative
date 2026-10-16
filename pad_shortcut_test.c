#include "PadShortcut.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(!pad_shortcut_buttons(0));
    assert(!pad_shortcut_buttons(0x10));
    assert(!pad_shortcut_buttons(0x20));
    assert(!pad_shortcut_buttons(0x1000)); /* A is not Select */
    assert(!pad_shortcut_buttons(0x1010));
    assert(pad_shortcut_buttons(0x30));
    assert(pad_shortcut_buttons(0x1030));
    assert(pad_shortcut_buttons(UINT16_MAX));
    bool was_down = false;
    assert(pad_shortcut_pressed(&was_down, true, true, false));
    assert(!pad_shortcut_pressed(&was_down, true, true, false));
    assert(!pad_shortcut_pressed(&was_down, false, true, false));
    assert(pad_shortcut_pressed(&was_down, true, true, false));
    assert(!pad_shortcut_pressed(&was_down, false, true, false));
    assert(!pad_shortcut_pressed(&was_down, true, true, true));
    assert(!pad_shortcut_pressed(&was_down, true, true, false));
    assert(!pad_shortcut_pressed(&was_down, true, false, false));
    assert(pad_shortcut_pressed(&was_down, true, true, false));
    puts("Physical controller chord tests passed");
    return 0;
}
