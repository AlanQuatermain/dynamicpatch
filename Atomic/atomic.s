/*
 *  atomic.s
 *  DynamicPatch
 *
 *  Created by jim on 24/3/2006.
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

#ifdef __ppc__

.macro ENTRY
   .text
   .align   2
   .globl   $0
$0:
.endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; int DPCompareAndSwap(unsigned int oldVal, unsigned int newVal, unsigned int * address)
    ENTRY _DPCompareAndSwap
.cas_retry:
    lwarx   r6, 0, r5           ; load 32-bit word at 'address' (r5) into r6
    cmpw    r6, r3              ; compare contents of r6 with 'oldVal' (r3)
    bne-    .cas_fail           ; end if not equal
    stwcx.  r4, 0, r5           ; try to store 'newVal' (r4) into 32-bit word at 'address' (r5)
    bne-    .cas_retry          ; if store failed, retry
    isync                       ; make sure instruction cache reflects any changes
    li      r3, 1               ; return 1 on success
    blr                         ; return to caller
.cas_fail:
    li      r3, 0               ; return 0 on failure
    blr                         ; return to caller

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; void DPCodeSync( void * address )     -- address is unused on PowerPC --
    ENTRY _DPCodeSync
    isync                       ; sync instruction cache
    sync                        ; sync data cache
    blr                         ; return to caller

#elif defined(__i386__)

#;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
#; int DPCompareAndSwap(unsigned int oldVal, unsigned int newVal, unsigned int *address)
    .globl _DPCompareAndSwap
_DPCompareAndSwap:
    mov         4(%esp),%eax    #; eax <- oldVal
    mov         8(%esp),%edx    #; edx <- newVal
    mov        12(%esp),%ecx    #; ecx <- address
    lock                        #; atomic operation
    cmpxchgl    %edx,(%ecx)     #; eax is an implicit operand
    sete        %al             #; see if it succeeded
    movzbl      %al,%eax        #; clear out high bytes of result
    ret                         #; return to caller

#;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
#; int DPCompareAndSwap64(unsigned long long oldVal, unsigned long long newVal, unsigned long long *address)
    .globl _DPCompareAndSwap64
_DPCompareAndSwap64:
    push        %esi            #; store old esi value, since we'll use esi
    mov         8(%esp),%eax    #; eax <- (lo-dword of oldVal)
    mov        12(%esp),%edx    #; edx <- (hi-dword of oldVal)
    mov        16(%esp),%ebx    #; ebx <- (lo-dword of newVal)
    mov        20(%esp),%ecx    #; ecx <- (hi-dword of newVal)
    mov        24(%esp),%esi    #; esi <- address
    lock                        #; atomic operation
    cmpxchg8b   (%esi)          #; edx:eax and ebx:ecx are implicit operands
    sete        %al             #; see if it succeeded
    movzbl      %al,%eax        #; clear out high bytes of result
    pop         %esi            #; restore old esi value
    ret

#;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
#; void DPCodeSync( void * address )
    .globl _DPCodeSync
_DPCodeSync:
    mov         4(%esp),%edx
    mfence
    clflush     (%edx)
    ret

#endif