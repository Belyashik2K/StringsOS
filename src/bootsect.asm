use16
org BOOT_ORG

STATE_IDLE = 0
STATE_B    = 1
STATE_S    = 2
STATE_ST   = 3
CMD_STATE equ bl

BOOT_ORG    = 7C00h
STACK_TOP   = BOOT_ORG

RM_STACK_TOP = 7000h
PM_STACK_TOP = 09FC00h

PARAM_SEG  equ 0x9000
PARAM_OFF  equ 0x0000

DRIVE_B     = 01h

start:
.init_cpu:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, RM_STACK_TOP
    sti
.init_params:
    mov ax, PARAM_SEG
    mov es, ax
    xor di, di
    mov byte [es:PARAM_OFF], 0
.init_video:
    mov ah, 02h
    mov bh, 00h
    mov dx, 0000h
    int 10h

    mov ax, 0600h
    mov bh, 07h
    mov cx, 0000h
    mov dx, 184Fh
    int 10h
.show_prompt:
    mov si, prompt_msg
    call print_string
.init_cmd_state:
    xor CMD_STATE, CMD_STATE
.waiting_for_mode_loop:
    xor ah, ah
    int 0x16

    cmp CMD_STATE, STATE_IDLE
    je  .state_idle

    cmp CMD_STATE, STATE_B
    je  .state_after_b

    cmp CMD_STATE, STATE_S
    je  .state_after_s

    jmp .state_after_st
.state_idle:
    cmp al, 'b'
    je  .enter_state_b

    cmp al, 's'
    je  .enter_state_s

    jmp .waiting_for_mode_loop
.state_after_b:
    cmp al, 'm'
    je  .command_bm_detected

    cmp al, 'b'
    je  .enter_state_b

    cmp al, 's'
    je  .enter_state_s

    mov CMD_STATE, STATE_IDLE
    jmp .waiting_for_mode_loop
.state_after_s:
    cmp al, 't'
    je  .enter_state_st

    cmp al, 's'
    je  .enter_state_s

    cmp al, 'b'
    je  .enter_state_b

    mov CMD_STATE, STATE_IDLE
    jmp .waiting_for_mode_loop
.state_after_st:
    cmp al, 'd'
    je  .command_std_detected

    cmp al, 's'
    je  .enter_state_s

    cmp al, 'b'
    je  .enter_state_b

    mov CMD_STATE, STATE_IDLE
    jmp .waiting_for_mode_loop
.enter_state_b:
    mov CMD_STATE, STATE_B
    jmp .waiting_for_mode_loop
.command_bm_detected:
    mov ax, PARAM_SEG
    mov es, ax
    mov byte [es:PARAM_OFF], 1
    jmp load_kernel
.enter_state_s:
    mov CMD_STATE, STATE_S
    jmp .waiting_for_mode_loop
.enter_state_st:
    mov CMD_STATE, STATE_ST
    jmp .waiting_for_mode_loop
.command_std_detected:
    mov ax, PARAM_SEG
    mov es, ax
    mov byte [es:PARAM_OFF], 2
    jmp load_kernel


load_kernel:
    cli
    mov dl, DRIVE_B
    mov ax, 1000h
    mov es, ax

    mov si, 3

.retry:
    xor ah, ah
    int 13h

    ; Read first sector (18 sectors per track, cylinder 0, head 0)
    ; Then read next sectors (cylinder 0, head 1)
    ; Then read sectors from the next track (cylinder 1, head 0)

    mov al, 18
    mov ch, 0
    mov dh, 0
    mov cl, 1
    xor bx, bx
    call read_chs
    jc  .fail

    mov al, 18
    mov ch, 0
    mov dh, 1
    mov cl, 1
    mov bx, 2400h
    call read_chs
    jc  .fail

    mov al, 12
    mov ch, 1
    mov dh, 0
    mov cl, 1
    mov bx, 4800h
    call read_chs
    jc  .fail

    jmp .ok
.fail:
    dec si
    jnz .retry
    jmp disk_error
.ok:
.enable_a20:
    in  al, 92h
    or  al, 2
    out 92h, al
.load_gdt:
    lgdt [gdt_info]
.enable_protected_mode:
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 08h:protected_mode

read_chs:
    mov ah, 02h
    int 13h
    ret

disk_error:
    hlt
    jmp disk_error

use32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, PM_STACK_TOP

    mov eax, 00010000h
    call eax

halt:
    hlt
    jmp halt

print_string:
    pusha
.ps_loop:
    lodsb
    test al, al
    jz .ps_done
    mov ah, 0x0E
    mov bh, 0x00
    int 0x10
    jmp .ps_loop
.ps_done:
    popa
    ret


align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_info:
    dw gdt_end - gdt - 1
    dd gdt

prompt_msg db "Enter algorithm (bm/std): ", 0

times (512 - ($ - start) - 2) db 0
dw 0xAA55
