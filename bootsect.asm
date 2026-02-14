; bootsect.asm
; BIOS bootloader (FASM)
; Loads kernel from floppy B: and jumps to it
; Linux + FASM + gcc, variant 9

use16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Read kernel from floppy B: (DL = 1)
    mov dl, 0x01        ; drive B:
    mov ch, 0x00        ; cylinder
    mov dh, 0x00        ; head
    mov cl, 0x01        ; sector
    mov al, 0x30        ; number of sectors (48)
    mov bx, 0x1000
    mov es, bx
    xor bx, bx          ; ES:BX = 0x1000:0
    mov ah, 0x02
    int 0x13
    jc error

    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load GDT
    lgdt [gdt_desc]

    ; Enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pmode

error:
    hlt
    jmp error

use32
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000

    call 0x00010000     ; call kernel

halt:
    hlt
    jmp halt

gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF

gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

times 510 - ($ - $$) db 0
dw 0xAA55
