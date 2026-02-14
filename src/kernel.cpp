extern "C" void kmain()
{
    // params at 0x90000 (0x9000:0)
    unsigned char mode = *(volatile unsigned char*)0x90000; // 1=bm, 2=std

    unsigned short* vga = (unsigned short*)0xB8000;
    unsigned short blank = (0x0F << 8) | ' ';

    // clear screen
    for (int i = 0; i < 80 * 25; i++) vga[i] = blank;

    const char* hello = "Hello from StringsOS";
    int pos = 0;
    for (int i = 0; hello[i]; i++) vga[pos++] = (0x0F << 8) | hello[i];

    const char* m1 = " [mode=bm]";
    const char* m2 = " [mode=std]";
    const char* m0 = " [mode=unknown]";

    const char* tag = (mode == 1) ? m1 : (mode == 2) ? m2 : m0;
    for (int i = 0; tag[i]; i++) vga[pos++] = (0x0F << 8) | tag[i];

    while (1) asm volatile("hlt");
}
