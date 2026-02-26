; bootsect.asm (FASM) — waits for "bm" or "std" without Enter, stores choice,
; then loads kernel from floppy B: to 0x1000:0000 (linear 0x10000) and jumps.

use16
org BOOT_ORG

STATE_IDLE = 0
STATE_B    = 1
STATE_S    = 2
STATE_ST   = 3
CMD_STATE equ bl

BOOT_ORG    = 7C00h
STACK_TOP   = BOOT_ORG     ; если так задумано

; where to store boot params (chosen by student)
PARAM_SEG  equ 0x9000
PARAM_OFF  equ 0x0000
; layout:
; [0] = mode (1 = bm, 2 = std)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti

    ; init params
    mov ax, PARAM_SEG
    mov es, ax
    xor di, di
    mov byte [es:di], 0

    ; move cursor to top-left (0, 0)
    mov ah, 0x02
    mov bh, 0x00
    mov dx, 0x0000
    int 0x10

    ; clear screen (BIOS scroll up entire window)
    mov ax, 0x0600      ; scroll up entire window
    mov bh, 0x07        ; attribute (white on black)
    mov cx, 0x0000      ; upper-left corner
    mov dx, 0x184F      ; lower-right corner (80x25)
    int 0x10

    ; print prompt "Enter algorithm (bm/std): "
    mov si, prompt_msg
    call print_string

    ; cmd_state (CMD_STATE):
    ; IDLE -> 'b' -> (expect 'm')  => "bm"
    ; IDLE -> 's' -> 't' -> (expect 'd') => "std"
    xor CMD_STATE, CMD_STATE          ; CMD_STATE = cmd_state (STATE_IDLE)

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

; --- load kernel from floppy B: into 0x1000:0000 ---
load_kernel:
    cli

    mov dl, 0x01        ; drive B:
    mov ch, 0x00        ; cylinder
    mov dh, 0x00        ; head
    mov cl, 0x01        ; sector
    mov al, 0x30        ; 48 sectors
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    mov ah, 0x02
    int 0x13
    jc disk_error

    ; enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; load GDT
    lgdt [gdt_info]

    ; enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

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
    mov esp, 0x90000

    call 0x00010000     ; kernel entry (linked to 0x10000)

halt:
    hlt
    jmp halt

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_info:
    dw gdt_end - gdt - 1
    dd gdt

; print_string: prints null-terminated string at DS:SI using BIOS teletype
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

prompt_msg db "Enter algorithm (bm/std): ", 0

times (512 - ($ - start) - 2) db 0
dw 0xAA55
