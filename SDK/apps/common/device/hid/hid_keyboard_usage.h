/* Transport-neutral USB HID Keyboard/Keypad Usage Page definitions. */

#ifndef HID_KEYBOARD_USAGE_H
#define HID_KEYBOARD_USAGE_H

#define HID_KEYBOARD_MOD_LCTRL                  0x01
#define HID_KEYBOARD_MOD_LSHIFT                 0x02
#define HID_KEYBOARD_MOD_LALT                   0x04
#define HID_KEYBOARD_MOD_LGUI                   0x08
#define HID_KEYBOARD_MOD_RCTRL                  0x10
#define HID_KEYBOARD_MOD_RSHIFT                 0x20
#define HID_KEYBOARD_MOD_RALT                   0x40
#define HID_KEYBOARD_MOD_RGUI                   0x80

#define HID_KEYBOARD_USAGE_NONE                 0x00
#define HID_KEYBOARD_USAGE_ERROR_ROLLOVER       0x01
#define HID_KEYBOARD_USAGE_POST_FAIL            0x02
#define HID_KEYBOARD_USAGE_ERROR_UNDEFINED      0x03

#define HID_KEYBOARD_USAGE_A                    0x04
#define HID_KEYBOARD_USAGE_C                    0x06
#define HID_KEYBOARD_USAGE_V                    0x19
#define HID_KEYBOARD_USAGE_X                    0x1B
#define HID_KEYBOARD_USAGE_ENTER                0x28
#define HID_KEYBOARD_USAGE_BACKSPACE            0x2A

#define HID_KEYBOARD_USAGE_STANDARD_MIN         0x04
#define HID_KEYBOARD_USAGE_STANDARD_MAX         0xA4
#define HID_KEYBOARD_USAGE_KEYPAD_EXT_MIN       0xB0
#define HID_KEYBOARD_USAGE_KEYPAD_EXT_MAX       0xDD
#define HID_KEYBOARD_USAGE_MODIFIER_MIN         0xE0
#define HID_KEYBOARD_USAGE_MODIFIER_MAX         0xE7

#define HID_KEYBOARD_USAGE_LEFT_CTRL            0xE0
#define HID_KEYBOARD_USAGE_LEFT_SHIFT           0xE1
#define HID_KEYBOARD_USAGE_LEFT_ALT             0xE2
#define HID_KEYBOARD_USAGE_LEFT_GUI             0xE3
#define HID_KEYBOARD_USAGE_RIGHT_CTRL           0xE4
#define HID_KEYBOARD_USAGE_RIGHT_SHIFT          0xE5
#define HID_KEYBOARD_USAGE_RIGHT_ALT            0xE6
#define HID_KEYBOARD_USAGE_RIGHT_GUI            0xE7

#endif /* HID_KEYBOARD_USAGE_H */
