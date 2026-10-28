#include <drivers/usb/usb.h>
char usb_key_ascii(uint16_t usage, uint8_t modifiers, bool caps)
{
    bool shift = (modifiers & 0x22) != 0;
    if (usage >= 4 && usage <= 29) {
        if (modifiers & 0x11) return (char)(usage - 3);
        return (char)((shift != caps ? 'A' : 'a') + usage - 4);
    }
    if (usage >= 30 && usage <= 39) return (shift ? "!@#$%^&*()" : "1234567890")[usage - 30];
    if (usage == 40) return '\r';
    if (usage == 41) return 27;
    if (usage == 42) return 127;
    if (usage == 43) return '\t';
    if (usage == 44) return ' ';
    if (usage >= 45 && usage <= 56) return (shift ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[usage - 45];
    return 0;
}
