# bootsect.asm (GNU AS, Intel syntax) — waits for "bm" or "std" without Enter, stores choice,
# then loads kernel from floppy B: to 0x1000:0000 (linear 0x10000) and jumps.

.intel_syntax noprefix
.code16

# where to store boot params (chosen by student)
.set PARAM_SEG, 0x9000
.set PARAM_OFF, 0x0000
# layout:
# [0] = mode (1 = bm, 2 = std)

.global _start
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    # init params
    mov ax, PARAM_SEG
    mov es, ax
    xor di, di
    xor al, al
    mov es:[di], al

    # --- read keyboard stream until "bm" or "std" appears ---
    # state machine:
    # for "bm": waiting 'b' -> got 'b' -> got 'm'
    # for "std": waiting 's' -> got 's' -> got 't' -> got 'd'
    xor bx, bx          # BL = bm_state (0..1), BH = std_state (0..2)

kbd_loop:
    xor ah, ah
    int 0x16            # wait key, AL = ASCII

    # ----- bm state -----
    cmp bl, 0
    jne bm_need_m
    cmp al, 'b'
    jne bm_reset_check
    mov bl, 1
    jmp std_state

bm_need_m:
    cmp al, 'm'
    jne bm_fail
    # matched "bm"
    mov ax, PARAM_SEG
    mov es, ax
    mov al, 1
    mov [es:PARAM_OFF], al
    jmp load_kernel

bm_fail:
    # if current char is 'b' keep state=1 else reset
    cmp al, 'b'
    jne bm_zero
    mov bl, 1
    jmp std_state
bm_zero:
    xor bl, bl

bm_reset_check:
    # nothing special, continue to std_state
    jmp std_state

    # ----- std state -----
std_state:
    cmp bh, 0
    jne std_need_t
    cmp al, 's'
    jne kbd_loop
    mov bh, 1
    jmp kbd_loop

std_need_t:
    cmp bh, 1
    jne std_need_d
    cmp al, 't'
    jne std_fail_1
    mov bh, 2
    jmp kbd_loop

std_need_d:
    cmp al, 'd'
    jne std_fail_2
    # matched "std"
    mov ax, PARAM_SEG
    mov es, ax
    mov al, 2
    mov [es:PARAM_OFF], al
    jmp load_kernel

std_fail_1:
    # if current char is 's' keep state=1 else reset
    cmp al, 's'
    jne std_zero
    mov bh, 1
    jmp kbd_loop
std_fail_2:
    # if current char is 's' restart at 1 else reset
    cmp al, 's'
    jne std_zero
    mov bh, 1
    jmp kbd_loop
std_zero:
    xor bh, bh
    jmp kbd_loop

# --- load kernel from floppy B: into 0x1000:0000 ---
load_kernel:
    cli

    mov dl, 0x01        # drive B:
    mov ch, 0x00        # cylinder
    mov dh, 0x00        # head
    mov cl, 0x01        # sector
    mov al, 0x30        # 48 sectors
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    mov ah, 0x02
    int 0x13
    jc disk_error

    # enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    # load GDT
    addr32 lgdt gdt_desc

    # enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    .byte 0x66, 0xEA
    .long pmode
    .word 0x08

disk_error:
    hlt
    jmp disk_error

.code32
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000

    mov eax, 0x10000
    call eax    # kernel entry (linked to 0x10000)

halt:
    hlt
    jmp halt

.balign 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_end:

gdt_desc:
    .word gdt_end - gdt - 1
    .long gdt

# pad to 510 bytes and add boot signature
.fill 510 - (. - _start), 1, 0
.word 0xAA55