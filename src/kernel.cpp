__asm__("jmp kmain");

#define VIDEO_BUF_PTR       (0xB8000)
#define VIDEO_WIDTH         (80)
#define VIDEO_HEIGHT        (25)

#define GDT_CS              (0x08)

#define IDT_TYPE_INTR       (0x0E)

#define PIC1_CMD            (0x20)
#define PIC1_DATA           (0x21)
#define PIC2_CMD            (0xA0)
#define PIC2_DATA           (0xA1)

#define KBD_DATA_PORT       (0x60)
#define KBD_STAT_PORT       (0x64)

#define CURSOR_PORT         (0x3D4)

#define BOOT_MODE_ADDR      (0x1984)

#define CMD_MAX_LEN         (40)
#define CMD_BUF_SIZE        (CMD_MAX_LEN + 1)

#define TEMPLATE_MAX_LEN    (40)
#define TEMPLATE_BUF_SIZE   (TEMPLATE_MAX_LEN + 1)

#define SCANCODE_BACKSPACE      (14)
#define SCANCODE_ENTER          (28)
#define SCANCODE_SHIFT_L        (42)
#define SCANCODE_SHIFT_R        (54)
#define SCANCODE_SHIFT_L_REL    (170)
#define SCANCODE_SHIFT_R_REL    (182)
#define SCANCODE_E0_PREFIX      (0xE0)
#define SCANCODE_RELEASE_MASK   (0x80)

#define COLOR_DEFAULT       (0x07)

#define ACPI_PWR_CMD        (0x604)
#define ACPI_PWR_VALUE      (0x2000)

#define INFO_AUTHOR         "Sokolov Dmitrii Andreevich"
#define INFO_OS             "Linux"
#define INFO_BOOTLOADER     "FASM"
#define INFO_COMPILER       "g++ (gcc)"

#define MODE_BM             (1)
#define MODE_STD            (2)

#define INFO_MODE_BM        "bm"
#define INFO_MODE_STD       "std"
#define INFO_MODE_UNKNOWN   "unknown"

#define PROMPT_STR           "> "

typedef void (*interrupt_handler_t)();

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

struct VideoState {
    unsigned int cursor_row = 0;
    unsigned int cursor_col = 0;
};
static VideoState g_video;
static volatile unsigned char * const video_memory = (volatile unsigned char *) VIDEO_BUF_PTR;

static idt_entry g_idt[256];
static idt_ptr g_idt_ptr;

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

static inline void interrupts_enable() { __asm__ volatile ("sti"); }

static inline void interrupts_disable() { __asm__ volatile ("cli"); }

static void idt_register_entry(
        int vector_num,
        unsigned short segment_sel,
        unsigned short flags,
        interrupt_handler_t handler
) {
    unsigned int handler_addr = (unsigned int) handler;
    g_idt[vector_num].base_lo = (unsigned short) (handler_addr & 0xFFFF);
    g_idt[vector_num].segm_sel = segment_sel;
    g_idt[vector_num].always0 = 0;
    g_idt[vector_num].flags = (unsigned char) flags;
    g_idt[vector_num].base_hi = (unsigned short) ((handler_addr >> 16) & 0xFFFF);
}

static void idt_initialize() {
    for (auto &entry_index: g_idt) {
        entry_index.base_lo = 0;
        entry_index.segm_sel = 0;
        entry_index.always0 = 0;
        entry_index.flags = 0;
        entry_index.base_hi = 0;
    }
}

static void idt_load() {
    g_idt_ptr.base = (unsigned int) (&g_idt[0]);
    g_idt_ptr.limit = (unsigned short) (sizeof(g_idt) - 1);
    __asm__ volatile ("lidt %0" : : "m"(g_idt_ptr));
}

static void pic_remap() {
    unsigned char pic1_mask = inb(PIC1_DATA);
    unsigned char pic2_mask = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, pic1_mask);
    outb(PIC2_DATA, pic2_mask);
}

static void pic_enable_keyboard_only() {
    outb(PIC1_DATA, (unsigned char) (0xFF ^ 0x02));
    outb(PIC2_DATA, 0xFF);
}

static void video_set_cursor(unsigned int row, unsigned int col) {
    unsigned short cursor_pos = (unsigned short) (row * VIDEO_WIDTH + col);
    outb(CURSOR_PORT, 0x0F);
    outb(CURSOR_PORT + 1, (unsigned char) (cursor_pos & 0xFF));
    outb(CURSOR_PORT, 0x0E);
    outb(CURSOR_PORT + 1, (unsigned char) ((cursor_pos >> 8) & 0xFF));
    g_video.cursor_row = row;
    g_video.cursor_col = col;
}

