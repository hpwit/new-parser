// Stands for a library's header you don't edit: this is how the
// ESP32 USB Host HID library's keyboard.h defines the key it hands out.
#pragma once

struct key_return
{
     unsigned char key_char;
     unsigned char key_code;
};
