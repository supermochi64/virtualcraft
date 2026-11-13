/* the ROM header (before the interrupt vectors): title, reserved, maker, game code, version */
struct rominfo { char title[20]; unsigned char reserved[5]; char maker[2]; char code[4]; unsigned char version; };
const struct rominfo rom_info __attribute__((section(".rominfo"))) = {
    "VIRTUALCRAFT        ", {0, 0, 0, 0, 0}, "MO", "VBLK", 0};