static void video_clear() {
    for (int screen_index = 0; screen_index < VIDEO_WIDTH * VIDEO_HEIGHT * 2; screen_index += 2) {
        video_memory[screen_index] = ' ';
        video_memory[screen_index + 1] = COLOR_DEFAULT;
    }
    g_video.cursor_row = 0;
    g_video.cursor_col = 0;
    video_set_cursor(0, 0);
}

static void video_erase_last_char() {
    if (g_video.cursor_col > 0) {
        g_video.cursor_col--;
        unsigned int video_index = 2 * (g_video.cursor_row * VIDEO_WIDTH + g_video.cursor_col);
        video_memory[video_index] = ' ';
        video_memory[video_index + 1] = COLOR_DEFAULT;
        video_set_cursor(g_video.cursor_row, g_video.cursor_col);
    }
}

static void video_putchar(int color, unsigned char character) {
    if (character == '\n') {
        g_video.cursor_col = 0;
        g_video.cursor_row++;
        if (g_video.cursor_row >= VIDEO_HEIGHT) video_clear();
        video_set_cursor(g_video.cursor_row, g_video.cursor_col);
        return;
    }

    unsigned int video_index = 2 * (g_video.cursor_row * VIDEO_WIDTH + g_video.cursor_col);
    video_memory[video_index] = character;
    video_memory[video_index + 1] = (unsigned char) color;

    g_video.cursor_col++;
    if (g_video.cursor_col >= VIDEO_WIDTH) {
        g_video.cursor_col = 0;
        g_video.cursor_row++;
        if (g_video.cursor_row >= VIDEO_HEIGHT) video_clear();
    }
    video_set_cursor(g_video.cursor_row, g_video.cursor_col);
}

static void video_putstr(int color, const char *string) {
    for (int char_index = 0; string[char_index]; char_index++) {
        video_putchar(color, (unsigned char) string[char_index]);
    }
}

static void video_putnum(int color, unsigned int number) {
    char digit_buffer[11];
    int digit_count = 0;

    if (number == 0) {
        video_putchar(color, '0');
        return;
    }

    while (number > 0 && digit_count < 10) {
        digit_buffer[digit_count++] = (char) ('0' + (number % 10));
        number /= 10;
    }

    for (int digit_index = digit_count - 1; digit_index >= 0; digit_index--) {
        video_putchar(color, (unsigned char) digit_buffer[digit_index]);
    }
}

static void video_prompt() {
    video_putstr(COLOR_DEFAULT, PROMPT_STR);
}

// ============================================================================
// INPUT BUFFER
// ============================================================================

static volatile char g_command_buffer[CMD_BUF_SIZE];
static volatile unsigned int g_command_length = 0;
static volatile unsigned char g_command_ready = 0;

static void input_reset() {
    for (int buffer_index = 0; buffer_index < CMD_BUF_SIZE; buffer_index++) {
        g_command_buffer[buffer_index] = 0;
    }
    g_command_length = 0;
    g_command_ready = 0;
}

// ============================================================================
// KEYBOARD
// ============================================================================

