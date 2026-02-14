__asm__("jmp kmain");

#define VIDEO_BUF_PTR  (0xB8000)
#define VIDEO_WIDTH    (80)
#define VIDEO_HEIGHT   (25)
#define GDT_CS         (0x8)

#define IDT_TYPE_INTR  (0x0E)

#define PIC1_CMD   (0x20)
#define PIC1_DATA  (0x21)
#define PIC2_CMD   (0xA0)
#define PIC2_DATA  (0xA1)

#define KBD_DATA_PORT  (0x60)
#define KBD_STAT_PORT  (0x64)

#define CURSOR_PORT (0x3D4)

// --------------- Port I/O ---------------
static inline unsigned char inb(unsigned short port)
{
    unsigned char data;
    __asm__ volatile ("inb %w1, %b0" : "=a"(data) : "Nd"(port));
    return data;
}
static inline void outb(unsigned short port, unsigned char data)
{
    __asm__ volatile ("outb %b0, %w1" : : "a"(data), "Nd"(port));
}
static inline void outw(unsigned short port, unsigned short data)
{
    __asm__ volatile ("outw %w0, %w1" : : "a"(data), "Nd"(port));
}

// --------------- IDT ---------------
typedef void (*intr_handler)();

struct idt_entry
{
    unsigned short base_lo;
    unsigned short segm_sel;
    unsigned char  always0;
    unsigned char  flags;
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr
{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

static idt_entry g_idt[256];
static idt_ptr   g_idtp;

static void intr_reg_handler(int num, unsigned short segm_sel, unsigned short flags, intr_handler hndlr)
{
    unsigned int addr = (unsigned int)hndlr;
    g_idt[num].base_lo  = (unsigned short)(addr & 0xFFFF);
    g_idt[num].segm_sel = segm_sel;
    g_idt[num].always0  = 0;
    g_idt[num].flags    = (unsigned char)flags;
    g_idt[num].base_hi  = (unsigned short)((addr >> 16) & 0xFFFF);
}

static void intr_init()
{
    for (int i = 0; i < 256; i++)
    {
        g_idt[i].base_lo = 0;
        g_idt[i].segm_sel = 0;
        g_idt[i].always0 = 0;
        g_idt[i].flags = 0;
        g_idt[i].base_hi = 0;
    }
}

static void intr_start()
{
    g_idtp.base  = (unsigned int)(&g_idt[0]);
    g_idtp.limit = (unsigned short)(sizeof(g_idt) - 1);
    __asm__ volatile ("lidt %0" : : "m"(g_idtp));
}

static void intr_enable()  { __asm__ volatile ("sti"); }
static void intr_disable() { __asm__ volatile ("cli"); }

// --------------- PIC ---------------
static void pic_remap()
{
    unsigned char a1 = inb(PIC1_DATA);
    unsigned char a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

static void pic_allow_only_keyboard()
{
    outb(PIC1_DATA, (unsigned char)(0xFF ^ 0x02)); // only IRQ1 enabled
    outb(PIC2_DATA, 0xFF);
}

// --------------- VGA output ---------------
static unsigned int g_str = 0;
static unsigned int g_pos = 0;

static void cursor_moveto(unsigned int strnum, unsigned int pos)
{
    unsigned short new_pos = (unsigned short)(strnum * VIDEO_WIDTH + pos);
    outb(CURSOR_PORT, 0x0F);
    outb(CURSOR_PORT + 1, (unsigned char)(new_pos & 0xFF));
    outb(CURSOR_PORT, 0x0E);
    outb(CURSOR_PORT + 1, (unsigned char)((new_pos >> 8) & 0xFF));
}

static void screen_clear()
{
    unsigned char* video = (unsigned char*)VIDEO_BUF_PTR;
    for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT * 2; i += 2)
    {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    g_str = 0;
    g_pos = 0;
    cursor_moveto(g_str, g_pos);
}

static void out_char(int color, unsigned char c)
{
    if (c == '\n')
    {
        g_pos = 0;
        g_str++;
        if (g_str >= VIDEO_HEIGHT) screen_clear();
        cursor_moveto(g_str, g_pos);
        return;
    }

    unsigned char* video = (unsigned char*)VIDEO_BUF_PTR;
    unsigned int idx = 2 * (g_str * VIDEO_WIDTH + g_pos);
    video[idx] = c;
    video[idx + 1] = (unsigned char)color;

    g_pos++;
    if (g_pos >= VIDEO_WIDTH)
    {
        g_pos = 0;
        g_str++;
        if (g_str >= VIDEO_HEIGHT) screen_clear();
    }
    cursor_moveto(g_str, g_pos);
}

static void out_str(int color, const char* s)
{
    for (int i = 0; s[i]; i++)
        out_char(color, (unsigned char)s[i]);
}

static void out_uint(int color, unsigned int x)
{
    char buf[11];
    int n = 0;
    if (x == 0) { out_char(color, '0'); return; }
    while (x > 0 && n < 10)
    {
        buf[n++] = (char)('0' + (x % 10));
        x /= 10;
    }
    for (int i = n - 1; i >= 0; i--) out_char(color, (unsigned char)buf[i]);
}

static void prompt()
{
    out_str(0x07, "# ");
}

// --------------- Command buffer ---------------
static volatile char cmd[41];
static volatile unsigned int cmd_len = 0;
static volatile unsigned char cmd_ready = 0;

static void cmd_reset()
{
    for (int i = 0; i < 41; i++) cmd[i] = 0;
    cmd_len = 0;
    cmd_ready = 0;
}

// --------------- Keyboard mapping ---------------
static const char scan_codes[128] =
        {
                0, 27,
                '1','2','3','4','5','6','7','8','9','0','-','=',
                8, 0,
                'q','w','e','r','t','y','u','i','o','p','[',']',
                0, 0,
                'a','s','d','f','g','h','j','k','l',';','\'','`',
                0, '\\','z','x','c','v','b','n','m',',','.','/',
                0, '*', 0, ' ',
                0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,'+',0,0,0,0
        };

static unsigned char boot_mode = 0; // 1=bm,2=std
static volatile unsigned char shift_down = 0;
static volatile unsigned char e0_prefix = 0;

static int is_allowed(char c)
{
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == ' ') return 1;
    if (c == '+') return 1;
    if (c == '-') return 1;
    if (c == '/') return 1;
    if (c == '*') return 1;
    return 0;
}

static void erase_last_char_on_screen()
{
    if (g_pos > 2)
    {
        g_pos--;
        unsigned char* video = (unsigned char*)VIDEO_BUF_PTR;
        unsigned int idx = 2 * (g_str * VIDEO_WIDTH + g_pos);
        video[idx] = ' ';
        video[idx + 1] = 0x07;
        cursor_moveto(g_str, g_pos);
    }
}

static void on_key(unsigned char sc)
{
    if (sc == 0xE0) { e0_prefix = 1; return; }
    if (e0_prefix) { e0_prefix = 0; return; } // ignore extended keys

    if (sc == 42 || sc == 54) { shift_down = 1; return; }     // shift press
    if (sc == 170 || sc == 182) { shift_down = 0; return; }   // shift release

    if (sc == 14) // backspace
    {
        if (cmd_len == 0) return;
        cmd_len--;
        cmd[cmd_len] = 0;
        erase_last_char_on_screen();
        return;
    }

    if (sc == 28) // enter
    {
        out_char(0x07, '\n');
        cmd[cmd_len] = 0;
        cmd_ready = 1;
        return;
    }

    if (sc & 0x80) return; // other releases

    char c = scan_codes[(unsigned int)sc];
    if (!c) return;

    if (shift_down && c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');

    if (!is_allowed(c)) return;
    if (cmd_len >= 40) return;

    cmd[cmd_len++] = c;
    cmd[cmd_len] = 0;
    out_char(0x07, (unsigned char)c);
}

extern "C" void keyb_process_keys()
{
    if (inb(KBD_STAT_PORT) & 0x01)
    {
        unsigned char sc = inb(KBD_DATA_PORT);
        on_key(sc);
    }
}

__attribute__((naked)) void keyb_handler()
{
    __asm__ volatile (
            "pusha \n"
            "call keyb_process_keys \n"
            "movb $0x20, %al \n"
            "outb %al, $0x20 \n"
            "popa \n"
            "iret \n"
            );
}

// --------------- Helpers for commands ---------------
static int streq(const char* a, const char* b)
{
    int i = 0;
    while (a[i] && b[i])
    {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return (a[i] == 0 && b[i] == 0);
}

static int starts_with(const char* s, const char* pfx)
{
    int i = 0;
    while (pfx[i])
    {
        if (s[i] != pfx[i]) return 0;
        i++;
    }
    return 1;
}

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}
static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static void print_upcase(const char* s)
{
    for (int i = 0; s[i]; i++) out_char(0x07, (unsigned char)to_upper(s[i]));
    out_char(0x07, '\n');
}
static void print_downcase(const char* s)
{
    for (int i = 0; s[i]; i++) out_char(0x07, (unsigned char)to_lower(s[i]));
    out_char(0x07, '\n');
}
static void print_titlize(const char* s)
{
    int new_word = 1;
    for (int i = 0; s[i]; i++)
    {
        char c = s[i];
        if (c == ' ')
        {
            new_word = 1;
            out_char(0x07, ' ');
            continue;
        }
        if (new_word)
        {
            out_char(0x07, (unsigned char)to_upper(to_lower(c)));
            new_word = 0;
        }
        else
        {
            out_char(0x07, (unsigned char)to_lower(c));
        }
    }
    out_char(0x07, '\n');
}

// --------------- Template/Search data ---------------
static volatile char template_buf[41];
static volatile unsigned int template_len = 0;
static volatile unsigned char template_loaded = 0;

// Horspool shift table (bm mode)
static unsigned char bm_shift[256];

static void bm_build_shift_table()
{
    unsigned int m = template_len;

    // Для m=0/1 таблица не нужна, но сделаем безопасно
    if (m == 0)
    {
        for (int i = 0; i < 256; i++) bm_shift[i] = 0;
        return;
    }
    if (m == 1)
    {
        for (int i = 0; i < 256; i++) bm_shift[i] = 1;
        return;
    }

    // Как в лабе: default = m-1 (а не m)
    for (int i = 0; i < 256; i++) bm_shift[i] = (unsigned char)(m - 1);

    // Для всех символов кроме последнего: shift = (m-1-i)
    for (unsigned int i = 0; i + 1 < m; i++)
    {
        unsigned char ch = (unsigned char)template_buf[i];
        bm_shift[ch] = (unsigned char)((m - 1) - i);
    }
}


static void print_template_loaded()
{
    out_str(0x07, "Template '");
    for (unsigned int i = 0; i < template_len; i++)
        out_char(0x07, (unsigned char)template_buf[i]);
    out_str(0x07, "' loaded.\n");
}

static void print_bm_info()
{
    out_str(0x07, "BM info:\n");

    // print unique chars from template in order, like: s:5 t:4 ...
    unsigned char seen[256];
    for (int i = 0; i < 256; i++) seen[i] = 0;

    for (unsigned int i = 0; i < template_len; i++)
    {
        unsigned char ch = (unsigned char)template_buf[i];
        if (seen[ch]) continue;
        seen[ch] = 1;

        out_char(0x07, (unsigned char)ch);
        out_char(0x07, ':');
        out_uint(0x07, (unsigned int)bm_shift[ch]);
        out_char(0x07, ' ');
    }
    out_char(0x07, '\n');
}

// naive search (std)
static int std_search(const char* text, unsigned int n, const char* pat, unsigned int m)
{
    if (m == 0) return 0;
    if (m > n) return -1;

    for (unsigned int i = 0; i + m <= n; i++)
    {
        unsigned int j = 0;
        while (j < m && text[i + j] == pat[j]) j++;
        if (j == m) return (int)i;
    }
    return -1;
}

// Horspool search (bm)
static int bm_search(const char* text, unsigned int n, const char* pat, unsigned int m)
{
    if (m == 0) return 0;
    if (m > n) return -1;

    unsigned int i = m - 1;
    while (i < n)
    {
        unsigned int k = 0;
        while (k < m && pat[m - 1 - k] == text[i - k]) k++;
        if (k == m)
            return (int)(i - (m - 1));

        unsigned char c = (unsigned char)text[i];
        unsigned int sh = (unsigned int)bm_shift[c];
        if (sh == 0) sh = 1; // на всякий случай
        i += sh;
    }
    return -1;
}


// --------------- Commands ---------------
static void cmd_info()
{
    out_str(0x07, "Author: Sokolov Dmitrii Andreevich\n");
    out_str(0x07, "OS: Linux\n");
    out_str(0x07, "Bootloader: FASM\n");
    out_str(0x07, "Compiler: g++ (gcc)\n");
    out_str(0x07, "Mode: ");
    if (boot_mode == 1) out_str(0x07, "bm\n");
    else if (boot_mode == 2) out_str(0x07, "std\n");
    else out_str(0x07, "unknown\n");
}

static void cmd_shutdown()
{
    out_str(0x07, "Shutting down...\n");
    outw(0x604, 0x2000);
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_template(const char* s)
{
    // load template from s into template_buf (max 40)
    template_len = 0;
    for (int i = 0; i < 41; i++) template_buf[i] = 0;

    // skip leading spaces
    int i = 0;
    while (s[i] == ' ') i++;

    while (s[i] && template_len < 40)
    {
        template_buf[template_len++] = s[i++];
    }

    template_loaded = (template_len > 0) ? 1 : 0;

    if (!template_loaded)
    {
        out_str(0x07, "No template provided.\n");
        return;
    }

    print_template_loaded();

    if (boot_mode == 1) // bm
    {
        bm_build_shift_table();
        print_bm_info();
    }
}

static void cmd_search(const char* s)
{
    if (!template_loaded)
    {
        out_str(0x07, "No template loaded.\n");
        return;
    }

    // copy search string into local buffer (max 40)
    char text[41];
    unsigned int n = 0;

    int i = 0;
    while (s[i] == ' ') i++;
    while (s[i] && n < 40)
    {
        text[n++] = s[i++];
    }
    text[n] = 0;

    int pos;
    if (boot_mode == 1) // bm
        pos = bm_search(text, n, (const char*)template_buf, template_len);
    else
        pos = std_search(text, n, (const char*)template_buf, template_len);

    if (pos >= 0)
    {
        out_str(0x07, "Found '");
        for (unsigned int k = 0; k < template_len; k++)
            out_char(0x07, (unsigned char)template_buf[k]);
        out_str(0x07, "' at pos: ");
        out_uint(0x07, (unsigned int)pos);
        out_char(0x07, '\n');
    }
    else
    {
        out_str(0x07, "Not found '");
        for (unsigned int k = 0; k < template_len; k++)
            out_char(0x07, (unsigned char)template_buf[k]);
        out_str(0x07, "'\n");
    }
}

static void handle_command()
{
    int i = 0;
    while (cmd[i] == ' ') i++;

    const char* s = (const char*)&cmd[i];
    if (s[0] == 0) return;

    if (streq(s, "info")) { cmd_info(); return; }
    if (streq(s, "shutdown")) { cmd_shutdown(); return; }

    if (starts_with(s, "upcase ")) { print_upcase(s + 7); return; }
    if (starts_with(s, "downcase ")) { print_downcase(s + 9); return; }
    if (starts_with(s, "titlize ")) { print_titlize(s + 8); return; }

    if (starts_with(s, "template ")) { cmd_template(s + 9); return; }
    if (starts_with(s, "search ")) { cmd_search(s + 7); return; }

    out_str(0x07, "Unknown command\n");
}

// --------------- Entry ---------------
extern "C" int kmain()
{
    boot_mode = *(volatile unsigned char*)0x90000; // 1=bm,2=std

    screen_clear();
    out_str(0x07, "Welcome to StringsOS!\n");
    prompt();
    g_pos = 2;
    cursor_moveto(g_str, g_pos);

    cmd_reset();

    intr_disable();
    pic_remap();
    intr_init();

    intr_reg_handler(0x21, GDT_CS, 0x80 | IDT_TYPE_INTR, (intr_handler)keyb_handler);

    intr_start();
    pic_allow_only_keyboard();
    intr_enable();

    for (;;)
    {
        if (cmd_ready)
        {
            cmd_ready = 0;
            handle_command();
            cmd_reset();
            prompt();
            g_pos = 2;
            cursor_moveto(g_str, g_pos);
        }
        __asm__ volatile ("hlt");
    }

    return 0;
}
