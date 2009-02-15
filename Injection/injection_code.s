;
;  injection_code.s
;  DynamicPatch
;
;  Created by jim on 30/3/2006.
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

; This file was generated from newthread.c. Do not build this file directly.
; See newthread.c for more information.

#if defined(__ppc__)

	.globl _PthreadStartFunction
.section __TEXT,__text,regular,pure_instructions
	.align 2
_PthreadStartFunction:
	mflr r0
	stmw r30,-8(r1)
	mr r30,r3
	stw r0,8(r1)
	stwu r1,-80(r1)
	lwz r0,16(r3)
	lwz r12,12(r30)
	addi r3,r3,72
	cmpwi cr7,r0,0
	beq cr7,L2
	li r4,10
	mtctr r12
	bctrl
	cmpwi cr7,r3,0
	beq cr7,L7
	lwz r12,16(r30)
	addi r4,r30,40
	mtctr r12
	bctrl
	b L13
L2:
	li r4,0
	mtctr r12
	bctrl
	cmpwi cr7,r3,0
	beq cr7,L7
	lwz r12,20(r30)
	addi r3,r30,40
	mtctr r12
	bctrl
	cmpwi cr7,r3,0
	beq cr7,L7
	lwz r12,24(r30)
	mtctr r12
	bctrl
L13:
	cmpwi cr7,r3,0
	mr r12,r3
	beq cr7,L7
	lbz r0,1096(r30)
	extsb r0,r0
	cmpwi cr7,r0,0
	beq cr7,L8
	addi r3,r30,1096
	mtctr r12
	bctrl
	b L7
L8:
	mtctr r3
	bctrl
L7:
	lwz r0,88(r1)
	addi r1,r1,80
	li r3,0
	lmw r30,-8(r1)
	mtlr r0
	blr
	.align 2
	.globl _NewThreadStartFunction
.section __TEXT,__text,regular,pure_instructions
	.align 2
_NewThreadStartFunction:
	mflr r0
	stmw r26,-24(r1)
	addi r28,r4,2120
	mr r26,r3
	mr r29,r4
	addi r27,r4,2724
	stw r0,8(r1)
	stwu r1,-112(r1)
	mr r3,r28
	lwz r12,0(r4)
	mtctr r12
	bctrl
	lwz r12,32(r29)
	mtctr r12
	bctrl
	lwz r12,4(r29)
	mr r4,r27
	lwz r5,36(r29)
	mr r6,r3
	mr r3,r28
	mtctr r12
	bctrl
	lwz r12,8(r29)
	mr r4,r27
	mr r5,r26
	mr r6,r29
	addi r3,r1,64
	mtctr r12
	bctrl
	lwz r12,32(r29)
	mtctr r12
	bctrl
	lwz r12,28(r29)
	mtctr r12
	bctrl
	lwz r0,120(r1)
	addi r1,r1,112
	lmw r26,-24(r1)
	mtlr r0
	blr

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; This bit is currently not used
	.align 2
	.globl _RosettaPatchInstaller
.section __TEXT,__text,regular,pure_instructions
	.align 2
_RosettaPatchInstaller:
	li r11,0
	cmplw cr7,r11,r4
	bge cr7,L25
	mtctr r4
L26:
	slwi r0,r11,3
	add r2,r0,r3
	lwzx r9,r3,r0
	lwz r0,4(r2)
	stw r0,0(r9)
	isync
	addi r11,r11,1
	bdnz L26
L25:
	li r0,-1
	stb r0,0(r5)
L21:
	b L21
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

#elif defined(__i386__)

	.text
.globl _PthreadStartFunction
_PthreadStartFunction:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	subl	$20, %esp
	movl	8(%ebp), %esi
	movl	16(%esi), %eax
	testl	%eax, %eax
	je	L2
	movl	$10, 4(%esp)
	leal	72(%esi), %eax
	movl	%eax, (%esp)
	call	*12(%esi)
	movl	%eax, %edx
	testl	%eax, %eax
	je	L4
	leal	40(%esi), %eax
	movl	%eax, 4(%esp)
	movl	%edx, (%esp)
	call	*16(%esi)
	jmp	L13
L2:
	movl	$0, 4(%esp)
	leal	72(%esi), %eax
	movl	%eax, (%esp)
	call	*12(%esi)
	testl	%eax, %eax
	je	L4
	leal	40(%esi), %eax
	movl	%eax, (%esp)
	call	*20(%esi)
	testl	%eax, %eax
	je	L4
	movl	%eax, (%esp)
	call	*24(%esi)
L13:
	movl	%eax, %edx
	testl	%eax, %eax
	je	L4
	cmpb	$0, 1096(%esi)
	je	L10
	leal	1096(%esi), %eax
	movl	%eax, (%esp)
	call	*%edx
	jmp	L4
L10:
	call	*%eax
L4:
	xorl	%eax, %eax
	addl	$20, %esp
	popl	%esi
	popl	%ebp
	ret
.globl _NewThreadStartFunction
_NewThreadStartFunction:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%edi
	pushl	%esi
	subl	$48, %esp
	movl	12(%ebp), %esi
	leal	2120(%esi), %eax
	movl	%eax, -28(%ebp)
	movl	%eax, (%esp)
	call	*(%esi)
	movl	4(%esi), %eax
	movl	%eax, -32(%ebp)
	call	*32(%esi)
	leal	2724(%esi), %edi
	movl	%eax, 12(%esp)
	movl	36(%esi), %eax
	movl	%eax, 8(%esp)
	movl	%edi, 4(%esp)
	movl	-28(%ebp), %eax
	movl	%eax, (%esp)
	call	*-32(%ebp)
	movl	%esi, 12(%esp)
	movl	8(%ebp), %eax
	movl	%eax, 8(%esp)
	movl	%edi, 4(%esp)
	leal	-12(%ebp), %eax
	movl	%eax, (%esp)
	call	*8(%esi)
	movl	28(%esi), %edi
	call	*32(%esi)
	movl	%eax, (%esp)
	call	*%edi
	addl	$48, %esp
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
.globl _RosettaPatchInstaller
_RosettaPatchInstaller:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	movl	8(%ebp), %ecx
	xorl	%esi, %esi
	jmp	L18
L19:
	movl	-8(%ecx), %eax
	movl	-4(%ecx), %edx
	movl	%edx, (%eax)
	movl	-8(%ecx), %eax
	clflush	(%eax)
	addl	$1, %esi
L18:
	addl	$8, %ecx
	cmpl	12(%ebp), %esi
	jb	L19
	movl	16(%ebp), %eax
	movb	$-1, (%eax)
L21:
	jmp	L21

#endif
