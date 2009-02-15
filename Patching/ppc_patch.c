/*
 *  ppc_patch.c
 *  DynamicPatch
 *
 *  Created by jim on 25/3/2006.
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

// This file contains the PowerPC patching implementation, more or less
// exactly as written in April 2003. It's still in its own file since I
// figured I'd rather work on the Intel code by modifying a copy of
// this, rather than playing about with what was also a working
// implementation for PPC. The only changes added to this recently were
// the tweaks for compatibility within the Rosetta environment, which
// are only going to trigger if an application running translated
// patches itself; in this case, it needs to handle the more limited
// address space available to the Intel processor and kernel.

// It is worth noting that this framework was written for internal use,
// so there isn't a great amount of paranoia. After working on the
// initial patching routines and never needing the error-handling they
// included, I resolved that I needn't concern myself unduly with
// things like checking what instructions were being overwritten. My
// reasoning was that I was the only client of the API, and none of the
// eight or so functions I patched had anything other than 'mflr r0' as
// their first instructions.

/*

 The assembler code generated when patching includes three logical
 parts, split into two blocks. One goes into high memory, one into low
 memory. The high memory block is placed there because it needs to be
 addressable using a 26-bit address sign-extended to 32-bits, and it's
 more likely that addresses above 0xFE000000 are free than below
 0x01FFFFFF.

 The branching itself is done using registers 11 and 12, the same ones
 used in Objective-C message passing and C++ virtual function calls. I
 settled on this partly because it seemed the standard behaviour,
 partly because any patched virtual functions would still see their own
 address in r12 after going through the patch blocks, and while such
 code seems odd, it's better to be safe than sorry. Continuing in this
 vein, the re-entry island modifies the contents of r12 after it has
 been copied to the count register, such that r12 contains the *real*
 address of the patched function, to maintain the illusion. This is
 just in case something somewhere expects to use r12 as the basis for a
 branch or fetch relative to the function address for some reason.

 The branch island is a little different than that used in
 mach_override. First of all, the branch target is stored in memory at
 the beginning of the block, rather than compiled into the
 instructions. Secondly, it is stored alongside an error-handler branch
 target. The idea is that the code gets the branch island address
 compiled in, and it reads from there its branch target. If that
 happens to be zero, it doesn't go there, and instead reads the second
 data value (the error-handler address) and branches to that instead.
 This enables us to zero the patch address in the island and have that
 island automatically jump to the re-entry island instead.
 Alternatively, the error-handler could be set to a custom error
 routine, which could do anything from printing a message to unrolling
 the stack, manually uninstalling the patch, and re-instating the
 original function call.

 In this one, the error-handler only provides a fallback branch to the
 original code. in the re-entry island, it's left blank to force a
 protection fault if the re-entry island gets corrupted.

*/

#if __ppc__

#include "atomic.h"

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>

#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_error.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <mach/vm_prot.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>
#include <mach/machine/vm_param.h>
#include <mach/machine/vm_types.h>

#include <mach-o/loader.h>

// addresses of the allocated jump tables
static vm_address_t     low_jump_table  = 0;
static vm_address_t     high_jump_table = 0;

// allocated size of the tables
// originally I'd planned to have these resizable, but that never got
// implemented
static vm_size_t        low_table_size  = 0;
static vm_size_t        high_table_size = 0;

// offsets to the next available slot in the tables
static vm_offset_t      low_table_offset    = 0;
static vm_offset_t      high_table_offset   = 0;

// a mutex wraps all patching attempts
static int              mutex_inited    = 0;
static pthread_mutex_t  patch_mutex;

// these constants are used to infer translated or native execution
enum
{
    OAH_UNKNOWN     = -1,
    OAH_NATIVE      =  0,
    OAH_TRANSLATED  =  1
};
static int rosetta = OAH_UNKNOWN;

