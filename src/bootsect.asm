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
    mov ax, 0x1000
    mov es, ax
    xor bx, bx          # ES:BX = 1000:0000

    mov si, 3           # retries

read_retry:
    # reset disk system
    mov ah, 0x00
    int 0x13

    # ---- read 18 sectors: C=0 H=0 S=1..18 into 1000:0000 ----
    mov ah, 0x02
    mov al, 18
    mov ch, 0
    mov dh, 0
    mov cl, 1
    xor bx, bx
    int 0x13
    jc read_fail

    # ---- read 18 sectors: C=0 H=1 S=1..18 into 1000:2400 ----
    mov ah, 0x02
    mov al, 18
    mov ch, 0
    mov dh, 1
    mov cl, 1
    mov bx, 0x2400
    int 0x13
    jc read_fail

    # ---- read 12 sectors: C=1 H=0 S=1..12 into 1000:4800 ----
    mov ah, 0x02
    mov al, 12
    mov ch, 1
    mov dh, 0
    mov cl, 1
    mov bx, 0x4800
    int 0x13
    jc read_fail

    jmp read_ok

read_fail:
    dec si
    jnz read_retry
    jmp disk_error

read_ok:
    in al, 0x92
    or al, 2
    out 0x92, al

    # load GDT
    lgdt gdt_desc

    # enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pmode

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
.zero (512 - ($ - _start) - 2)

.word 0xAA55