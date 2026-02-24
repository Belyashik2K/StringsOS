__asm__("jmp kmain");

// --------------- Constants ---------------
// Video
#define VIDEO_BUF_PTR   (0xB8000)
#define VIDEO_WIDTH     (80)
#define VIDEO_HEIGHT    (25)

// GDT
#define GDT_CS          (0x8)

// IDT
#define IDT_TYPE_INTR   (0x0E)

// PIC
#define PIC1_CMD        (0x20)
#define PIC1_DATA       (0x21)
#define PIC2_CMD        (0xA0)
#define PIC2_DATA       (0xA1)

// Keyboard
#define KBD_DATA_PORT   (0x60)
#define KBD_STAT_PORT   (0x64)

// VGA Cursor
#define CURSOR_PORT     (0x3D4)

// Boot params
#define BOOT_MODE_ADDR  (0x90000)

// Command buffer
#define CMD_MAX_LEN     (40)
#define CMD_BUF_SIZE    (CMD_MAX_LEN + 1)

// Template buffer
#define TEMPLATE_MAX_LEN (40)
#define TEMPLATE_BUF_SIZE (TEMPLATE_MAX_LEN + 1)

// Keyboard scan codes
#define SCANCODE_ESCAPE     (1)
#define SCANCODE_BACKSPACE  (14)
#define SCANCODE_ENTER      (28)
#define SCANCODE_SHIFT_L    (42)
#define SCANCODE_SHIFT_R    (54)
#define SCANCODE_SHIFT_L_REL (170)
#define SCANCODE_SHIFT_R_REL (182)
#define SCANCODE_E0_PREFIX  (0xE0)

// Colors
#define COLOR_DEFAULT       (0x07)

// ACPI
#define ACPI_PWR_CMD        (0x604)
#define ACPI_PWR_VALUE      (0x2000)

// --------------- Port I/O ---------------
static inline unsigned char inb(unsigned short port) {
    unsigned char data;
    __asm__ volatile ("inb %w1, %b0" : "=a"(data) : "Nd"(port));
    return data;
}

