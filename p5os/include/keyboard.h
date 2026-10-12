#ifndef P5_KEYBOARD_H
#define P5_KEYBOARD_H
#include <types.h>
/* Non-ASCII key codes delivered by kbd_getkey() */
enum {
    KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END,
    KEY_DELETE, KEY_PGUP, KEY_PGDN, KEY_INSERT, KEY_CTRL_ALT_DEL,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
};
void kbd_init(void);
int kbd_getkey(void);           /* blocks (HLT) until a key is pressed */
int kbd_trygetkey(void);        /* -1 if none pending */
u32 kbd_dropped(void);          /* keys lost because the buffer was full */
#endif
