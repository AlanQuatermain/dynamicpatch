;
;  stub_binding_helper.s
;  DynamicPatch
;
;  Created by jim on 27/3/2006.
;  Copyright (c) 2003-2006 Jim Dovey. Some Rights Reserved.
;
;  This work is licensed under a Creative Commons Attribution License.
;  You are free to use, modify, and redistribute this work, provided you
;  include the following disclaimer:
;
;    Portions Copyright (c) 2003-2006 Jim Dovey
;
;  For license details, see:
;    http://creativecommons.org/licences/by/2.5/
;
;

; This file contains the Rosetta stub binding assembler routines. Their
; job is to load the patching framework, lookup the address of the bind
; function proper, and call it to link to the real patch function. This
; file is not built with the project by default; rather, you can compile
; it once, then use your favourite hex editor to find the code bytes, 
; which can then be included as a byte-array within the finished program.
; The bytes for this implementation can be found in stub_code_bytes.c.

#ifdef __ppc__

    .text
    .align 2

; GLOBALS

; offsets into globals table
#define fmwk_ok         0
#define fmwk_path       4
#define bind_fn_sym     8
#define load_fn         12
#define sym_fn          16
#define bind_fn         20
#define tbl_addr        24

; this is global so I can look for its address in the symbol table & use 
; Epsilon's hex-mode to get the code bytes
    .globl  __rosetta_stub_data
__rosetta_stub_data:
    .long               0 ; fmwk_ok: initialized to zero, set to address of framework header when loaded
    .long               0 ; fmwk_path: initialized to ptr to c-string fqpn of framework binary
    .long               0 ; bind_fn_sym: initialized as ptr to c-string (name of stub binding function)
    .long               0 ; load_fn: initialized to address of NSAddImage
    .long               0 ; sym_fn: initialized to address of dlsym
    .long               0 ; bind_fn: initialized to zero -- addr of stub binding fn added below
    .long               0 ; tbl_addr: initialized to address of data table

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; LOAD FRAMEWORK FUNCTION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; This function handles loading of the patch framework itself. It will also look up
; the address of the stub binding function, storing that address in the data block above.
    .private_extern __load_patch_framework
__load_patch_framework:
    mflr    r0                  ; copy link register
    stw     r0,8(r1)            ; store in linkage area
    stwu    r1,-80(r1)          ; make new stack frame

    stw     r13,56(r1)          ; store caller's r13
    stw     r14,60(r1)          ; store caller's r14

    ; The address of the globals table is still in r9 -- so save it
    stw     r9,64(r1)           ; store it on the stack

    ; Call NSAddImage
    lwz     r3,fmwk_path(r9)    ; path to framework binary
    li      r4,0                ; no options (will exit on load failure)
    lwz     r12,load_fn(r9)     ; address of NSAddImage
    mtctr   r12
    bctrl                       ; call NSAddImage(fmwk_path, 0)

    ; If we get here, the framework is loaded, along with dependancies
    lwz     r9,64(r1)           ; restore globals ptr
    stw     r3,fmwk_ok(r9)      ; store framework's mach_header* in the globals

    ; lookup patch stub binding function
    lwz     r12,sym_fn(r9)      ; r12 <- address of dlsym
    li      r3,-2               ; r3 <- RTLD_DEFAULT
    lwz     r4,bind_fn_sym(r9)  ; r4 <- symbol name
    mtctr   r12
    bctrl                       ; call dlsym(RTLD_DEFAULT, symbol)
    lwz     r9,64(r1)           ; restore globals ptr
    stw     r3,bind_fn(r9)      ; store address of bind function in globals

    lwz     r13,56(r1)          ; restore caller's r13
    lwz     r14,60(r1)          ; restore caller's r14
    lwz     r0,88(r1)           ; get old link register value
    addi    r1,r1,80            ; restore old stack pointer
    mtlr    r0                  ; restore link register
    blr                         ; return to calling function

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PATCH STUB BINDING HELPER INTERFACE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; This function is the default target for all patch branch islands under Rosetta.
; It expects two parameters, in non-standard registers (to not interfere with
; other things), as follows:
;
;   r13 <- address of memory containing the branch_target address in the island
;   r14 <- index into the patch table, used to identify the patch to link & load
;
; It will pass these into the real function (implemented in C, in rosetta_patch.c),
; which will lookup the patch info, load its bundle, and let the bundle link with
; its reentry islands. Once it's done that, it will lookup the address of the
; patch function associated with this call, and will both return that address in
; r3, and will store it using the pointer provided to this asm code in r13. That 
; way, the next time the patch island executes, it will branch directly to the
; patch function, rather than going through here.
;
; This is pretty much what the dyld stub binding helper does.
    .private_extern __patch_stub_binder_interface
__patch_stub_binder_interface:
    mflr    r0                  ; save link register
    stw     r31,-4(r1)          ; store frame pointer in reg save area
    stw     r0,8(r1)            ; store link in linkage area
    stwu    r1,-128(r1)         ; save current stack pointer & grow stack
    mr      r31,r1              ; make new frame pointer from stack pointer

    ; save all registers which might contain parameters
    stw     r3,56(r1)
    stw     r4,60(r1)
    stw     r5,64(r1)
    stw     r6,68(r1)
    stw     r7,72(r1)
    stw     r8,76(r1)
    stw     r9,80(r1)
    stw     r10,84(r1)

    ; get the address of the globals table into r9
    lis     r9,0xDEAD
    ori     r9,r9,0xF00D
    stw     r9,88(r1)           ; store it on the stack

    ; if necessary, load the patch framework
    lwz     r3,fmwk_ok(r9)      ; load variable
    cmpwi   r3,0                ; see if it's been set
    beql    __load_patch_framework ; if zero, framework needs to be loaded

    lwz     r9,88(r1)           ; restore globals pointer
    mr      r3,r14              ; move patch info index into first parameter
    mr      r4,r13              ; move address of branch target variable into second parameter
    lwz     r5,tbl_addr(r9)     ; load address of data table into third parameter
    lwz     r12,bind_fn(r9)     ; load bind function address
    mtctr   r12
    bctrl                       ; call bind function

    mr      r12,r3              ; move patch function address into r12
    mtctr   r12                 ; move into count register before resetting the stack

    lwz     r0,136(r1)          ; get old link value

    ; restore parameter registers
    lwz     r3,56(r1)
    lwz     r4,60(r1)
    lwz     r5,64(r1)
    lwz     r6,68(r1)
    lwz     r7,72(r1)
    lwz     r8,76(r1)
    lwz     r9,80(r1)
    lwz     r10,84(r1)

    addi    r1,r1,128           ; restore old stack pointer
    mtlr    r0                  ; restore link register
    lwz     r31,-4(r1)          ; restore frame pointer from reg save area

    bctr                        ; jump to newly-bound patch function

#endif