static const char g_scancode_to_ascii[128] = {
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

static unsigned char g_boot_mode = 0;
static volatile unsigned char g_shift_pressed = 0;
static volatile unsigned char g_e0_prefix = 0;

static int is_allowed_char(char character) {
    if (character >= 'a' && character <= 'z') return 1;
    if (character >= 'A' && character <= 'Z') return 1;
    if (character >= '0' && character <= '9') return 1;
    if (character == ' ') return 1;
    if (character == '+') return 1;
    if (character == '-') return 1;
    if (character == '/') return 1;
    if (character == '*') return 1;
    return 0;
}

static void keyboard_handle_extended() {
    g_e0_prefix = 1;
}

static void keyboard_handle_shift_press() {
    g_shift_pressed = 1;
}

static void keyboard_handle_shift_release() {
    g_shift_pressed = 0;
}

static void keyboard_handle_backspace() {
    if (g_command_length == 0) return;
    g_command_length--;
    g_command_buffer[g_command_length] = 0;
    video_erase_last_char();
}

static void keyboard_handle_enter() {
    video_putchar(COLOR_DEFAULT, '\n');
    g_command_buffer[g_command_length] = 0;
    g_command_ready = 1;
}

static char keyboard_translate_scancode(unsigned char scancode) {
    char character = g_scancode_to_ascii[scancode];
    if (!character) return 0;

    if (g_shift_pressed && character >= 'a' && character <= 'z') {
        character = (char) (character - 'a' + 'A');
    }

    return character;
}

static void keyboard_append_char(char character) {
    if (!is_allowed_char(character)) return;
    if (g_command_length >= CMD_MAX_LEN) return;

    g_command_buffer[g_command_length++] = character;
    g_command_buffer[g_command_length] = 0;
    video_putchar(COLOR_DEFAULT, (unsigned char) character);
}

static void keyboard_handle_scancode(unsigned char scancode) {
    if (scancode == SCANCODE_E0_PREFIX) {
        keyboard_handle_extended();
        return;
    }
    if (g_e0_prefix) {
        g_e0_prefix = 0;
        return;
    }

    if (scancode == SCANCODE_SHIFT_L || scancode == SCANCODE_SHIFT_R) {
        keyboard_handle_shift_press();
        return;
    }
    if (scancode == SCANCODE_SHIFT_L_REL || scancode == SCANCODE_SHIFT_R_REL) {
        keyboard_handle_shift_release();
        return;
    }

    if (scancode == SCANCODE_BACKSPACE) {
        keyboard_handle_backspace();
        return;
    }

    if (scancode == SCANCODE_ENTER) {
        keyboard_handle_enter();
        return;
    }

    if (scancode & SCANCODE_RELEASE_MASK) return;

    char character = keyboard_translate_scancode(scancode);
    if (!character) return;

    keyboard_append_char(character);
}

extern "C" void keyboard_process_keys() {
    if (inb(KBD_STAT_PORT) & 0x01) {
        unsigned char scancode = inb(KBD_DATA_PORT);
        keyboard_handle_scancode(scancode);
    }
}

__attribute__((naked)) void keyboard_handler() {
    __asm__ volatile (
            "pusha \n"
            "call keyboard_process_keys \n"
            "movb $0x20, %al \n"
            "outb %al, $0x20 \n"
            "popa \n"
            "iret \n"
            );
}

// ============================================================================
// STRING HELPERS
// ============================================================================

static int string_length(const char *string) {
    int length = 0;
    while (string[length]) length++;
    return length;
}

static char char_to_upper(char character) {
    if (character >= 'a' && character <= 'z') {
        return (char) (character - 'a' + 'A');
    }
    return character;
}

static char char_to_lower(char character) {
    if (character >= 'A' && character <= 'Z') {
        return (char) (character - 'A' + 'a');
    }
    return character;
}

// ============================================================================
// TEMPLATE & SEARCH
// ============================================================================

static volatile char g_template_buffer[TEMPLATE_BUF_SIZE];
static volatile unsigned int g_template_length = 0;
static volatile unsigned char g_template_loaded = 0;

static unsigned char g_bm_shift_table[256];

static void bm_build_shift_table() {
    unsigned int pattern_length = g_template_length;

    if (pattern_length == 0) {
        for (int table_index = 0; table_index < 256; table_index++) {
            g_bm_shift_table[table_index] = 0;
        }
        return;
    }
    if (pattern_length == 1) {
        for (int table_index = 0; table_index < 256; table_index++) {
            g_bm_shift_table[table_index] = 1;
        }
        return;
    }

    for (int table_index = 0; table_index < 256; table_index++) {
        g_bm_shift_table[table_index] = (unsigned char) (pattern_length - 1);
    }

    for (unsigned int pattern_index = 0; pattern_index + 1 < pattern_length; pattern_index++) {
        unsigned char pattern_char = (unsigned char) g_template_buffer[pattern_index];
        g_bm_shift_table[pattern_char] = (unsigned char) ((pattern_length - 1) - pattern_index);
    }
}

static void template_print_status() {
    video_putstr(COLOR_DEFAULT, "Template '");
    for (unsigned int char_index = 0; char_index < g_template_length; char_index++) {
        video_putchar(COLOR_DEFAULT, (unsigned char) g_template_buffer[char_index]);
    }
    video_putstr(COLOR_DEFAULT, "' loaded.\n");
}

static void bm_print_shift_table() {
    video_putstr(COLOR_DEFAULT, "BM info:\n");

    unsigned char seen_chars[256];
    for (int char_index = 0; char_index < 256; char_index++) {
        seen_chars[char_index] = 0;
    }

    for (unsigned int pattern_index = 0; pattern_index < g_template_length; pattern_index++) {
        unsigned char pattern_char = (unsigned char) g_template_buffer[pattern_index];
        if (seen_chars[pattern_char]) continue;
        seen_chars[pattern_char] = 1;

        video_putchar(COLOR_DEFAULT, pattern_char);
        video_putchar(COLOR_DEFAULT, ':');
        video_putnum(COLOR_DEFAULT, (unsigned int) g_bm_shift_table[pattern_char]);
        video_putchar(COLOR_DEFAULT, ' ');
    }
    video_putchar(COLOR_DEFAULT, '\n');
}

static int search_naive(const char *text, unsigned int text_length, const char *pattern, unsigned int pattern_length) {
    if (pattern_length == 0) return 0;
    if (pattern_length > text_length) return -1;

    for (unsigned int text_index = 0; text_index + pattern_length <= text_length; text_index++) {
        unsigned int pattern_index = 0;
        while (pattern_index < pattern_length && text[text_index + pattern_index] == pattern[pattern_index]) {
            pattern_index++;
        }
        if (pattern_index == pattern_length) return (int) text_index;
    }
    return -1;
}

static int
search_boyer_moore(const char *text, unsigned int text_length, const char *pattern, unsigned int pattern_length) {
    if (pattern_length == 0) return 0;
    if (pattern_length > text_length) return -1;

    unsigned int text_index = pattern_length - 1;
    while (text_index < text_length) {
        unsigned int match_count = 0;
        while (match_count < pattern_length &&
               pattern[pattern_length - 1 - match_count] == text[text_index - match_count]) {
            match_count++;
        }
        if (match_count == pattern_length) {
            return (int) (text_index - (pattern_length - 1));
        }

        unsigned char text_char = (unsigned char) text[text_index];
        unsigned int shift_amount = (unsigned int) g_bm_shift_table[text_char];
        if (shift_amount == 0) shift_amount = 1;
        text_index += shift_amount;
    }
    return -1;
}

// ============================================================================
// COMMANDS
// ============================================================================

typedef void (*command_handler_t)(const char *);

struct Command {
    const char *name;
    command_handler_t handler;
};

static void cmd_info(const char *) {
    video_putstr(COLOR_DEFAULT, "Author: ");
    video_putstr(COLOR_DEFAULT, INFO_AUTHOR);
    video_putchar(COLOR_DEFAULT, '\n');

    video_putstr(COLOR_DEFAULT, "OS: ");
    video_putstr(COLOR_DEFAULT, INFO_OS);
    video_putchar(COLOR_DEFAULT, '\n');

    video_putstr(COLOR_DEFAULT, "Bootloader: ");
    video_putstr(COLOR_DEFAULT, INFO_BOOTLOADER);
    video_putchar(COLOR_DEFAULT, '\n');

    video_putstr(COLOR_DEFAULT, "Compiler: ");
    video_putstr(COLOR_DEFAULT, INFO_COMPILER);
    video_putchar(COLOR_DEFAULT, '\n');

    video_putstr(COLOR_DEFAULT, "Mode: ");
    if (g_boot_mode == MODE_BM) {
        video_putstr(COLOR_DEFAULT, INFO_MODE_BM);
    } else if (g_boot_mode == MODE_STD) {
        video_putstr(COLOR_DEFAULT, INFO_MODE_STD);
    } else {
        video_putstr(COLOR_DEFAULT, INFO_MODE_UNKNOWN);
    }
    video_putchar(COLOR_DEFAULT, '\n');
}

static void cmd_shutdown(const char *) {
    video_putstr(COLOR_DEFAULT, "Shutting down...\n");
    outw(ACPI_PWR_CMD, ACPI_PWR_VALUE);
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_upcase(const char *input_string) {
    for (int char_index = 0; input_string[char_index]; char_index++) {
        video_putchar(COLOR_DEFAULT, (unsigned char) char_to_upper(input_string[char_index]));
    }
    video_putchar(COLOR_DEFAULT, '\n');
}

static void cmd_downcase(const char *input_string) {
    for (int char_index = 0; input_string[char_index]; char_index++) {
        video_putchar(COLOR_DEFAULT, (unsigned char) char_to_lower(input_string[char_index]));
    }
    video_putchar(COLOR_DEFAULT, '\n');
}

static void cmd_titlize(const char *input_string) {
    bool new_word = true;
    for (int char_index = 0; input_string[char_index]; char_index++) {
        unsigned char character = (unsigned char) input_string[char_index];
        if (character == ' ') {
            new_word = true;
        } else {
            if (new_word) {
                if (character >= 'a' && character <= 'z') {
                    character -= 'a' - 'A';
                }
                new_word = false;
            } else if (character >= 'A' && character <= 'Z') {
                character += 'a' - 'A';
            }
        }
        video_putchar(COLOR_DEFAULT, character);
    }
    video_putchar(COLOR_DEFAULT, '\n');
}

static void cmd_template(const char *arguments) {
    g_template_length = 0;
    for (int buffer_index = 0; buffer_index < TEMPLATE_BUF_SIZE; buffer_index++) {
        g_template_buffer[buffer_index] = 0;
    }

    int arg_index = 0;
    while (arguments[arg_index] == ' ') arg_index++;

    while (arguments[arg_index] && g_template_length < TEMPLATE_MAX_LEN) {
        g_template_buffer[g_template_length++] = arguments[arg_index++];
    }

    g_template_loaded = (g_template_length > 0) ? 1 : 0;

    if (!g_template_loaded) {
        video_putstr(COLOR_DEFAULT, "No template provided.\n");
        return;
    }

    template_print_status();

    if (g_boot_mode == MODE_BM) {
        bm_build_shift_table();
        bm_print_shift_table();
    }
}

static void cmd_search(const char *arguments) {
    if (!g_template_loaded) {
        video_putstr(COLOR_DEFAULT, "No template loaded.\n");
        return;
    }

    char search_text[TEMPLATE_BUF_SIZE];
    unsigned int search_length = 0;

    int arg_index = 0;
    while (arguments[arg_index] == ' ') arg_index++;
    while (arguments[arg_index] && search_length < TEMPLATE_MAX_LEN) {
        search_text[search_length++] = arguments[arg_index++];
    }
    search_text[search_length] = 0;

    int found_position;
    if (g_boot_mode == MODE_BM) {
        found_position = search_boyer_moore(search_text, search_length, (const char *) g_template_buffer,
                                            g_template_length);
    } else {
        found_position = search_naive(search_text, search_length, (const char *) g_template_buffer, g_template_length);
    }

    if (found_position >= 0) {
        video_putstr(COLOR_DEFAULT, "Found '");
        for (unsigned int pattern_index = 0; pattern_index < g_template_length; pattern_index++) {
            video_putchar(COLOR_DEFAULT, (unsigned char) g_template_buffer[pattern_index]);
        }
        video_putstr(COLOR_DEFAULT, "' at pos: ");
        video_putnum(COLOR_DEFAULT, (unsigned int) found_position);
        video_putchar(COLOR_DEFAULT, '\n');
    } else {
        video_putstr(COLOR_DEFAULT, "Not found '");
        for (unsigned int pattern_index = 0; pattern_index < g_template_length; pattern_index++) {
            video_putchar(COLOR_DEFAULT, (unsigned char) g_template_buffer[pattern_index]);
        }
        video_putstr(COLOR_DEFAULT, "'\n");
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

static bool command_name_matches(const char *input, const char *name, int name_length) {
    for (int char_index = 0; char_index < name_length; char_index++) {
        if (input[char_index] != name[char_index]) {
            return false;
        }
    }
    char next_char = input[name_length];
    return (next_char == 0 || next_char == ' ');
}

static const Command *find_command(const char *input) {
    for (int command_index = 0; command_index < 7; command_index++) {
        const Command &command = g_commands[command_index];
        int name_length = string_length(command.name);
        if (command_name_matches(input, command.name, name_length)) {
            return &command;
        }
    }
    return nullptr;
}

static void dispatch_command(const char *input) {
    while (*input == ' ') input++;
    if (*input == 0) return;

    const Command *command = find_command(input);
    if (command != nullptr) {
        int name_length = string_length(command->name);
        const char *arguments = input + name_length;
        if (*arguments == ' ') arguments++;
        command->handler(arguments);
        return;
    }

    video_putstr(COLOR_DEFAULT, "Unknown command\n");
}

// ============================================================================
// ENTRY POINT
// ============================================================================

extern "C" int kmain() {
    g_boot_mode = *(volatile unsigned char *) BOOT_MODE_ADDR;

    video_clear();
    video_putstr(COLOR_DEFAULT, "Welcome to StringsOS!\n");
    video_prompt();
    video_set_cursor(g_video.cursor_row, 2);

    input_reset();

    interrupts_disable();
    pic_remap();
    idt_initialize();

    idt_register_entry(0x21, GDT_CS, 0x80 | IDT_TYPE_INTR, (interrupt_handler_t) keyboard_handler);

    idt_load();
    pic_enable_keyboard_only();
    interrupts_enable();

    for (;;) {
        if (g_command_ready) {
            g_command_ready = 0;
            dispatch_command((const char *) g_command_buffer);
            input_reset();
            video_prompt();
            video_set_cursor(g_video.cursor_row, 2);
        }
        __asm__ volatile ("hlt");
    }
}