// the template for branch islands. This uses r11 & r12 to hold the
// branch target address, just like ObjC messaging and C++ vtables. It
// also reads the target address from memory, so that address can
// potentially be rewritten later. The only compiled-in address is the
// address of this block (address of the 32-bit data at its start).
// That block contains the main branch target followed by an optional
// error-handler branch target. In the case of branch-to-patch, this
// can be the address of the branch-to-original block, so that the
// patch can be easily removed by zeroing the patch target address
// here. In the branch-to-original block, this shouldn't be needed, but
// could point to an error handler routine of some kind.
static unsigned int branch_template[] = {
    0x00000000,     // .long    branch_target_addr
    0x00000000,     // .long    error_branch_target
    0x3D600000,     // lis      r11,(msw of this_entry_addr)
    0x616B0000,     // ori      r11,r11,(lsw of this_entry_addr)
    0x818B0000,     // lwz      r12,0(r11)  -- loads branch_target_addr into r12
    0x2C0C0000,     // cmpwi    r12,0
    0x7D8903A6,     // mtctr    r12
    0x60000000,     // nop                  -- can modify r12 here (optional)
    0x60000000,     // nop                  -- store first instruction here (optional)
    0x4C820420,     // bnectr
    0x818B0004,     // lwz      r12,4(r11)  -- loads error_branch_target into r12
    0x7D8903A6,     // mtctr    r12
    0x4E800420      // bctr                 -- call error handler
};

// this function is called at program termination via atexit()
static void free_jump_tables( void )
{
    if ( low_jump_table != 0 )
        vm_deallocate( mach_task_self( ), low_jump_table, low_table_size );
    if ( high_jump_table != 0 )
        vm_deallocate( mach_task_self( ), high_jump_table, high_table_size );
    if ( mutex_inited )
        pthread_mutex_destroy( &patch_mutex );
}

// this will setup the mutex variables
static void initialize_patch_mutexes( void )
{
    if ( mutex_inited == 0 )
    {
        mutex_inited = 1;

        {
            pthread_mutexattr_t attr;
            pthread_mutexattr_init( &attr );
            pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );

            pthread_mutex_init( &patch_mutex, &attr );

            pthread_mutexattr_destroy( &attr );
        }
    }
}

#pragma mark -

// this builds an entry in the low-memory jump table
static size_t build_low_entry( vm_address_t this_entry_addr,
                               vm_address_t reentry_addr,
                               unsigned int saved_instruction )
{
    unsigned int * data_ptr = (unsigned int *) this_entry_addr;

    // we write directly to the table; if things go wrong, we simply
    // leave this there as unused garbage, to be overwritten by the
    // next patch call
    memcpy( data_ptr, branch_template, sizeof(branch_template) );

    // the branch target is the second instruction of the patched
    // function
    data_ptr[0] = (unsigned int) reentry_addr;

    // this block leaves the error-handler blank

    // put msw & lsw of this_entry_addr into load instructions
    data_ptr[2] |= ((this_entry_addr & 0xFFFF0000) >> 16);
    data_ptr[3] |=  (this_entry_addr & 0x0000FFFF);

    // returning to the original function, make r12 be the real address
    data_ptr[7]  = 0x398CFFFC;      // addi r12,r12,-4
    // store the first instruction of the patched function here
    data_ptr[8]  = saved_instruction;

    return ( sizeof(branch_template) );
}

// this builds the high jump table entry
static size_t build_high_entry( vm_address_t this_entry_addr,
                                vm_address_t low_entry_addr,
                                vm_address_t patch_fn_addr )
{
    unsigned int * data_ptr = (unsigned int *) this_entry_addr;

    memcpy( data_ptr, branch_template, sizeof(branch_template) );

    data_ptr[0]  = patch_fn_addr;       // patch function address
    data_ptr[1]  = low_entry_addr;      // re-entry address
    data_ptr[2] |= ((this_entry_addr & 0xFFFF0000) >> 16);
    data_ptr[3] |=  (this_entry_addr & 0x0000FFFF);

    return ( sizeof(branch_template) );
}

