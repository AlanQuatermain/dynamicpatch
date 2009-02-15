/*
 *  stub_helper_code.c
 *  DynamicPatch
 *
 *  Created by jim on 27/3/2006.
 *  Copyright (c) 2003-2006 Jim Dovey. Some Rights Reserved.
 *
 *  This work is licensed under a Creative Commons Attribution License.
 *  You are free to use, modify, and redistribute this work, provided you
 *  include the following disclaimer:
 *
 *    Portions Copyright (c) 2003-2006 Jim Dovey
 *
 *  For license details, see:
 *    http://creativecommons.org/licences/by/2.5/
 *
 */

// INCLUDE THIS FILE TO USE -- DO NOT COMPILE DIRECTLY
// Not restricted to particular architectures -- this will be installed
// by a separate application, probably running native, accessing an
// emulated application, to install emulated code

// code block to copy into some PowerPC memory location in a Rosetta
// process
const unsigned char _stub_helper_code[ ] = {
// _patch_global_data:
    0x00,0x00,0x00,0x00, // fmwk_ok
    0x00,0x00,0x00,0x00, // fmwk_path
    0x00,0x00,0x00,0x00, // bind_fn_sym
    0x00,0x00,0x00,0x00, // load_fn
    0x00,0x00,0x00,0x00, // sym_fn
    0x00,0x00,0x00,0x00, // bind_fn
    0x00,0x00,0x00,0x00, // tbl_addr

// _load_patch_framework:
    0x7c,0x08,0x02,0xa6, // mflr    r0              ; copy link register
    0x90,0x01,0x00,0x08, // stw     r0,8(r1)        ; store link register value in linkage area
    0x94,0x21,0xff,0xb0, // stwu    r1,-80(r1)      ; make new stack frame
    0x91,0xa1,0x00,0x38, // stw     r13,56(r1)      ; store register 13 on the stack
    0x91,0xc1,0x00,0x3c, // stw     r14,60(r1)      ; store register 14 on the stack
    0x91,0x21,0x00,0x40, // stw     r9,64(r1)       ; put onto the stack for safe-keeping
    0x80,0x69,0x00,0x04, // lwz     r3,fmwk_path(r9); path to framework binary
    0x38,0x80,0x00,0x00, // li      r4,0            ; no options (will exit on load failure)
    0x81,0x89,0x00,0x0c, // lwz     r12,load_fn(r9) ; address of NSAddImage
    0x7d,0x89,0x03,0xa6, // mtctr   r12
    0x4e,0x80,0x04,0x21, // bctrl                   ; call NSAddImage(fmwk_path,0)
    0x81,0x21,0x00,0x40, // lwz     r9,64(r1)       ; restore our globals ptr
    0x90,0x69,0x00,0x00, // stw     r3,fmwk_ok(r9)  ; note that the framework has been loaded
    0x81,0x89,0x00,0x10, // lwz     r12,sym_fn(r9)  ; load address of dlsym
    0x38,0x60,0xff,0xfe, // li      r3,-2           ; handle = RTLD_DEFAULT
    0x80,0x89,0x00,0x08, // lwz     r4,bind_fn_sym(r9); load address of symbol-name string
    0x7d,0x89,0x03,0xa6, // mtctr   r12
    0x4e,0x80,0x04,0x21, // bctrl                   ; call dlsym(RTLD_DEFAULT,bind_fn_sym)
    0x81,0x21,0x00,0x40, // lwz     r9,64(r1)       ; restore our globals ptr
    0x90,0x69,0x00,0x14, // stw     r3,bind_fn(r9)  ; store returned function address
    0x81,0xa1,0x00,0x38, // lwz     r13,56(r1)      ; restore caller's r13
    0x81,0xc1,0x00,0x3c, // lwz     r14,60(r1)      ; restore caller's r14
    0x80,0x01,0x00,0x58, // lwz     r0,88(r1)       ; get old link register value
    0x38,0x21,0x00,0x50, // addi    r1,r1,80        ; restore old stack pointer
    0x7c,0x08,0x03,0xa6, // mtlr    r0              ; restore link register
    0x4e,0x80,0x00,0x20, // blr                     ; return to calling function

// _ctrl_stub_binder_interface:
    0x7c,0x08,0x02,0xa6, // mflr    r0              ; get link register value
    0x93,0xe1,0xff,0xfc, // stw     r31,-4(r1)      ; save frame pointer in reg save area
    0x90,0x01,0x00,0x08, // stw     r0,8(r1)        ; save link register in linkage area
    0x94,0x21,0xff,0x80, // stwu    r1,-128(r1)     ; save stack pointer & update
    0x7c,0x3f,0x0b,0x78, // mr      r31,r1          ; make new frame pointer from stack pointer
    0x90,0x61,0x00,0x38, // stw     r3,56(r1)
    0x90,0x81,0x00,0x3c, // stw     r4,60(r1)
    0x90,0xa1,0x00,0x40, // stw     r5,64(r1)
    0x90,0xc1,0x00,0x44, // stw     r6,68(r1)
    0x90,0xe1,0x00,0x48, // stw     r7,72(r1)
    0x91,0x01,0x00,0x4c, // stw     r8,76(r1)
    0x91,0x21,0x00,0x50, // stw     r9,80(r1)
    0x91,0x41,0x00,0x54, // stw     r10,84(r1)
    0x3d,0x20,0xde,0xad, // lis     r9,0xDEAD       ; load address of globals table (compiled in)
    0x61,0x29,0xf0,0x0d, // ori     r9,r9,0xF00D
    0x91,0x21,0x00,0x58, // stw     r9,88(r1)       ; store globals ptr on stack
    0x80,0x69,0x00,0x00, // lwz     r3,fmwk_ok(r9)  ; load variable
    0x2c,0x03,0x00,0x00, // cmpwi   r3,0            ; see if it's zero
    0x41,0x82,0xff,0x49, // beql    __load_patch_framework ; if zero, library needs to be loaded
    0x81,0x21,0x00,0x58, // lwz     r9,88(r1)       ; restore globals ptr
    0x7d,0xc3,0x73,0x78, // mr      r3,r14          ; move stub info index into first parameter
    0x7d,0xa4,0x6b,0x78, // mr      r4,r13          ; move address of fn addr variable into second parameter
    0x80,0xA9,0x00,0x18, // lwz     r5,tbl_addr(r9) ; load address of data table into third parameter
    0x81,0x89,0x00,0x14, // lwz     r12,bind_fn(r9) ; load address of bind function
    0x7d,0x89,0x03,0xa6, // mtctr   r12
    0x4e,0x80,0x04,0x21, // bctrl                   ; call bind function
    0x7c,0x6c,0x1b,0x78, // mr      r12,r3          ; move patch function address into r12
    0x7d,0x89,0x03,0xa6, // mtctr   r12             ; move patch addr into count register
    0x80,0x01,0x00,0x88, // lwz     r0,136(r1)      ; get old link register value
    0x80,0x61,0x00,0x38, // lwz     r3,56(r1)
    0x80,0x81,0x00,0x3c, // lwz     r4,60(r1)
    0x80,0xa1,0x00,0x40, // lwz     r5,64(r1)
    0x80,0xc1,0x00,0x44, // lwz     r6,68(r1)
    0x80,0xe1,0x00,0x48, // lwz     r7,72(r1)
    0x81,0x01,0x00,0x4c, // lwz     r8,76(r1)
    0x81,0x21,0x00,0x50, // lwz     r9,80(r1)
    0x81,0x41,0x00,0x54, // lwz     r10,84(r1)
    0x38,0x21,0x00,0x80, // addi    r1,r1,128       ; restore old stack pointer
    0x7c,0x08,0x03,0xa6, // mtlr    r0              ; restore old link register
    0x83,0xe1,0xff,0xfc, // lwz     r31,-4(r1)      ; restore frame ptr from reg save area
    0x4e,0x80,0x04,0x20  // bctr                    ; jump to newly-bound patch function

};

// offsets to the place we need to paste in address of this lump
// these are byte offsets
#define table_offset_hi   186     // high 2 bytes go here
#define table_offset_lo   190     // low 2 bytes go here

// offsets (again, in bytes) to variables in the data block
// only the ones which we actually set at 'compile' time
#define fmwk_path_offset    4
#define bind_fn_sym_offset  8
#define load_fn_offset      12
#define sym_fn_offset       16
#define table_addr_offset   24
#define stub_code_offset    136
