org 0x100

; TODO detect presence of EMM/HIMEM.sys and fail

    ; open bzimage.com
    mov ax, 0x3d00
    mov dx, bzimage_path
    int 0x21

    ; check error
    mov dx, open_err
    jc fatal

    ; store file handle
    mov [bzimage_handle], ax

    ; read first sector of bzimage
    mov ah, 0x3f
    mov dx, bzimage
    mov bx, [bzimage_handle]
    mov cx, 512
    int 0x21

    ; check error
    mov dx, read_err
    jc fatal

    ; pull setup_sects value from header
    movzx ax, byte [k_setup_sects_b]
    shl ax, 9 ; multiply by 512 to get byte count from sector count
    mov [setup_bytes], ax

    ; read remaining setup code
    mov ah, 0x3f
    mov dx, bzimage + 512
    mov bx, [bzimage_handle]
    mov cx, [setup_bytes]
    int 0x21

    ; check error
    mov dx, read_err
    jc fatal

    ; check magic header value
    mov eax, [k_header_magic_d]
    cmp eax, 0x53726448 ; 'HdrS'
    mov dx, not_kernel_err
    jne fatal

    ; pull syssize from header - count of 16 byte paras of system code after setup
    mov eax, [k_syssize_d]
    shl eax, 4 ; multiply by 16 to get bytes
    mov [sys_bytes], eax

    ; calculate sys_load_end pointer
    add eax, [sys_load_ptr]
    mov [sys_load_end], eax

    ; init unreal mode switching in prep for loading kernel to extended memory
    call init_unreal

.sys_load_loop:
    mov ah, 0x3f
    mov dx, readbuf
    mov bx, [bzimage_handle]
    mov cx, READBUF_SIZE
    int 0x21

    ; check error
    mov dx, read_err
    jc fatal

    ; do unreal copy
    mov si, readbuf
    mov edi, [sys_load_ptr]
    mov ecx, READBUF_SIZE
    call copy_unreal

    ; advance load pointer
    add dword [sys_load_ptr], READBUF_SIZE

    ; loop around again if more to read
    mov eax, [sys_load_ptr]
    cmp eax, [sys_load_end]
    jb .sys_load_loop

    ; finished reading kernel
    xchg bx, bx

    ; return to DOS
    mov ah, 0x4c
    int 0x21

;
; helper subroutines
;

; print error message and exit
fatal:
    mov ah, 0x09
    int 0x21
    mov ah, 0x4c
    int 0x21

; initialize unreal mode switching
init_unreal:
    ; setup gdt offset in gdtr
    mov eax, ds
    shl eax, 4
    add eax, gdt
    mov [gdtr.offset], eax

    ret

; copy from low to extended memory (> 1 MiB)
; DS:ESI - source (far pointer)
; EDI - destination (linear address)
; ECX - byte count
; clobbers EAX
copy_unreal:
    ; change base of gdt data entry
    mov eax, edi
    and eax, ~0xfff
    and edi, 0xfff
    mov [gdt.data_base_0_w], ax
    shr eax, 16
    mov [gdt.data_base_16_b], al
    mov [gdt.data_base_24_b], ah

    ; ; zero high bits of ESI
    ; movzx eax, si
    ; mov esi, eax

    ; ; calculate linear offset of DS and add to SI
    ; mov eax, ds
    ; shl eax, 4
    ; add esi, eax

    ; load gdt to prepare to enter protected mode
    cli
    lgdt [gdtr]

    ; enable protected mode
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; save ds/es and load 32 bit segment selectors
    push es
    mov ax, 0x08
    mov es, ax

     ; copy 4 bytes at a time:
    add ecx, 3
    shr ecx, 2

    ; do the copy
    rep movsd

    ; disable protected mode
    mov eax, cr0
    and al, ~1
    mov cr0, eax

    ; restore ds/es
    pop es

    ret

;
; RO data
;

bzimage_path db "C:\doslinux\bzimage", 0
open_err db "Could not open bzImage$"
read_err db "Could not read bzImage$"
not_kernel_err db "bzImage is not a Linux kernel$"

gdt:
    ; entry 0x00 : null
    dq 0

    ; data entry
    dw 0xffff       ; limit 0:15
    .data_base_0_w:
    dw 0x0000       ; base 0:15
    .data_base_16_b:
    db 0x00         ; base 16:23
    db 0b10010010   ; access byte - data
    db 0xcf         ; flags/(limit 16:19). 4 KB granularity + 32 bit mode flags
    .data_base_24_b:
    db 0x00         ; base 24:31
.end:

;
; variables
;

align 4

bzimage_handle: dw 0
setup_bytes: dw 0
sys_bytes: dd 0
sys_load_ptr: dd 0x100000
sys_load_end: dd 0

gdtr:
    dw gdt.end - gdt - 1
.offset:
    dd 0

; kernel real mode header fields
; see https://www.kernel.org/doc/html/latest/x86/boot.html#the-real-mode-kernel-header
k_setup_sects_b     equ bzimage + 0x1f1
k_syssize_d         equ bzimage + 0x1f4
k_header_magic_d    equ bzimage + 0x202

align 16
progend:

READBUF_SIZE equ 4096

readbuf equ progend
bzimage equ progend + READBUF_SIZE
