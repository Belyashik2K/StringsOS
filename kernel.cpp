extern "C" void kmain() {
    unsigned short *vga = (unsigned short *) 0xB8000;
    unsigned short blank = (0x0F << 8) | ' ';   // white on black

    for (int i = 0; i < 80 * 25; i++)
        vga[i] = blank;

    const char *msg = "Hello from StringsOS";

    for (int i = 0; msg[i]; i++)
        vga[i] = (0x0F << 8) | msg[i];

    while (1)
        asm volatile("hlt");
}