// a basic Rosetta-detection routine, written before anything else was
// available
static int is_rosetta_process( void )
{
    int result = OAH_NATIVE;

    // The 'translate' binary loads at this address. On 10.4.1, this
    // basically meant it loaded into the top n pages of memory. The
    // max page address has now gone upwards somewhat, but the binary
    // still loads at the same address.
    u_int32_t *pRosettaHdr = (u_int32_t *) 0xb8000000;

    // don't segfalut if it's not there -- check first using
    // vm_region() whether that page is in use
    vm_address_t region_addr = 0xb8000000;
    vm_size_t region_size = 0;
    vm_region_flavor_t region_flavor = VM_REGION_BASIC_INFO;
    struct vm_region_basic_info region_info;
    mach_msg_type_number_t info_count = sizeof( struct vm_region_basic_info );
    memory_object_name_t region_object_name;

    kern_return_t kr = vm_region( mach_task_self( ), &region_addr, &region_size, region_flavor,
                                  ( vm_region_info_t ) &region_info,
                                  &info_count, &region_object_name );

    // vm_region will start at the given address & keep looking forwards until it finds an
    //  allocated region, so
    if ( ( kr == KERN_SUCCESS ) && ( region_addr == 0xb8000000 ) )
    {
        // make sure it's readable, at least
        if ( region_info.protection & VM_PROT_READ )
        {
            // we can read it - check what's there
            if ( ( *pRosettaHdr == MH_CIGAM ) ||
                 ( *pRosettaHdr == MH_MAGIC ) ) // you never know...
            {
                result = OAH_TRANSLATED;
                LogMessage( "Patching inside a Rosetta process" );
            }
        }
    }

    return ( result );
}

