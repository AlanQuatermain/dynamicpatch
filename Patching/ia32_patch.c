/*
 *  ia32_patch.c
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

#if __i386__

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

// The Intel-based patching algorithm is essentially a port of the
// PowerPC one. 

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

extern int __calc_insn_size( const unsigned char * in_fn_addr, void * jmp_target, 
                             unsigned char new_instr[32], size_t *pSize );

static void free_patch_tables( void )
{
    if ( high_jump_table != 0L )
        vm_deallocate( mach_task_self( ), high_jump_table, high_table_size );
    if ( low_jump_table != 0L )
        vm_deallocate( mach_task_self( ), low_jump_table, low_table_size );
    if ( mutex_inited )
        pthread_mutex_destroy( &patch_mutex );
}

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

// this is a standalone chunk
static unsigned char patch_template[] = {
// L_TemplateStart:
    0x00,0x00,0x00,0x00,            // .long branch_target
    0x00,0x00,0x00,0x00,            // .long error_handler
    0xBA,0x00,0x00,0x00,0x00,       // movl L_TemplateStart, %edx
    0x8B,0x02,                      // movl (%edx), %eax        -- loads branch_target
    0x85,0xC0,                      // test %eax, %eax
    0x0F,0x85,0x03,0x00,0x00,0x00,  // jne  L_BranchToTarget
    0x8B,0x42,0x04,                 // movl 4(%edx), %eax       -- loads error_handler
// L_BranchToTarget:
    0xFF,0xE0                       // jmp  *%eax
};

// the next two go together; they go on either side of the saved
// instructions from the target. Note that the last byte of the starter
// template is an 8-bit relative offset to the beginning of the end
// template: its value should be overwritten with the size of the
// copied code. This is also the place from which we read that length
// when removing patches.
static unsigned char reentry_template_start[] = {
// L_TemplateStart:
    0x00,0x00,0x00,0x00,            // .long branch_target
    0x00,0x00,0x00,0x00,            // .long error_handler
    0xBA,0x00,0x00,0x00,0x00,       // movl L_TemplateStart, %edx
    0x8B,0x02,                      // movl (%edx), %eax        -- loads branch_target
    0x85,0xC0,                      // test %eax, %eax
    0x0F,0x85,0x08,0x00,0x00,0x00,  // jne  L_CallOriginal
    0x8B,0x42,0x04,                 // movl 4(%edx), %eax       -- loads error_handler
    0xEB,0x01                       // jmp  L_BranchToTarget -- **** overwrite last byte with real offset
// L_CallOriginal:
};

// ... saved instruction goes in the middle ...

static unsigned char reentry_template_end[]  = {
// L_BranchToTarget:
    0xFF,0xE0                       // jmp  *%eax
};

// some useful offsets into those blocks
#define branch_target_offset     0
#define error_handler_offset     4
#define start_addr_offset        9
#define instr_size_offset       27

#pragma mark -

// this builds a reentry table entry
static size_t build_low_entry( vm_address_t this_entry_addr,
                               vm_address_t reentry_addr,
                               unsigned char * saved_instructions,
                               unsigned int instr_size )
{
    size_t result = sizeof(reentry_template_start);
    unsigned char * data_ptr = (unsigned char *) this_entry_addr;

    // write the code directly into place
    memcpy( data_ptr, reentry_template_start, result );

    // we're going to try & make the compiler use lda & mov rather than
    // call memcpy()

    // store branch target -- second instruction of target fn
    *((vm_address_t *)(data_ptr + branch_target_offset)) = reentry_addr;

    // this block leaves the error-handler blank
    // we could actually copy instr_size here, if we wanted

    // put this entry's address into load instruction
    *((vm_address_t *)(data_ptr + start_addr_offset)) = this_entry_addr;

    // now copy in a one-byte variation of the saved instruction size
    data_ptr[instr_size_offset] = (unsigned char) instr_size;

    // copy in the saved instructions
    memcpy( data_ptr + result, saved_instructions, instr_size );
    result += instr_size;

    // copy in the end of the template
    memcpy( data_ptr + result, reentry_template_end,
            sizeof(reentry_template_end) );
    result += sizeof(reentry_template_end);

    return ( result );
}

// this builds the branch-to-patch island
static size_t build_high_entry( vm_address_t this_entry_addr,
                                vm_address_t low_entry_addr,
                                vm_address_t patch_fn_addr )
{
    unsigned char * data_ptr = (unsigned char *) this_entry_addr;

    memcpy( data_ptr, patch_template, sizeof(patch_template) );

    *((vm_address_t *)(data_ptr + branch_target_offset)) = patch_fn_addr;
    *((vm_address_t *)(data_ptr + error_handler_offset)) = low_entry_addr;
    *((vm_address_t *)(data_ptr + start_addr_offset))    = this_entry_addr;

    return ( sizeof(patch_template) );
}

static void allocate_jump_tables( void )
{
    // use vm_allocate to get a couple of pages in or around a couple certain addresses
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
        low_jump_table = page_addr;
        low_table_size = page_size;
        low_table_offset = 0;

        // on Intel, the 'high' memory jump table doesn't need to be in
        // high memory, since we can give a full 32-bit address as a
        // target operand for the jump instruction

        // I'm not sure if this is supposed to be a hint or not, but we
        // might as well increment the handed-in location by one page
        page_addr += page_size;

        kr = vm_allocate( me, &page_addr, page_size, TRUE );
        if ( kr == KERN_SUCCESS )
        {
            int deallocate = 0;

            high_jump_table = page_addr;
            high_table_size = page_size;
            high_table_offset = 0;

            // set protection on these pages
            kr = vm_protect( me, high_jump_table, high_table_size,
                             TRUE, VM_PROT_ALL );
            if ( kr != KERN_SUCCESS )
            {
                deallocate = 1;
            }
            else
            {
                // set current to maximum
                kr = vm_protect( me, high_jump_table, high_table_size,
                                 FALSE, VM_PROT_ALL );

                kr = vm_protect( me, low_jump_table, low_table_size,
                                 TRUE, VM_PROT_ALL );
                if ( kr != KERN_SUCCESS )
                {
                    deallocate = 1;
                    LogEmergency( "Unable to set protection on low jump table ! %d (%s)",
                                  kr, mach_error_string(kr) );
                }
                else
                {
                    // set current to maximum
                    kr = vm_protect( me, low_jump_table, low_table_size,
                                     FALSE, VM_PROT_ALL );
                }
            }

            if ( deallocate )
            {
                // argh -- failed !
                (void) vm_deallocate( me, low_jump_table, low_table_size );
                (void) vm_deallocate( me, high_jump_table, high_table_size );

                high_jump_table = low_jump_table = 0;
                high_table_size = low_table_size = 0;

                LogEmergency( "Unable to set protection on high jump table ! %d (%s)",
                              kr, mach_error_string(kr) );
            }
            else
            {
                // deallocate when we exit
                atexit( free_patch_tables );
            }
        }
        else
        {
            // failed to allocate second table, deallocate the first
            (void) vm_deallocate( me, low_jump_table, low_table_size );

            low_jump_table = low_table_size = 0;

            LogEmergency( "Unable to allocate low jump table ! %d (%s)",
                          kr, mach_error_string(kr) );
        }
    }
    else
    {
        LogEmergency( "Unable to allocate high jump table ! %d (%s)",
                      kr, mach_error_string(kr) );
    }
}

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
                LogEmergency( "Failed to make target memory writable ! %d (%s)",
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
                LogEmergency( "Failed to make target memory writable ! %d (%s)",
                              kr, mach_error_string(kr) );
            }
        }
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

    if ( high_jump_table != 0 )
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

        unsigned char saved_instr[32];
        unsigned char new_instr[32];
        vm_address_t fn_addr = ( vm_address_t ) in_fn_addr;
        vm_address_t patch_addr = ( vm_address_t ) in_patch_addr;
        vm_address_t low_entry = low_jump_table + low_table_offset;
        vm_address_t high_entry = high_jump_table + high_table_offset;
        size_t saved_size, low_size, high_size;

        // calculate size of instructions to save off, and generate
        // replacement instruction padded with no-ops
        if ( __calc_insn_size( in_fn_addr, (void *) (high_entry + 8),
                               new_instr, &saved_size ) )
        {
            // can't really do this atomically -- we could be reading
            // twenty-odd bytes here...
            memcpy( saved_instr, in_fn_addr, saved_size );

            // generate reentry island
            low_size = build_low_entry( low_entry, fn_addr + saved_size,
                                        saved_instr, saved_size );

            if ( low_size > 0 )
            {
                // generate patch island
                high_size = build_high_entry( high_entry, low_entry, patch_addr );
            }

	    // ensure we have valid blocks, and that they'll both fit
	    // into the tables
            if ( ( (low_size > 0) && (high_size > 0) ) &&
		 ( ( high_table_offset + high_size ) <= high_table_size ) &&
		 ( ( low_table_offset + low_size ) <= low_table_size ) )
            {
                // Ideally we want to use an atomic operation here, and
                // one which will allow us to re-save the initial
                // instruction block should it have changed in the
                // meantime

                // If the data is less than eight bytes in length, then
                // we can use a 64-bit cmpxchg instruction.

                // Unfortunately, anything more than that can't be done
                // atomically -- at least, not easily -- so we have to
                // rely on our mutexes, and hope nothing else (Unsanity,
                // mach_override) gets in the way during our
                // operation...

                if ( saved_size <= 8 )
                {
                    // we will write eight bytes at once, so fill out
                    // the end of new_instr as necessary
                    unsigned long long oldVal, newVal;
                    unsigned long long * addr = (unsigned long long *) in_fn_addr;

                    do
                    {
                        newVal = oldVal = *addr;
                        memcpy( &newVal, new_instr, saved_size );

                        // newVal now contains new_instr padded with
                        // some bytes which aren't to be changed
                        if ( DPCompareAndSwap64( oldVal, newVal, addr ) == 0 )
                        {
                            // it's changed beneath us

                            // recalculate instructions
                            // *pray* this call doesn't fail. It
                            // shouldn't ever do that
                            __calc_insn_size( in_fn_addr, (void *) (high_entry+8),
                                              new_instr, &saved_size );

                            // re-save instructions
                            memcpy( saved_instr, in_fn_addr, saved_size );

                            // re-generate reentry island
                            low_size = build_low_entry( low_entry, fn_addr + saved_size,
                                saved_instr, saved_size );
                        }
                        else
                        {
                            // all done
                            break;
                        }

                    } while ( saved_size < 8 );
                }

                // a change might have made the saved instructions get
                // larger than eight bytes, so we check again here.
                if ( saved_size > 8 )
                {
                    // copy the padded ljmp instruction into the target function...
                    memcpy( in_fn_addr, new_instr, saved_size );
                }

                // call msync() on each & update table offsets - flushes instruction cache
                vm_msync( mach_task_self( ), low_entry,
                          low_size, VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );
                vm_msync( mach_task_self( ), high_entry, 
                          high_size, VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );

                low_table_offset += low_size;
                high_table_offset += high_size;

                DPCodeSync( in_fn_addr );

                // set result - addr is address of first *instruction* in the new low addr table entry
                result = (void *) (low_entry + 8);
            }
        }
    }

    pthread_mutex_unlock( &patch_mutex );

    return ( result );
}

void DPRemovePatch( void * fn_addr )
{
    pthread_mutex_lock( &patch_mutex );

    // to get reentry_addr:
    // 1: Look at first byte of function, if it's 0xE9 it's a jump
    // 2: Read next four bytes: address of patch branch code
    // 3: Deduct four bytes from that, read address of reentry code
    // 4: Advance 19 bytes, read one-byte size of saved instr
    // 5: Advance one byte, read saved instr
    // 6: Copy into target function

    // 1
    if ( *((unsigned char *) fn_addr) == 0xE9 )
    {
        size_t size = 0;
        void * addr = NULL;

        // 2
        addr = *((void **)(fn_addr + 1));
        // 3
        addr = *((void **)(addr - 4));
        // 4
        addr += 19;
        size = (size_t) *((unsigned char *)addr);
        // 5
        addr++;
        // 6
        memcpy( fn_addr, addr, size );

        DPCodeSync( fn_addr );
    }

    pthread_mutex_unlock( &patch_mutex );
}

#endif