static inline void outb(unsigned short port, unsigned char data) {
    __asm__ volatile ("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short data) {
    __asm__ volatile ("outw %w0, %w1" : : "a"(data), "Nd"(port));
}

// --------------- IDT ---------------
typedef void (*intr_handler)();

struct idt_entry {
    unsigned short base_lo;
    unsigned short segm_sel;
    unsigned char always0;
    unsigned char flags;
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

static idt_entry g_idt[256];
static idt_ptr g_idtp;

static void intr_reg_handler(int num, unsigned short segm_sel, unsigned short flags, intr_handler hndlr) {
    unsigned int addr = (unsigned int) hndlr;
    g_idt[num].base_lo = (unsigned short) (addr & 0xFFFF);
    g_idt[num].segm_sel = segm_sel;
    g_idt[num].always0 = 0;
    g_idt[num].flags = (unsigned char) flags;
    g_idt[num].base_hi = (unsigned short) ((addr >> 16) & 0xFFFF);
}

static void intr_init() {
    for (int i = 0; i < 256; i++) {
        g_idt[i].base_lo = 0;
        g_idt[i].segm_sel = 0;
        g_idt[i].always0 = 0;
        g_idt[i].flags = 0;
        g_idt[i].base_hi = 0;
    }
}

static void intr_start() {
    g_idtp.base = (unsigned int) (&g_idt[0]);
    g_idtp.limit = (unsigned short) (sizeof(g_idt) - 1);
    __asm__ volatile ("lidt %0" : : "m"(g_idtp));
}

static void intr_enable() { __asm__ volatile ("sti"); }

static void intr_disable() { __asm__ volatile ("cli"); }

// --------------- PIC ---------------
static void pic_remap() {
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

static void pic_allow_only_keyboard() {
    outb(PIC1_DATA, (unsigned char) (0xFF ^ 0x02)); // only IRQ1 enabled
    outb(PIC2_DATA, 0xFF);
}

// --------------- VGA output ---------------
static unsigned int g_str = 0;
static unsigned int g_pos = 0;

static void cursor_moveto(unsigned int strnum, unsigned int pos) {
    unsigned short new_pos = (unsigned short) (strnum * VIDEO_WIDTH + pos);
    outb(CURSOR_PORT, 0x0F);
    outb(CURSOR_PORT + 1, (unsigned char) (new_pos & 0xFF));
    outb(CURSOR_PORT, 0x0E);
    outb(CURSOR_PORT + 1, (unsigned char) ((new_pos >> 8) & 0xFF));
}

static void screen_clear() {
    unsigned char *video = (unsigned char *) VIDEO_BUF_PTR;
    for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = COLOR_DEFAULT;
    }
    g_str = 0;
    g_pos = 0;
    cursor_moveto(g_str, g_pos);
}

static void out_char(int color, unsigned char c) {
    if (c == '\n') {
        g_pos = 0;
        g_str++;
        if (g_str >= VIDEO_HEIGHT) screen_clear();
        cursor_moveto(g_str, g_pos);
        return;
    }

    unsigned char *video = (unsigned char *) VIDEO_BUF_PTR;
    unsigned int idx = 2 * (g_str * VIDEO_WIDTH + g_pos);
    video[idx] = c;
    video[idx + 1] = (unsigned char) color;

    g_pos++;
    if (g_pos >= VIDEO_WIDTH) {
        g_pos = 0;
        g_str++;
        if (g_str >= VIDEO_HEIGHT) screen_clear();
    }
    cursor_moveto(g_str, g_pos);
}

static void out_str(int color, const char *s) {
    for (int i = 0; s[i]; i++)
        out_char(color, (unsigned char) s[i]);
}

static void out_uint(int color, unsigned int x) {
    char buf[11];
    int n = 0;
    if (x == 0) {
        out_char(color, '0');
        return;
    }
    while (x > 0 && n < 10) {
        buf[n++] = (char) ('0' + (x % 10));
        x /= 10;
    }
    for (int i = n - 1; i >= 0; i--) out_char(color, (unsigned char) buf[i]);
}

static void prompt() {
    out_str(COLOR_DEFAULT, "# ");
}

// --------------- Command buffer ---------------
static volatile char cmd[CMD_BUF_SIZE];
static volatile unsigned int cmd_len = 0;
static volatile unsigned char cmd_ready = 0;

static void cmd_reset() {
    for (int i = 0; i < CMD_BUF_SIZE; i++) cmd[i] = 0;
    cmd_len = 0;
    cmd_ready = 0;
}

// --------------- Keyboard mapping ---------------
static const char scan_codes[128] =
        {
                0, 27,
                '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
                8, 0,
                'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
                0, 0,
                'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
                0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
                0, '*', 0, ' ',
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '+', 0, 0, 0, 0
        };

static unsigned char boot_mode = 0; // 1=bm,2=std
static volatile unsigned char shift_down = 0;
static volatile unsigned char e0_prefix = 0;

static int is_allowed(char c) {
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

static void erase_last_char_on_screen() {
    if (g_pos > 2) {
        g_pos--;
        unsigned char *video = (unsigned char *) VIDEO_BUF_PTR;
        unsigned int idx = 2 * (g_str * VIDEO_WIDTH + g_pos);
        video[idx] = ' ';
        video[idx + 1] = COLOR_DEFAULT;
        cursor_moveto(g_str, g_pos);
    }
}

static void on_key(unsigned char sc) {
    if (sc == SCANCODE_E0_PREFIX) {
        e0_prefix = 1;
        return;
    }
    if (e0_prefix) {
        e0_prefix = 0;
        return;
    } // ignore extended keys

    if (sc == SCANCODE_SHIFT_L || sc == SCANCODE_SHIFT_R) {
        shift_down = 1;
        return;
    }     // shift press
    if (sc == SCANCODE_SHIFT_L_REL || sc == SCANCODE_SHIFT_R_REL) {
        shift_down = 0;
        return;
    }   // shift release

    if (sc == SCANCODE_BACKSPACE) // backspace
    {
        if (cmd_len == 0) return;
        cmd_len--;
        cmd[cmd_len] = 0;
        erase_last_char_on_screen();
        return;
    }

    if (sc == SCANCODE_ENTER) // enter
    {
        out_char(COLOR_DEFAULT, '\n');
        cmd[cmd_len] = 0;
        cmd_ready = 1;
        return;
    }

    if (sc & 0x80) return; // other releases

    char c = scan_codes[(unsigned int) sc];
    if (!c) return;

    if (shift_down && c >= 'a' && c <= 'z')
        c = (char) (c - 'a' + 'A');

    if (!is_allowed(c)) return;
    if (cmd_len >= CMD_MAX_LEN) return;

    cmd[cmd_len++] = c;
    cmd[cmd_len] = 0;
    out_char(COLOR_DEFAULT, (unsigned char) c);
}

extern "C" void keyb_process_keys() {
    if (inb(KBD_STAT_PORT) & 0x01) {
        unsigned char sc = inb(KBD_DATA_PORT);
        on_key(sc);
    }
}

__attribute__((naked)) void keyb_handler() {
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

static int str_length(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int starts_with(const char *s, const char *pfx) {
    int i = 0;
    while (pfx[i]) {
        if (s[i] != pfx[i]) return 0;
        i++;
    }
    return 1;
}

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char) (c - 'a' + 'A');
    return c;
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char) (c - 'A' + 'a');
    return c;
}

// --------------- Template/Search data ---------------
static volatile char template_buf[TEMPLATE_BUF_SIZE];
static volatile unsigned int template_len = 0;
static volatile unsigned char template_loaded = 0;

// Horspool shift table (bm mode)
static unsigned char bm_shift[256];

static void bm_build_shift_table() {
    unsigned int m = template_len;

    // Для m=0/1 таблица не нужна, но сделаем безопасно
    if (m == 0) {
        for (int i = 0; i < 256; i++) bm_shift[i] = 0;
        return;
    }
    if (m == 1) {
        for (int i = 0; i < 256; i++) bm_shift[i] = 1;
        return;
    }

    // Как в лабе: default = m-1 (а не m)
    for (int i = 0; i < 256; i++) bm_shift[i] = (unsigned char) (m - 1);

    // Для всех символов кроме последнего: shift = (m-1-i)
    for (unsigned int i = 0; i + 1 < m; i++) {
        unsigned char ch = (unsigned char) template_buf[i];
        bm_shift[ch] = (unsigned char) ((m - 1) - i);
    }
}


static void print_template_loaded() {
    out_str(COLOR_DEFAULT, "Template '");
    for (unsigned int i = 0; i < template_len; i++)
        out_char(COLOR_DEFAULT, (unsigned char) template_buf[i]);
    out_str(COLOR_DEFAULT, "' loaded.\n");
}

static void print_bm_info() {
    out_str(COLOR_DEFAULT, "BM info:\n");

    // print unique chars from template in order, like: s:5 t:4 ...
    unsigned char seen[256];
    for (int i = 0; i < 256; i++) seen[i] = 0;

    for (unsigned int i = 0; i < template_len; i++) {
        unsigned char ch = (unsigned char) template_buf[i];
        if (seen[ch]) continue;
        seen[ch] = 1;

        out_char(COLOR_DEFAULT, (unsigned char) ch);
        out_char(COLOR_DEFAULT, ':');
        out_uint(COLOR_DEFAULT, (unsigned int) bm_shift[ch]);
        out_char(COLOR_DEFAULT, ' ');
    }
    out_char(COLOR_DEFAULT, '\n');
}

// naive search (std)
static int std_search(const char *text, unsigned int n, const char *pat, unsigned int m) {
    if (m == 0) return 0;
    if (m > n) return -1;

    for (unsigned int i = 0; i + m <= n; i++) {
        unsigned int j = 0;
        while (j < m && text[i + j] == pat[j]) j++;
        if (j == m) return (int) i;
    }
    return -1;
}

// Horspool search (bm)
static int bm_search(const char *text, unsigned int n, const char *pat, unsigned int m) {
    if (m == 0) return 0;
    if (m > n) return -1;

    unsigned int i = m - 1;
    while (i < n) {
        unsigned int k = 0;
        while (k < m && pat[m - 1 - k] == text[i - k]) k++;
        if (k == m)
            return (int) (i - (m - 1));

        unsigned char c = (unsigned char) text[i];
        unsigned int sh = (unsigned int) bm_shift[c];
        if (sh == 0) sh = 1; // на всякий случай
        i += sh;
    }
    return -1;
}


// --------------- Commands ---------------
typedef void (*cmd_handler_t)(const char *);

struct Command {
    const char *name;
    cmd_handler_t handler;
};

static void cmd_info(const char *) {
    out_str(COLOR_DEFAULT, "Author: Sokolov Dmitrii Andreevich\n");
    out_str(COLOR_DEFAULT, "OS: Linux\n");
    out_str(COLOR_DEFAULT, "Bootloader: FASM\n");
    out_str(COLOR_DEFAULT, "Compiler: g++ (gcc)\n");
    out_str(COLOR_DEFAULT, "Mode: ");
    if (boot_mode == 1) out_str(COLOR_DEFAULT, "bm\n");
    else if (boot_mode == 2) out_str(COLOR_DEFAULT, "std\n");
    else out_str(COLOR_DEFAULT, "unknown\n");
}

static void cmd_shutdown(const char *) {
    out_str(COLOR_DEFAULT, "Shutting down...\n");
    outw(ACPI_PWR_CMD, ACPI_PWR_VALUE);
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_upcase(const char *s) {
    for (int i = 0; s[i]; i++) out_char(COLOR_DEFAULT, (unsigned char) to_upper(s[i]));
    out_char(COLOR_DEFAULT, '\n');
}

static void cmd_downcase(const char *s) {
    for (int i = 0; s[i]; i++) out_char(COLOR_DEFAULT, (unsigned char) to_lower(s[i]));
    out_char(COLOR_DEFAULT, '\n');
}

static void cmd_titlize(const char *s) {
    int new_word = 1;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c == ' ') {
            new_word = 1;
            out_char(COLOR_DEFAULT, ' ');
            continue;
        }
        if (new_word) {
            out_char(COLOR_DEFAULT, (unsigned char) to_upper(to_lower(c)));
            new_word = 0;
        } else {
            out_char(COLOR_DEFAULT, (unsigned char) to_lower(c));
        }
    }
    out_char(COLOR_DEFAULT, '\n');
}

static void cmd_template(const char *s) {
    // load template from s into template_buf (max TEMPLATE_MAX_LEN)
    template_len = 0;
    for (int i = 0; i < TEMPLATE_BUF_SIZE; i++) template_buf[i] = 0;

    // skip leading spaces
    int i = 0;
    while (s[i] == ' ') i++;

    while (s[i] && template_len < TEMPLATE_MAX_LEN) {
        template_buf[template_len++] = s[i++];
    }

    template_loaded = (template_len > 0) ? 1 : 0;

    if (!template_loaded) {
        out_str(COLOR_DEFAULT, "No template provided.\n");
        return;
    }

    print_template_loaded();

    if (boot_mode == 1) // bm
    {
        bm_build_shift_table();
        print_bm_info();
    }
}

static void cmd_search(const char *s) {
    if (!template_loaded) {
        out_str(COLOR_DEFAULT, "No template loaded.\n");
        return;
    }

    // copy search string into local buffer (max 40)
    char text[TEMPLATE_BUF_SIZE];
    unsigned int n = 0;

    int i = 0;
    while (s[i] == ' ') i++;
    while (s[i] && n < TEMPLATE_MAX_LEN) {
        text[n++] = s[i++];
    }
    text[n] = 0;

    int pos;
    if (boot_mode == 1) // bm
        pos = bm_search(text, n, (const char *) template_buf, template_len);
    else
        pos = std_search(text, n, (const char *) template_buf, template_len);

    if (pos >= 0) {
        out_str(COLOR_DEFAULT, "Found '");
        for (unsigned int k = 0; k < template_len; k++)
            out_char(COLOR_DEFAULT, (unsigned char) template_buf[k]);
        out_str(COLOR_DEFAULT, "' at pos: ");
        out_uint(COLOR_DEFAULT, (unsigned int) pos);
        out_char(COLOR_DEFAULT, '\n');
    } else {
        out_str(COLOR_DEFAULT, "Not found '");
        for (unsigned int k = 0; k < template_len; k++)
            out_char(COLOR_DEFAULT, (unsigned char) template_buf[k]);
        out_str(COLOR_DEFAULT, "'\n");
    }
}

static const Command g_commands[] = {
        {"info",     cmd_info},
        {"shutdown", cmd_shutdown},
        {"upcase",   cmd_upcase},
        {"downcase", cmd_downcase},
        {"titlize",  cmd_titlize},
        {"template", cmd_template},
        {"search",   cmd_search},
};

static bool cmd_name_matches(const char* input, const char* name, int name_len)
{
    for (int j = 0; j < name_len; j++) {
        if (input[j] != name[j]) {
            return false;
        }
    }
    char next = input[name_len];
    return (next == 0 || next == ' ');
}

static const Command* find_command(const char* input)
{
    for (int i = 0; i < 7; i++) {
        const Command &cmd = g_commands[i];
        int name_len = str_length(cmd.name);
        if (cmd_name_matches(input, cmd.name, name_len)) {
            return &cmd;
        }
    }
    return nullptr;
}

static void dispatch_command(const char *input) {
    while (*input == ' ') input++;
    if (*input == 0) return;

    const Command* cmd = find_command(input);
    if (cmd != nullptr) {
        int name_len = str_length(cmd->name);
        const char *args = input + name_len;
        if (*args == ' ') args++;
        cmd->handler(args);
        return;
    }

    out_str(COLOR_DEFAULT, "Unknown command\n");
}

// --------------- Entry ---------------
extern "C" int kmain() {
    boot_mode = *(volatile unsigned char *) BOOT_MODE_ADDR; // 1=bm,2=std

    screen_clear();
    out_str(COLOR_DEFAULT, "Welcome to StringsOS!\n");
    prompt();
    g_pos = 2;
    cursor_moveto(g_str, g_pos);

    cmd_reset();

    intr_disable();
    pic_remap();
    intr_init();

    intr_reg_handler(0x21, GDT_CS, 0x80 | IDT_TYPE_INTR, (intr_handler) keyb_handler);

    intr_start();
    pic_allow_only_keyboard();
    intr_enable();

    while (1) {
        if (cmd_ready) {
            cmd_ready = 0;
            dispatch_command((const char *) cmd);
            cmd_reset();
            prompt();
            g_pos = 2;
            cursor_moveto(g_str, g_pos);
        }
        __asm__ volatile ("hlt");
    }

    return 0;
}