static kern_return_t allocate_jump_tables( void )
{
    // use vm_allocate to get a couple of pages in or around a couple
    // certain addresses
    vm_size_t page_size = 0;
    kern_return_t kr = KERN_SUCCESS;
    task_t me = 0;
    vm_address_t page_addr = 0;

    kr = host_page_size( mach_host_self( ), &page_size );

    if ( kr != KERN_SUCCESS )
    {
        // make an educated guess at 4096 bytes
        page_size = 4096;
    }

    // cache this one
    me = mach_task_self( );

    // allocate one page for each jump table
    // low memory one first...
    kr = vm_allocate( me, &page_addr, page_size, TRUE );

    if ( kr == KERN_SUCCESS )
    {
        int done = 0, err = 0, allocated = 0;
        low_jump_table = page_addr;
        low_table_size = page_size;
        low_table_offset = 0;

        page_addr = 0xffff0000;	// our ideal start address - we want memory above here

        // detect whether we're running in the Rosetta environment; if we are, we can't use
        //  memory above address 0xC0000000, so we'll have to change
        //  our rules
        if ( rosetta == OAH_UNKNOWN )
            rosetta = is_rosetta_process( );

        if ( rosetta == OAH_TRANSLATED )
        {
            // From the original DTK release:

            // running in rosetta - branch absolutes will need to have destination where
            //  addresses are in the range [0x00000000 .. 0x01FFFFFC]
            //  This is because the target must be sign-extended from a 26-bit address,
            //  and therefore since we can't put anything in an address above 0xC0000000,
            //  the topmost of our 26 bits must be zero -- meaning that the topmost seven
            //  bits of the 32-bit destination address must also be zero.
            // So, our allowable page-address range, instead of being [0xFFFF0000..0xFFFFF000],
            //  is now [0x00000000 .. 0x01FFF000].
            // The easiest way of handling this is to just do a low-memory vm_allocate, giving
            //  a base address of zero, and see what it returns...

            //page_addr = 0;
            //done = 1;

            // These days, Intel has more memory available (up to
            // 0xFFC00000 in fact), so we can try to allocate high.
            // Therefore, we set a new start address and go through the
            // lookup routine as usual (previously we'd skip it & try
            // to allocate low, as described above).
            page_addr = 0xFFDF0000;
        }

        // note that in <mach/ppc/vm_param.h>, the highest vm_address is 0xfffff000...
        // loop to find a free page here...
        do
        {
            vm_address_t region_addr = page_addr;
            vm_size_t region_size = 0;
            vm_region_flavor_t region_flavor = VM_REGION_BASIC_INFO;
            struct vm_region_basic_info region_info;
            mach_msg_type_number_t info_count = sizeof( struct vm_region_basic_info );
            memory_object_name_t region_object_name;

            kr = vm_region( me, &region_addr, &region_size, region_flavor,
                            ( vm_region_info_t ) &region_info,
                            &info_count, &region_object_name );

            if ( kr == KERN_INVALID_ADDRESS )
            {
                // there's nothing allocated up there
                // so just use page_addr as it stands
                done = 1;
            }
            else if ( kr == KERN_SUCCESS )
            {
                // found a region... first of all, where is it?
                // vm_region() will search, starting at the specified address
                //  so it might have returned the topmost page for all we know
                if ( ( region_addr - page_addr ) >= page_size )
                {
                    // okay, there's a free page or so at page_addr still
                    done = 1;
                }
                else
                {
                    // not enough room underneath to allocate a page here...
                    // move out page_addr forward a few pages, but be careful...
                    vm_size_t max_len = VM_MAX_ADDRESS - region_addr;

                    // max_len is the size necessary to make region at region_addr fill the rest
                    // of the allowed memory
                    if ( region_size > ( max_len - page_size ) )
                    {
                        // okay, region goes up to and includes the last accessible page of memory...
                        // which means we can't allocate any high memory...
                        err = 1;
                    }
                    else
                    {
                        // there's probably room left there for another page... loop again & see
                        page_addr = region_addr + region_size;

                        // make sure we get a page-aligned address
                        page_addr += ( page_size - 1 );
                        page_addr &= ~( page_size - 1 );

                        // continue
                    }
                }
            }
            else
            {
                err = 1;
            }

        } while ( ( done == 0 ) && ( err == 0 ) );

        if ( (!done) && (rosetta == OAH_TRANSLATED) )
        {
            // rosetta case -- provide a suggestion (zero) and see what we get. Should be
            //  as low as possible, with the most significant seven bits *zeroed*.
            kr = vm_allocate( me, &page_addr, page_size, TRUE );

            if ( kr == KERN_SUCCESS )
            {
                if ( (page_addr & 0xFE000000) != 0 )
                {
                    // successfully allocated, but at an address which is no good to us
                    (void) vm_deallocate( me, page_addr, page_size );

                    // set error to something vaguely meaningful to go into failure case below
                    kr = KERN_NO_SPACE;
                }
                else
                {
                    done = 1;
                    allocated = 1;
                }
            }
        }

        if ( !done )
        {
            // deallocate low memory jump table - we won't be needing it...
            kr = vm_deallocate( me, low_jump_table, low_table_size );

            low_jump_table = 0;
            low_table_size = 0;

            LogEmergency( "Unable to allocate high jump table at a branchable address !" );
        }
        else
        {
            // non-rosetta case -- found a free page, so allocate ONLY THERE
            if ( allocated == 0 )
            {
                // we've got a full address for the next page...
                // ...allocate it...
                // ...but don't allow the kernel to relocate this wherever it sees fit...
                kr = vm_allocate( me, &page_addr, page_size, FALSE );
            }

            if ( kr == KERN_SUCCESS )
            {
                high_jump_table = page_addr;
                high_table_size = page_size;
                high_table_offset = 0;

                // allocated both jump tables - set an exit routine to deallocate them
                atexit( free_jump_tables );
            }
            else
            {
                // deallocate the low memory jump table
                kr = vm_deallocate( me, low_jump_table, low_table_size );

                low_jump_table = 0;
                low_table_size = 0;

                LogEmergency( "Failed to allocate high jump table ! %d (%s)",
                              kr, mach_error_string(kr) );
            }
        }
    }

    return ( kr );
}

