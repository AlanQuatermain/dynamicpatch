/*
 *  code_blocks.c
 *  DynamicPatch
 *
 *  Created by jim on 30/3/2006.
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

// The contents of this file could be used by any architecture (well,
// theoretically). Therefore, it's all stored in arrays of single
// bytes, to side-step endianness issues. In practice, however, the
// PowerPC insertion code is only ever inserted by a PowerPC process,
// and the Intel and Rosetta code is only ever inserted using Intel
// processors. One Rosetta application could patch another, of course,
// but that is currently untested, and in this first revision is
// actually disabled.

#if defined(__ppc__)
# define insertion_code         _ppc_insertion_code
# define oah_insertion_code     _ia32_insertion_code
# define NewThreadFnOffset      _ppc_newthr_offset
# define oahNewThreadFnOffset   _ia32_newthr_offset
#elif defined(__i386__)
# define insertion_code         _ia32_insertion_code
# define oah_insertion_code     _ppc_insertion_code
# define NewThreadFnOffset      _ia32_newthr_offset
# define oahNewThreadFnOffset   _ppc_newthr_offset
#else
# error Unsupported architecture
#endif

// there are a couple of commented-out lines in the
// NewThreadStartFunction blocks below. These are where I've changed
// the second argument of pthread_create() to be NULL.

// stored as bytes to side-step endian issues
const unsigned char _ppc_insertion_code[ ] = {

//_PthreadStartFunction:
    0x7c,0x08,0x02,0xa6,  //    mflr r0
    0xbf,0xc1,0xff,0xf8,  //    stmw   r30,-8(r1)
    0x7c,0x7e,0x1b,0x78,  //    mr     r30,r3
    0x90,0x01,0x00,0x08,  //    stw    r0,8(r1)
    0x94,0x21,0xff,0xb0,  //    stwu   r1,-80(r1)
    0x80,0x03,0x00,0x10,  //    lwz    r0,16(r3)
    0x81,0x9e,0x00,0x0c,  //    lwz    r12,12(r30)
    0x38,0x63,0x00,0x48,  //    addi   r3,r3,72
    0x2f,0x80,0x00,0x00,  //    cmpwi  cr7,r0,0
    0x41,0x9e,0x00,0x2c,  //    beq    cr7,L2
    0x38,0x80,0x00,0x0a,  //    li     r4,10
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x2f,0x83,0x00,0x00,  //    cmpwi  cr7,r3,0
    0x41,0x9e,0x00,0x84,  //    beq    cr7,L7
    0x81,0x9e,0x00,0x10,  //    lwz    r12,16(r30)
    0x38,0x9e,0x00,0x28,  //    addi   r4,r30,40
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x48,0x00,0x00,0x3c,  //    b L13
//L2:
    0x38,0x80,0x00,0x00,  //    li     r4,0
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x2f,0x83,0x00,0x00,  //    cmpwi  cr7,r3,0
    0x41,0x9e,0x00,0x5c,  //    beq    cr7,L7
    0x81,0x9e,0x00,0x14,  //    lwz    r12,20(r30)
    0x38,0x7e,0x00,0x28,  //    addi   r3,r30,40
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x2f,0x83,0x00,0x00,  //    cmpwi  cr7,r3,0
    0x41,0x9e,0x00,0x44,  //    beq    cr7,L7
    0x81,0x9e,0x00,0x18,  //    lwz    r12,24(r30)
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
//L13:
    0x2f,0x83,0x00,0x00,  //    cmpwi  cr7,r3,0
    0x7c,0x6c,0x1b,0x78,  //    mr     r12,r3
    0x41,0x9e,0x00,0x2c,  //    beq    cr7,L7
    0x88,0x1e,0x04,0x48,  //    lbz    r0,1096(r30)
    0x7c,0x00,0x07,0x74,  //    extsb  r0,r0
    0x2f,0x80,0x00,0x00,  //    cmpwi  cr7,r0,0
    0x41,0x9e,0x00,0x14,  //    beq    cr7,L8
    0x38,0x7e,0x04,0x48,  //    addi   r3,r30,1096
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x48,0x00,0x00,0x0c,  //    b L7
//L8:
    0x7c,0x69,0x03,0xa6,  //    mtctr r3
    0x4e,0x80,0x04,0x21,  //    bctrl
//L7:
    0x80,0x01,0x00,0x58,  //    lwz    r0,88(r1)
    0x38,0x21,0x00,0x50,  //    addi   r1,r1,80
    0x38,0x60,0x00,0x00,  //    li     r3,0
    0xbb,0xc1,0xff,0xf8,  //    lmw    r30,-8(r1)
    0x7c,0x08,0x03,0xa6,  //    mtlr r0
    0x4e,0x80,0x00,0x20,  //    blr

//_NewThreadStartFunction:
    0x7c,0x08,0x02,0xa6,  //    mflr r0
    0xbf,0x41,0xff,0xe8,  //    stmw   r26,-24(r1)
    0x3b,0x84,0x08,0x48,  //    addi   r28,r4,2120
    0x7c,0x7a,0x1b,0x78,  //    mr     r26,r3
    0x7c,0x9d,0x23,0x78,  //    mr     r29,r4
    0x3b,0x64,0x0a,0xa4,  //    addi   r27,r4,2724
    0x90,0x01,0x00,0x08,  //    stw    r0,8(r1)
    0x94,0x21,0xff,0x90,  //    stwu   r1,-112(r1)
    0x7f,0x83,0xe3,0x78,  //    mr     r3,r28
    0x81,0x84,0x00,0x00,  //    lwz    r12,0(r4)
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x81,0x9d,0x00,0x20,  //    lwz    r12,32(r29)
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x81,0x9d,0x00,0x04,  //    lwz    r12,4(r29)
    0x7f,0x64,0xdb,0x78,  //    mr     r4,r27
    0x80,0xbd,0x00,0x24,  //    lwz    r5,36(r29)
    0x7c,0x66,0x1b,0x78,  //    mr     r6,r3
    0x7f,0x83,0xe3,0x78,  //    mr     r3,r28
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x81,0x9d,0x00,0x08,  //    lwz    r12,8(r29)
    //0x7f,0x64,0xdb,0x78,  //    mr     r4,r27
    0x38,0x80,0x00,0x00,  //    li     r4,0
    0x7f,0x45,0xd3,0x78,  //    mr     r5,r26
    0x7f,0xa6,0xeb,0x78,  //    mr     r6,r29
    0x38,0x61,0x00,0x40,  //    addi   r3,r1,64
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x81,0x9d,0x00,0x20,  //    lwz    r12,32(r29)
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x81,0x9d,0x00,0x1c,  //    lwz    r12,28(r29)
    0x7d,0x89,0x03,0xa6,  //    mtctr r12
    0x4e,0x80,0x04,0x21,  //    bctrl
    0x80,0x01,0x00,0x78,  //    lwz    r0,120(r1)
    0x38,0x21,0x00,0x70,  //    addi   r1,r1,112
    0xbb,0x41,0xff,0xe8,  //    lmw    r26,-24(r1)
    0x7c,0x08,0x03,0xa6,  //    mtlr r0
    0x4e,0x80,0x00,0x20,  //    blr
};
const unsigned int _ppc_newthr_offset = 212;

const unsigned char _ia32_insertion_code[ ] = {

// _PthreadStartFunction:
    0x55,                                     //     pushl	%ebp
    0x89,0xe5,                                //     movl	%esp, %ebp
    0x56,                                     //     pushl	%esi
    0x83,0xec,0x14,                           //     subl	$20, %esp
    0x8b,0x75,0x08,                           //     movl	8(%ebp), %esi
    0x8b,0x46,0x10,                           //     movl	16(%esi), %eax
    0x85,0xc0,                                //     testl	%eax, %eax
    0x74,0x26,                                //     je	L2
    0xc7,0x44,0x24,0x04,0x0a,0x00,0x00,0x00,  //     movl	$10, 4(%esp)
    0x8d,0x46,0x48,                           //     leal	72(%esi), %eax
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x56,0x0c,                           //     call	*12(%esi)
    0x89,0xc2,                                //     movl	%eax, %edx
    0x85,0xc0,                                //     testl	%eax, %eax
    0x74,0x55,                                //     je	L4
    0x8d,0x46,0x28,                           //     leal	40(%esi), %eax
    0x89,0x44,0x24,0x04,                      //     movl	%eax, 4(%esp)
    0x89,0x14,0x24,                           //     movl	%edx, (%esp)
    0xff,0x56,0x10,                           //     call	*16(%esi)
    0xeb,0x28,                                //     jmp	L13
// L2:
    0xc7,0x44,0x24,0x04,0x00,0x00,0x00,0x00,  //     movl	$0, 4(%esp)
    0x8d,0x46,0x48,                           //     leal	72(%esi), %eax        
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x56,0x0c,                           //     call	*12(%esi)
    0x85,0xc0,                                //     testl	%eax, %eax
    0x74,0x31,                                //     je	L4
    0x8d,0x46,0x28,                           //     leal	40(%esi), %eax
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x56,0x14,                           //     call	*20(%esi)
    0x85,0xc0,                                //     testl	%eax, %eax
    0x74,0x24,                                //     je	L4
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x56,0x18,                           //     call	*24(%esi)
// L13:
    0x89,0xc2,                                //     movl	%eax, %edx
    0x85,0xc0,                                //     testl	%eax, %eax
    0x74,0x18,                                //     je	L4
    0x80,0xbe,0x48,0x04,0x00,0x00,0x00,       //     cmpb	$0, 1096(%esi)
    0x74,0x0d,                                //     je	L10
    0x8d,0x86,0x48,0x04,0x00,0x00,            //     leal	1096(%esi), %eax
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0xd2,                                //     call	*%edx
    0xeb,0x02,                                //     jmp	L4
// L10:
    0xff,0xd0,                                //     call	*%eax
// L4:
    0x31,0xc0,                                //     xorl	%eax, %eax
    0x83,0xc4,0x14,                           //     addl	$20, %esp
    0x5e,                                     //     popl	%esi
    0x5d,                                     //     popl	%ebp
    0xc3,                                     //     ret

// _NewThreadStartFunction:
    0x55,                                     //     pushl	%ebp
    0x89,0xe5,                                //     movl	%esp, %ebp
    0x57,                                     //     pushl	%edi
    0x56,                                     //     pushl	%esi
    0x83,0xec,0x30,                           //     subl	$48, %esp
    0x8b,0x75,0x0c,                           //     movl	12(%ebp), %esi
    0x8d,0x86,0x48,0x08,0x00,0x00,            //     leal	2120(%esi), %eax
    0x89,0x45,0xe4,                           //     movl	%eax, -28(%ebp)
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x16,                                //     call	*(%esi)
    0x8b,0x46,0x04,                           //     movl	4(%esi), %eax
    0x89,0x45,0xe0,                           //     movl	%eax, -32(%ebp)
    0xff,0x56,0x20,                           //     call	*32(%esi)
    0x8d,0xbe,0xa4,0x0a,0x00,0x00,            //     leal	2724(%esi), %edi
    0x89,0x44,0x24,0x0c,                      //     movl	%eax, 12(%esp)
    0x8b,0x46,0x24,                           //     movl	36(%esi), %eax
    0x89,0x44,0x24,0x08,                      //     movl	%eax, 8(%esp)
    0x89,0x7c,0x24,0x04,                      //     movl	%edi, 4(%esp)
    0x8b,0x45,0xe4,                           //     movl	-28(%ebp), %eax
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x55,0xe0,                           //     call	*-32(%ebp)
    0x89,0x74,0x24,0x0c,                      //     movl	%esi, 12(%esp)
    0x8b,0x45,0x08,                           //     movl	8(%ebp), %eax
    0x89,0x44,0x24,0x08,                      //     movl	%eax, 8(%esp)
    //0x89,0x7c,0x24,0x04,                      //     movl	%edi, 4(%esp)
    0xc7,0x44,0x24,0x04,0x00,0x00,0x00,0x00,  //     movl	$0, 4(%esp)
    0x8d,0x45,0xf4,                           //     leal	-12(%ebp), %eax
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0x56,0x08,                           //     call	*8(%esi)
    0x8b,0x7e,0x1c,                           //     movl	28(%esi), %edi
    0xff,0x56,0x20,                           //     call	*32(%esi)
    0x89,0x04,0x24,                           //     movl	%eax, (%esp)
    0xff,0xd7,                                //     call	*%edi
    0x83,0xc4,0x30,                           //     addl	$48, %esp
    0x5e,                                     //     popl	%esi
    0x5f,                                     //     popl	%edi
    0x5d,                                     //     popl	%ebp
    0xc3                                      //     ret

};
const unsigned int _ia32_newthr_offset = 133;

// this is the Intel processor code that does an atomic copy to
// overwrite the initial instructions of the target functions.
const unsigned char _rosetta_code[ ]   = {
    0x00,                                     //     .byte 0
// _RosettaPatchInstaller:
    0x55,                                     //     pushl	%ebp
    0x89,0xe5,                                //     movl	%esp, %ebp
    0x56,                                     //     pushl	%esi
    0x8b,0x4d,0x08,                           //     movl	8(%ebp), %ecx
    0x31,0xf6,                                //     xorl	%esi, %esi
    0xeb,0x11,                                //     jmp	L18
// L19:
    0x8b,0x41,0xf8,                           //     movl	-8(%ecx), %eax
    0x8b,0x51,0xfc,                           //     movl	-4(%ecx), %edx
    0x89,0x10,                                //     movl	%edx, (%eax)
    0x8b,0x41,0xf8,                           //     movl	-8(%ecx), %eax
    0x0f,0xae,0x38,                           //     clflush	(%eax)
    0x83,0xc6,0x01,                           //     addl	$1, %esi
// L18:
    0x83,0xc1,0x08,                           //     addl	$8, %ecx
    0x3b,0x75,0x0c,                           //     cmpl	12(%ebp), %esi
    0x72,0xe7,                                //     jb	L19
    0x8b,0x45,0x10,                           //     movl	16(%ebp), %eax
    0xc6,0x00,0xff,                           //     movb	$-1, (%eax)
// L21:
    0xeb,0xfe                                 //     jmp	L21
    
};
const unsigned int _rosetta_code_offset = 1;
