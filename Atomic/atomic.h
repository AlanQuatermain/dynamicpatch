/*
 *  atomic.h
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

#ifndef __DP_ATOMIC_H__
#define __DP_ATOMIC_H__

#include <sys/cdefs.h>

__BEGIN_DECLS

/*!
 @header Atomic Utilities
 @discussion Atomic utility routines used by the DynamicPatch library.
        These routines will perform an atomic (non-interruptable) change
        to volatile values in memory. They will also ensure that the 
        processor's instruction and data caches do not contain copies of
        the modified data from before it was modified. 

        The routines themselves are implemented in PowerPC and IA-32
        assembly.
 @copyright 2004-2006 Jim Dovey. Some Rights Reserved.
 @author Jim Dovey
 */

/*!
 @function DPCompareAndSwap
 @abstract Check the value at an address, then update it.
 @discussion This routine will atomically change the 32-bit word contained
        at the specified memory address, after first comparing it with a
        given value. If the current value doesn't match what's expected,
        it will return failure. Otherwise, it will atomically set the
        value at the address to the given new value, and will instruct
        the processor to refetch its instruction cache.

        This routine is used to write the branch absolute instruction at
        the beginning of a patched function.
 @param oldVal Value expected to be at supplied address prior to write.
 @param newVal Value to write into memory at supplied address.
 @param address The address at which to check/update value.
 @result Returns 1 on success, 0 if <code>oldVal</code> does not match
         the value at <code>address</code>.
 */
int DPCompareAndSwap( unsigned int oldVal, unsigned int newVal, unsigned int * address );

#if __i386__
// Intel processors get a 64-bit version of the above
int DPCompareAndSwap64( unsigned long long oldVal, unsigned long long newVal,
                        unsigned long long * address );
#endif

/*!
 @function DPCodeSync
 @abstract Syncs the processor data & instruction caches with memory.
 @discussion This function will cause the data & instruction processor
        caches to re-sync with main memory. This is called after writing
        new executable code to memory.

        On Intel processors, full syncs can only be performed by in
        ring zero (I think), so this function takes a parameter to use
        with the i386 CLFLUSH instruction, which flushes/syncs the cache
        line containing a specific address. On PowerPC, however, there
        are simply instructions to sync each cache (data and instruction)
        as a whole.
 @param address Address of data whose cache lines to flush 
        (Intel processors only).
 */
void DPCodeSync( void * address );

__END_DECLS

#endif  /* __DP_ATOMIC_H__ */