// this one isn't static, because the cocoa method swizzler expects to
// be able to find/call it from another file
// as such, it gets a funny prefix on its name (ok, so I'm paranoid)
int __make_writable( void * addr )
{
    kern_return_t kr = KERN_SUCCESS;
    vm_address_t region_start = ( vm_address_t ) addr;
    vm_size_t region_size = 0;
    vm_region_flavor_t region_flavor = VM_REGION_BASIC_INFO;
    struct vm_region_basic_info region_info;
    mach_msg_type_number_t region_info_count = sizeof( struct vm_region_basic_info );
    memory_object_name_t region_object_name = 0;
    int good_to_go = 1;

    bzero( &region_info, sizeof( struct vm_region_basic_info ) );

    // make sure we can write to this region of virtual memory...
    kr = vm_region( mach_task_self( ), &region_start, &region_size, region_flavor,
                    ( vm_region_info_t ) &region_info, &region_info_count, &region_object_name );

    if ( kr == KERN_SUCCESS )
    {
        if ( ( region_info.max_protection & VM_PROT_WRITE ) == 0 )
        {
            good_to_go = 0;

            // VM_PROT_COPY along with VM_WRITE essentially makes this region a
            //  'copy on write' area of shared memory. We can't just make the region
            //  writeable, because it's shared between multiple processes - it's loaded
            //  once into physical memory, and simply mapped into the virtual memory
            //  of any process using it. Enabling copy-on-write simply means that when
            //  any process tries to write to this region of memory, the whole page is
            //  duplicated in physical memory for the benefit of that process alone.
            kr = vm_protect( mach_task_self( ), region_start, region_size, TRUE,
                             region_info.max_protection | VM_PROT_WRITE | VM_PROT_COPY );

            if ( kr == KERN_SUCCESS )
            {
                good_to_go = 1;
            }
            else
            {
                LogEmergency( "Set max protection on target failed ! %d (%s)",
                              kr, mach_error_string(kr) );
            }
        }

        if ( ( good_to_go ) && ( region_info.protection & VM_PROT_WRITE ) == 0 )
        {
            // no write permission, have to add it...
            good_to_go = 0;
            kr = vm_protect( mach_task_self( ), region_start, region_size, FALSE,
                             region_info.protection | VM_PROT_WRITE );

            if ( kr == KERN_SUCCESS )
            {
                good_to_go = 1;
            }
            else
            {
                LogEmergency( "Set current protection on target failed ! %d (%s)",
                              kr, mach_error_string(kr) );
            }
        }
    }
    else
    {
        LogError( "vm_region on target address failed !? %d (%s)",
                  kr, mach_error_string(kr) );
    }

    return ( good_to_go );
}

// entry point from CreatePatch()
void * __create_patch( void * in_fn_addr, void * in_patch_addr )
{
    void * result = NULL;

    if ( !mutex_inited )
        initialize_patch_mutexes( );

    // don't do ANYTHING unless we know we're not infringing on something else
    pthread_mutex_lock( &patch_mutex );

    // not much point doing anything else if we can't get write access to patch the function...
    if ( !__make_writable( in_fn_addr ) )
    {
        // multiple return paths - EEUURRRRGHHH !!!
        pthread_mutex_unlock( &patch_mutex );
        return ( NULL );
    }

    // allocate memory for jump tables
    if ( high_jump_table == 0 )	// either both will be allocated, or neither
    {
        allocate_jump_tables( );
    }

    if ( ( high_jump_table != 0 ) &&
         ( ( high_table_offset + sizeof(branch_template) ) <= high_table_size ) &&
         ( ( low_table_offset + sizeof(branch_template) ) <= low_table_size ) )
    {
        // Okay, we need:
        //
        // The first instruction from the function we're about to patch.
        // The address of the entry in the low memory jump table
        // The address of the function to patch
        // The address of the patch function
        // The address of the entry in the high memory jump table
        // The address of the bit of the high jump table entry which refers back to the target fn
        //

        unsigned int saved_instruction;
        vm_address_t fn_addr = ( vm_address_t ) in_fn_addr;
        vm_address_t patch_addr = ( vm_address_t ) in_patch_addr;
        vm_address_t low_entry = low_jump_table + low_table_offset;
        vm_address_t high_entry = high_jump_table + high_table_offset;
        vm_offset_t high_size = 0, low_size = 0;

        saved_instruction = *((unsigned int *) in_fn_addr);

        // generate jump table entry in low memory
        low_size = build_low_entry( low_entry, (fn_addr + 4), saved_instruction );

        if ( low_size > 0 )
        {
            // generate high memory jump table entry
            high_size = build_high_entry( high_entry, low_entry, patch_addr );
        }

        if ( (low_size > 0) && (high_size > 0) )
        {
            // get a branch absolute instruction to patch the target function
            // need to point to first instruction in high_table_entry (high_table_entry + 4, then)
            unsigned int ba_instruction = 0x48000002;   // branch instruction, with Absolute Address bit set
            ba_instruction |= ((high_entry + 8) & 0x03FFFFFC);   // address, with high 6 & low 2 bits cleared

            // try to do this as atomically as possible
            //*( ( unsigned long * ) in_fn_addr ) = ba_instruction;
            while ( DPCompareAndSwap( saved_instruction, ba_instruction,
                                      (unsigned int *) in_fn_addr ) == 0 )
            {
                // instruction has been changed underneath us...
                saved_instruction = *((unsigned int *) in_fn_addr);
                // write this to high_table_entry + 60 (offset of saved instruction in high table entry)
                *((unsigned int *) (high_entry + (8*sizeof(unsigned int)))) = saved_instruction;
            }

            // call msync() on each & update table offsets - flushes instruction cache
            vm_msync( mach_task_self( ), low_entry, low_size,
                      VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );
            vm_msync( mach_task_self( ), high_entry, high_size,
                      VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );

            low_table_offset += low_size;
            high_table_offset += high_size;

            // synchronizes instruction and data caches
            // ppc code doesn't use the address, but might as well keep
            // some sort of parity between architectures
            DPCodeSync( in_fn_addr );

            // set result - addr is address of first *instruction* in the new low addr table entry
            result = (void *) (low_entry + 8);
        }
    }

    pthread_mutex_unlock( &patch_mutex );

    return ( result );
}

void DPRemovePatch( void * fn_addr )
{
    pthread_mutex_lock( &patch_mutex );

    // okay, fn_addr is simple enough; the fiddly bit is locating the
    // original first instruction which we need to reinstate. The first
    // instruction of the original function should be a branch
    // absolute, which contains the address of the code in the high
    // memory block. On PowerPC, it's a simple matter of addition to
    // locate the 'branch to original' block within here, and therefore
    // pull out the original first instruction which we need to place.
    unsigned int *pTo = (unsigned int *) fn_addr;
    unsigned int *pFrom = NULL;
    unsigned int instr = *pTo;
    unsigned int restore = 0;

    // check to see if it's a branch absolute to high memory:
    // ba(addr) is 0x4800002 | (addr & 0x03FFFFFC)
    // Therefore, we need (0x4800002 | 0x02000000), masked against
    // top seven bits and bottom two bits
    if ( (instr & 0xFE000003) == 0x4A000002)
    {
        // it's a branch absolute, pull out the address, and
        // sign-extend it (it'll be all 0xFF above anyway)
        // clear bottom three bits, set top six bits
        pFrom = (unsigned int*) ( (instr & ~3) | 0xFC00000 );

        // read address of low table from here
        pFrom = (unsigned int *) pFrom[1];

        // instruction we're grabbing is at offset 8 (32 bytes)
        restore = pFrom[8];

        // atomic swap
        DPCompareAndSwap( instr, restore, pTo );

        DPCodeSync( fn_addr );
    }

    // all done
    pthread_mutex_unlock( &patch_mutex );
}

#endif
