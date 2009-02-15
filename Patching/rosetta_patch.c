/*
 *  rosetta_patch.c
 *  DynamicPatch
 *
 *  Created by jim on 26/3/2006.
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
#include <mach-o/dyld.h>

#include <machine/endian.h>

#include <CoreFoundation/CoreFoundation.h>

#include "lookup.h"
#include "Injection.h"

// this file includes the helper code, defined as an array like the
// templates below
#include "stub_helper_code.c"

// addresses of jump tables in target process
static vm_address_t low_jump_table = 0;
static vm_address_t high_jump_table = 0;

// addresses of data tables in target process
static vm_address_t data_table = 0;
static vm_address_t string_table = 0;

// allocated sizes of the tables
static vm_size_t low_table_size = 0;
static vm_size_t high_table_size = 0;
static vm_size_t data_table_size = 0;
static vm_size_t string_table_size = 0;

// offsets to next free byte in tables
static vm_size_t low_table_offset = 0;
static vm_size_t high_table_offset = 0;
static vm_size_t data_table_offset = 0;
static vm_size_t string_table_offset = 0;

// addresses of local working copies of the tables
// NB: string table is actually allocated with the data table
static void * local_low_table = NULL;
static void * local_high_table = NULL;
static void * local_data_table = NULL;

// default page size
static vm_size_t page_size = 4096;

// mutex to help avoid some nasty things
static pthread_mutex_t rosetta_mutex;

// a collection of macros to get at each individual byte of a number
#define _lbyte1(n)  (unsigned char)((((unsigned)(n)) & 0xff000000) >> 24)
#define _lbyte2(n)  (unsigned char)((((unsigned)(n)) & 0x00ff0000) >> 16)
#define _lbyte3(n)  (unsigned char)((((unsigned)(n)) & 0x0000ff00) >>  8)
#define _lbyte4(n)  (unsigned char) (((unsigned)(n)) & 0x000000ff)

#define _sbyte1(n)  (unsigned char)((((unsigned short)(n)) & 0xff00) >> 8)
#define _sbyte2(n)  (unsigned char) (((unsigned short)(n)) & 0x00ff)

#define _lshrt1(n)  (unsigned short)((((unsigned)(n)) & 0xffff0000) >> 16)
#define _lshrt2(n)  (unsigned short) (((unsigned)(n)) & 0x0000ffff)

// constants for checking validity
#define INVALID_STRING_OFFSET   0xFFFFFFFF
#define INVALID_INFO_INDEX      0xFFFFFFFF

// change this to be the real path to this framework. If it's installed
// inside your application, make it a settable value that you can tweak
// at run-time. It needs to be a FQPN.
#define FMWK_PATH   "/Library/Frameworks/DynamicPatch.framework/DynamicPatch"

// Data Table Structure:
struct rosetta_data_table_header
{
    // length of data table (will be multiple of page-size)
    vm_size_t data_table_size;

    // addresses of jump tables, so they can be deallocated
    // note that this *still* won't happen if no patched functions are
    // called
    vm_address_t low_jump_table;
    vm_size_t low_jump_table_size;
    vm_address_t high_jump_table;
    vm_size_t high_jump_table_size;

    // patch info table offset/count:
    unsigned info_table_offset;     // offset from data table start
    unsigned info_table_count;      // number of items in info table

    // string table - a block of data; items in info table contain
    // offsets from the start of the string table. Here we have an
    // offset to the start of the string table, relative to the start
    // of the data table
    unsigned string_table_offset;

    // one string gets coded in explicitly: its address is needed by
    // the stub helper code, and must be 'compiled' in
    // not that the size is a multiple of 4 to keep alignment. Padding
    // bytes don't matter, so long as the string itself is
    // null-terminated.
    char bind_fn_sym[24];           // "__rosetta_bind_helper"

    // at this point, we place the rosetta stub helper code, which
    // includes some static variables:
    // unsigned fmwk_ok;
    // const char * fmwk_path;      // zero, filled at runtime
    // const char * bind_fn_sym;    // addr of var in this header
    // void * load_fn;              // address of NSAddImage
    // void * sym_fn;               // address of dlsym
    // void * bind_fn;              // zero, filled at runtime
    // void * table_addr;           // address of this header
    unsigned char stub_helper_interface[1]; // actually larger

};

// Entry in the Loaded Modules table
struct rosetta_module_table_entry
{
    // path offset, copied from info table entry
    // used as a key to identify different modules
    unsigned key;

    // image of file
    NSObjectFileImage image;

    // linked module
    NSModule module;

};

// Patch Info Table Entry:
struct rosetta_info_table_entry
{
    // address of branch-to-original block
    // this is what the normal patch functions would return
    void * branch_original_code;

    // offset in the string table to the path of the bundle containing
    // code for this patch
    unsigned patch_bundle_path_offset;

    // offset of the patch function within its file image
    unsigned patch_fn_offset;

};

static struct rosetta_info_table_entry *pInfoTable = NULL;
static unsigned info_count = 0;

// used only in target process
static struct rosetta_module_table_entry *pModules = NULL;
static unsigned module_count = 0;
static unsigned module_avail = 0;

// The branch island templates. Unlike the standard PPC one, there are
// actually two different ones. The re-entry island is the same as the
// original -- although in this case the normally optional modification
// or r12 prior to the branch instruction is explicitly coded. The
// patch island is slightly different, since it needs to load some more
// registers for use by the stub function.
// Note that since this code is intended to be compatible with both
// PowerPC and Intel processors (but is expected to be used mostly on
// Intel) all the instructions are stored as arrays of single bytes, so
// endianness doesn't enter into the equation.

// re-entry template
static unsigned char rosetta_reentry_template[] =
{
    0x00,0x00,0x00,0x00,     // .long    branch_target_addr
    0x00,0x00,0x00,0x00,     // .long    error_branch_target
    0x3D,0x60,0x00,0x00,     // lis      r11,(msw of this_entry_addr)
    0x61,0x6B,0x00,0x00,     // ori      r11,r11,(lsw of this_entry_addr)
    0x81,0x8B,0x00,0x00,     // lwz      r12,0(r11)  -- loads branch_target_addr into r12
    0x2C,0x0C,0x00,0x00,     // cmpwi    r12,0
    0x7D,0x89,0x03,0xA6,     // mtctr    r12
    0x39,0x8C,0xFF,0xFC,     // addi     r12,r12,-4  -- make r12 hold real function address
    0x60,0x00,0x00,0x00,     // nop                  -- store first instruction here
    0x4C,0x82,0x04,0x20,     // bnectr
    0x81,0x8B,0x00,0x04,     // lwz      r12,4(r11)  -- loads error_branch_target into r12
    0x7D,0x89,0x03,0xA6,     // mtctr    r12
    0x4E,0x80,0x04,0x20      // bctr                 -- call error handler
};
#define low_entry_size sizeof(rosetta_reentry_template)

// branch template
// this loads the address of the memory holding the branch target into
// register 13, so that the stub function can overwrite it with the
// real patch address after it's been linked. This works in much the
// same way as the dyld stub binding helper. It also loads a data value
// into r14 -- this is the index into the patch table copied in during
// the injection process, and provides the bind routine with the
// information it needs to load & link the patch function.
static unsigned char rosetta_patch_template[] =
{
    0x00,0x00,0x00,0x00,    // .long    branch_target_addr
    0x00,0x00,0x00,0x00,    // .long    error_branch_target
    0x00,0x00,0x00,0x00,    // .long    patch_table_index
    0x3D,0x60,0x00,0x00,    // lis      r11,(msw of this_entry_addr)
    0x61,0x6B,0x00,0x00,    // ori      r11,r11,(lsw of this_entry_addr)
    0x81,0x8B,0x00,0x00,    // lwz      r12,0(r11)  -- r12 <- branch_target_addr
    0x7D,0x6D,0x5B,0x78,    // mr       r13,r11     -- r13 <- address of branch_target_addr
    0x81,0xCB,0x00,0x08,    // lwz      r14,8(r11)  -- r14 <- patch_table_index
    0x2C,0x0C,0x00,0x00,    // cmpwi    r12,0
    0x7D,0x89,0x03,0xA6,    // mtctr    r12
    0x4C,0x82,0x04,0x20,    // bnectr
    0x81,0x8B,0x00,0x04,    // lwz      r12,4(r11)  -- loads error_branch_target into r12
    0x7D,0x89,0x03,0xA6,    // mtctr    r12
    0x4E,0x80,0x04,0x20     // bctr                 -- call error handler
};
#define high_entry_size sizeof(rosetta_patch_template)

// offsets
#define branch_target_offset        0
#define error_handler_offset        4
#define patch_index_offset          8
#define saved_instruction_offset   32

// setup to be called at exit time. This will only happen if a patch
// gets used, though: otherwise it'll just leak. However, it doesn't
// matter *which* patch gets used. Only that the binder function gets
// loaded and called by the stub helper assembly code.
static void _rosetta_free_tables( void )
{
    if ( high_jump_table != 0 )
    {
        (void) vm_deallocate( mach_task_self( ),
                              high_jump_table,
                              high_table_size );
    }
    if ( low_jump_table != 0 )
    {
        (void) vm_deallocate( mach_task_self( ),
                              high_jump_table,
                              high_table_size );
    }
    if ( data_table != 0 )
    {
        (void) vm_deallocate( mach_task_self( ), data_table,
                              data_table_size );
    }
    if ( pModules != NULL )
    {
        // release/deallocate each module
        unsigned i;
        for ( i = 0; i < module_count; i++ )
        {
            if ( pModules[i].module != NULL )
            {
                NSUnLinkModule( pModules[i].module,
                                NSUNLINKMODULE_OPTION_NONE );
            }
            if ( pModules[i].image != NULL )
            {
                NSDestroyObjectFileImage( pModules[i].image );
            }
        }

        // release memory used for the table
        free( pModules );
    }
}

// in the build functions, the last parameter is the address of a block
// of memory in which to write the data
static size_t __rosetta_build_low_entry( vm_address_t this_entry_addr,
                                         vm_address_t reentry_addr,
                                         unsigned int saved_instruction,
                                         void * address )
{
    unsigned char * data_ptr = (unsigned char *) address;
    size_t result = sizeof(rosetta_reentry_template);

    memcpy( data_ptr, rosetta_reentry_template, result );

    // store branch target
    OSWriteBigInt32( data_ptr, branch_target_offset, reentry_addr );
    // store saved instruction - already big-endian
    *((unsigned int *)(data_ptr + saved_instruction_offset)) = saved_instruction;

    // write hi & lo words of this_entry_addr into instructions
    data_ptr[10] = _lbyte1(this_entry_addr);
    data_ptr[11] = _lbyte2(this_entry_addr);
    data_ptr[14] = _lbyte3(this_entry_addr);
    data_ptr[15] = _lbyte4(this_entry_addr);

    return ( result );
}

static size_t __rosetta_build_high_entry( vm_address_t this_entry_addr,
                                          vm_address_t low_entry_addr,
                                          vm_address_t stub_fn_addr,
                                          unsigned int patch_table_index,
                                          void * address )
{
    unsigned char * data_ptr = (unsigned char *) address;
    size_t result = sizeof(rosetta_patch_template);

    memcpy( data_ptr, rosetta_patch_template, result );

    // store branch target
    OSWriteBigInt32( data_ptr, branch_target_offset, stub_fn_addr );
    // store error handler
    OSWriteBigInt32( data_ptr, error_handler_offset, low_entry_addr );
    // store patch table index
    OSWriteBigInt32( data_ptr, patch_index_offset, patch_table_index );

    // fill in address of this block in instructions
    data_ptr[14] = _lbyte1(this_entry_addr);
    data_ptr[15] = _lbyte2(this_entry_addr);
    data_ptr[18] = _lbyte3(this_entry_addr);
    data_ptr[19] = _lbyte4(this_entry_addr);

    return ( result );
}

static unsigned __rosetta_locate_string( const char * str )
{
    unsigned result = INVALID_STRING_OFFSET;

    // if the string table is empty, then the string isn't there yet
    if ( string_table_offset > 0 )
    {
        const char * s = (const char *) (local_data_table + page_size);
        const char * e = s + string_table_offset;
        const char * p = s;

        while ( p < e )
        {
            if ( strcmp( p, str ) == 0 )
            {
                // found the string -- return offset
                result = p - s;
                break;
            }
            else
            {
                p = strchr( p, '\0' );
                p++;
            }
        }
    }

    return ( result );
}

// adds a string to the string table
// returns offset of the added string
static unsigned __rosetta_add_string( const char * str )
{
    // see if there's already an instance in the table
    unsigned result = __rosetta_locate_string( str );

    if ( result == INVALID_STRING_OFFSET )
    {
        // add the given string to the string table, if that's possible
        vm_size_t sz = (vm_size_t) strlen( str ) + 1;

        if ( (string_table_size - string_table_offset) >= sz )
        {
            // there's enough room for the string in the table
            memcpy( (local_data_table + page_size + string_table_offset),
                    str, sz );
            result = (unsigned) string_table_offset;
            string_table_offset += sz;
        }
    }

    return ( result );
}

// creates an entry in the info table
// branch_addr is address of the re-entry island's first instruction
// patch_bundle_path is the path to the path bundle, which will be
// written into the string table
// patch_fn_offset is the bundle-relative address of the patch function
static unsigned __rosetta_create_info_entry( void * branch_addr,
                                             const char * patch_bundle_path,
                                             unsigned patch_fn_offset )
{
    unsigned result = INVALID_INFO_INDEX;
    vm_size_t sz = sizeof(struct rosetta_info_table_entry);

    if ( (data_table_size - data_table_offset) >= sz )
    {
        // enough room for one more
        void * addr = local_data_table + data_table_offset;

        // see if we can store the string, first:
        unsigned pathOffset;
        pathOffset = __rosetta_add_string( patch_bundle_path );
        if ( pathOffset != INVALID_STRING_OFFSET )
        {
            // okay to add this bit
            // probablty should use offsetof, but I feel reasonably
            // secure in assuming that a structure containing three
            // 32-bit words will use < 32-bit alignment
            OSWriteBigInt32( addr, 0, (unsigned) branch_addr );
            OSWriteBigInt32( addr, 4, pathOffset );
            OSWriteBigInt32( addr, 8, patch_fn_offset );

            data_table_offset += sizeof(struct rosetta_info_table_entry);
            result = info_count++;
        }
    }

    return ( result );
}

// these were long things to type so often, so they got macros
#define HDR_OFFSET(m) offsetof(struct rosetta_data_table_header, m)
#define PACK_HEADER_DWORD(m,v) OSWriteBigInt32(local_data_table, HDR_OFFSET(m), v)

// this function will fill out the constant/static things in the data
// header. The data comes from globals within this file.
// This packs not only the things defined in the header structure
// above, but also fills out the data block at the beginning of the
// stub interface code.
static void __rosetta_create_data_header( void )
{
    // size of the header struct
    size_t header_block_size = offsetof(struct rosetta_data_table_header, stub_helper_interface);
    // size of the stub code
    size_t stub_code_size = sizeof(_stub_helper_code);
    // size of entire block
    size_t data_header_size = header_block_size + stub_code_size;
    // location of stub code block
    void * stub_code_addr = local_data_table + HDR_OFFSET(stub_helper_interface);
    // location of stub block in remote process
    vm_address_t stub_addr = data_table + HDR_OFFSET(stub_helper_interface);
    // address of PPC implementation of NSAddImage
    void * NSAddImage_ppc_ptr = NULL;
    // address of PPC implementation of dlsym
    void * dlsym_ppc_ptr = NULL;

    // lookup these function pointers
    NSAddImage_ppc_ptr = DPFindFunctionForArchitecture( "_NSAddImage",
        "/usr/lib/libSystem.dylib", kInsertionArchPPC );
    dlsym_ppc_ptr = DPFindFunctionForArchitecture( "_dlsym",
        "/usr/lib/libSystem.dylib", kInsertionArchPPC );

    PACK_HEADER_DWORD( data_table_size, data_table_size );
    PACK_HEADER_DWORD( low_jump_table, low_jump_table );
    PACK_HEADER_DWORD( low_jump_table_size, low_table_size );
    PACK_HEADER_DWORD( high_jump_table, high_jump_table );
    PACK_HEADER_DWORD( high_jump_table_size, high_table_size );
    PACK_HEADER_DWORD( info_table_offset, data_header_size );
    // info_table_count will get filled in later
    PACK_HEADER_DWORD( string_table_offset, page_size );
    strncpy( local_data_table + HDR_OFFSET(bind_fn_sym),
             "__rosetta_bind_helper", 24 );
    // copy stub code at the end
    memcpy( stub_code_addr, _stub_helper_code, stub_code_size );

    // also need to fill in some things in the stub code
    // first thing is address of framework path, which is first item in
    // the string table -- so address of string table
    (void) __rosetta_add_string( FMWK_PATH );
    // store addresses:
    // addr of framework path
    OSWriteBigInt32( stub_code_addr, fmwk_path_offset, string_table );
    // addr of stub binder symbol
    OSWriteBigInt32( stub_code_addr, bind_fn_sym_offset,
                     (unsigned) (data_table + HDR_OFFSET(bind_fn_sym)) );
    // address of NSAddImage
    OSWriteBigInt32( stub_code_addr, load_fn_offset,
                     (unsigned) NSAddImage_ppc_ptr );
    // address of dlsym
    OSWriteBigInt32( stub_code_addr, sym_fn_offset,
                     (unsigned) dlsym_ppc_ptr );
    // address of data table
    OSWriteBigInt32( stub_code_addr, table_addr_offset, data_table );

    // lastly, put the code block's end address into some instructions
    // so they can reach the data table
    unsigned short hiword = _lshrt1(stub_addr);
    unsigned short loword = _lshrt2(stub_addr);

    OSWriteBigInt16( stub_code_addr, table_offset_hi, hiword );
    OSWriteBigInt16( stub_code_addr, table_offset_lo, loword );

    // setup offsets ready to use when writing info table
    data_table_offset = data_header_size;
}

// this fills out the variables in the header block that contain daa
// modified during hte injection setup process
static void __rosetta_complete_data_header( void )
{
    // fill in the last field
    PACK_HEADER_DWORD( info_table_count, info_count );
}

// this function will allocate space for the high and low memory jump
// tables inside the target task. If both those succeed, then it will
// allocate a two-page block to hold the data table and string table.
// The data table goes into the first page, the string table into the
// second. We pretty much assume that the data table won't grow to be
// 4Kb in size, and that the string table is therefore safe from
// corruption. If they're likely to grow some more, then this code
// could be altered to allocate twice as much, etc. We need to have it
// allocated within the target task NOW however, since any patches we
// build will need to point themselves at the stub interface code
// located within this block.
static void __rosetta_allocate_tables( task_t taskPort )
{
    // use vm_allocate to get a couple of pages in or around a couple certain addresses
    kern_return_t kr = KERN_SUCCESS;
    vm_address_t page_addr = 0;

    kr = host_page_size( mach_host_self( ), &page_size );

    if ( kr != KERN_SUCCESS )
    {
        // make an educated guess at 4096 bytes
        page_size = 4096;
    }

    // allocate one page for each jump table
    // low memory one first...
    kr = vm_allocate( taskPort, &page_addr, page_size, TRUE );

    if ( kr == KERN_SUCCESS )
    {
        int done = 0, err = 0;
        low_jump_table = page_addr;
        low_table_size = page_size;
        low_table_offset = 0;

        // For PowerPC Branch Absolute instructions, we need a 26-bit
        // address which gets sign-extended. This means that the 26th
        // bit (with first being least significant bit) needs to be a
        // *one*, along with all those above it. This gives us a value
        // of 0xFE000000 as the lowest possible page address. However,
        // it's just possible that memory at that address would be in
        // use, so we'll start a little higher, so we're not inspecting
        // vast wodges of memory via many Mach IPC calls.

        // note that in <mach/ppc/vm_param.h>, the highest page address
        // is 0xFFFFF000 -- PowerPC uses *all* 32-bit memory
        // also note that in <mach/i386/vm_param.h>, the highest
        // vm_address is 0xFFC00000 (PAE mode is 0xFFE00000)
        // On PowerPC, that would mean we'd be looking at the last 16
        // pages available (starting with -0xFFFF0000), so do it with
        // math to cover the Intel version in the same manner.
        page_addr = VM_MAX_PAGE_ADDRESS - (page_size << 4);
        
        do
        {
            vm_address_t region_addr = page_addr;
            vm_size_t region_size = 0;
            vm_region_flavor_t region_flavor = VM_REGION_BASIC_INFO;
            struct vm_region_basic_info region_info;
            mach_msg_type_number_t info_count = sizeof(struct vm_region_basic_info);
            memory_object_name_t region_object_name;

            kr = vm_region( taskPort, &region_addr, &region_size, region_flavor,
                            (vm_region_info_t) &region_info, &info_count,
                            &region_object_name );

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
                    vm_size_t max_len = 0xffffffff - region_addr;

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

        if ( !done )
        {
            // deallocate low memory jump table - we won't be needing it...
            kr = vm_deallocate( taskPort, low_jump_table, low_table_size );

            low_jump_table = 0;
            low_table_size = 0;
        }
        else
        {
            // we've got a full address for the next page...
            // ...allocate it...
            // ...but don't allow the kernel to relocate this wherever it sees fit...
            kr = vm_allocate( taskPort, &page_addr, page_size, FALSE );

            if ( kr == KERN_SUCCESS )
            {
                high_jump_table = page_addr;
                high_table_size = page_size;
                high_table_offset = 0;

                // now allocate two page anywhere for the data table +
                // string table
                kr = vm_allocate( taskPort, &page_addr, page_size * 2, TRUE );

                if ( kr == KERN_SUCCESS )
                {
                    // actual data table uses first page...
                    data_table = page_addr;
                    data_table_size = page_size;

                    // ...and is followed by string table
                    string_table = page_addr + page_size;
                    string_table_size = page_size;
                }
                else
                {
                    // deallocate jump tables
                    (void) vm_deallocate( taskPort, high_jump_table,
                                          high_table_size );
                    (void) vm_deallocate( taskPort, low_jump_table,
                                          low_table_size );

                    high_jump_table = 0;
                    high_table_size = 0;
                    low_jump_table = 0;
                    low_table_size = 0;
                }
            }
            else
            {
                // deallocate the low memory jump table
                kr = vm_deallocate( taskPort, low_jump_table, low_table_size );

                low_jump_table = 0;
                low_table_size = 0;
            }
        }
    }
}

// makes a page of the target task writable
int __rosetta_make_writable( task_t taskPort, vm_address_t fn_addr )
{
    kern_return_t kr = KERN_SUCCESS;
    vm_address_t region_start = ( vm_address_t ) fn_addr;
    vm_size_t region_size = 0;
    vm_region_flavor_t region_flavor = VM_REGION_BASIC_INFO;
    struct vm_region_basic_info region_info;
    mach_msg_type_number_t region_info_count = sizeof( struct vm_region_basic_info );
    memory_object_name_t region_object_name = 0;
    int success = 1;

    bzero( &region_info, sizeof( struct vm_region_basic_info ) );

    // make sure we can write to this region of virtual memory...
    kr = vm_region( taskPort, &region_start, &region_size, region_flavor,
                    (vm_region_info_t) &region_info, &region_info_count,
                    &region_object_name );

    if ( kr == KERN_SUCCESS )
    {
        if ( (region_info.max_protection & VM_PROT_WRITE) == 0 )
        {
            success = 0;
            kr = vm_protect( taskPort, region_start, region_size, TRUE,
                             region_info.max_protection | VM_PROT_WRITE | VM_PROT_COPY );

            if ( kr == KERN_SUCCESS )
                success = 1;
        }

        if ( ( success ) && ( region_info.protection & VM_PROT_WRITE ) == 0 )
        {
            // no write permission, have to add it...
            success = 0;
            kr = vm_protect( taskPort, region_start, region_size, FALSE,
                             region_info.protection | VM_PROT_WRITE );

            if ( kr == KERN_SUCCESS )
            {
                success = 1;
            }
        }
    }

    return ( success );
}

// deallocates the tables allocated in the target task
static void __rosetta_clear_remote_tables( task_t taskPort )
{
    if ( ( low_jump_table != 0 ) &&
         ( low_table_size > 0 ) )
    {
        (void) vm_deallocate( taskPort, low_jump_table,
                              low_table_size );
        low_jump_table = 0;
        low_table_size = 0;
    }
    if ( ( high_jump_table != 0 ) &&
         ( high_table_size > 0 ) )
    {
        (void) vm_deallocate( taskPort, high_jump_table,
                              high_table_size );
        high_jump_table = 0;
        high_table_size = 0;
    }
    if ( ( data_table != 0 ) &&
         ( (data_table_size > 0) ||
           (string_table_size > 0) ) )
    {
        (void) vm_deallocate( taskPort, data_table,
                              data_table_size + string_table_size );
        data_table = 0;
        data_table_size = 0;
        string_table_size = 0;
    }
}

// this function looks up the address of the reentry island, ready to
// pass back to the patch bundle so it can call the original functions
// it has patched
static void * __rosetta_lookup_patch( void * fn_addr )
{
    void * result = NULL;

    // get patch branch address from the start of the given function
    unsigned instr = *((unsigned *) fn_addr);

    // see RemovePatch() in ppc_patch.c for info about this
    if ( (instr & 0xFE000003) == 0x4A000002 )
    {
        // this is a patched function -- lookup the corresponding entry
        // in the info table
        unsigned index = INVALID_INFO_INDEX;

        // address of a patched function
        // clear bottom three bits, set top six bits
        unsigned * addr = (unsigned *) ((instr & ~3) | 0xFC000000);
        // decrement by four bytes to read patch table index
        addr--;
        index = *addr;

        // get address from that entry
        result = pInfoTable[index].branch_original_code;
    }

    return ( result );
}

// this stores information about a loaded module in the loaded modules
// table, alloating or reallocating as necessary. There will be a
// single entry for each patch module loaded.
// The key is a unique identifier used for quick searching; it is the
// offset in the string table to the path string for this module.
static void __rosetta_store_module( NSObjectFileImage img, NSModule mod,
                                    unsigned key )
{
    if ( module_avail == 0 )
    {
        module_avail = 4;
        pModules = (struct rosetta_module_table_entry *)
                   malloc( module_avail *
                           sizeof(struct rosetta_module_table_entry) );
    }
    if ( module_avail == module_count )
    {
        module_avail += 4;
        pModules = (struct rosetta_module_table_entry *)
                   realloc( pModules, module_avail *
                            sizeof(struct rosetta_module_table_entry) );
    }

    pModules[module_count].key = key;
    pModules[module_count].image = img;
    pModules[module_count].module = mod;

    module_count++;
}

// looks up the given module, so see if it's already been loaded. The
// key is the offset of the module's path in the string table.
static NSModule __rosetta_lookup_module( unsigned key )
{
    NSModule result = NULL;

    if ( pModules != NULL )
    {
        unsigned i;
        for ( i = 0; i < module_count; i++ )
        {
            if ( pModules[i].key == key )
            {
                result = pModules[i].module;
                break;
            }
        }
    }

    return ( result );
}

// if a patch bundle is not already loaded, this loads & links it, and
// creates an entry for it in the loaded module table
static NSModule __rosetta_load_bundle( const char * path, unsigned key )
{
    NSModule result = NULL;
    NSObjectFileImage image;
    NSObjectFileImageReturnCode ret;
    int errnum;
    const char * errstr = NULL;
    const char * errfile = NULL;
    NSLinkEditErrors linkerr = NSLinkEditUndefinedError;

    ret = NSCreateObjectFileImageFromFile( path, &image );
    if ( ret == NSObjectFileImageSuccess )
    {
        result = NSLinkModule( image, path,
                               (NSLINKMODULE_OPTION_BINDNOW |
                                NSLINKMODULE_OPTION_RETURN_ON_ERROR) );
        if ( result != NULL )
        {
            __rosetta_store_module( image, result, key );
        }
        else
        {
            NSLinkEditError( &linkerr, &errnum, &errfile, &errstr );
            (void) NSDestroyObjectFileImage( image );
        }
    }

    if ( result == NULL )
    {
        // if we can't link the bundle, we'll crash later. However,
        // it's better to crash here, in an actual function, than to
        // crash in the stub interface code which gdb & co see as just
        // some random address with no associated name.
        abort( );
    }

    return ( result );
}

// this will find the mach_header for the loaded module, and will look
// up the vmaddr_slide (offset from zero) of that module, so we can
// work out the address of the patch handler function.
static unsigned __rosetta_get_vmaddr_slide( const char * path )
{
    unsigned result = 0;
    const char * name = NULL;
    int32_t i, num = _dyld_image_count( );

    for ( i = 0; i < num; i++ )
    {
        name = _dyld_get_image_name( i );
        if ( ( name != NULL ) &&
             ( strncmp( name, path, PATH_MAX ) == 0 ) )
        {
            // this is the one
            result = (unsigned) _dyld_get_image_vmaddr_slide( i );
            break;
        }
    }

    return ( result );
}

// This is the C part of the patch bundle runtime loader & linker
// It takes an index into the patch info table, used to identify the
// patch being called; the address of the location in which the patch
// function address should be written; and the address of the patch
// data table.
// It's an external symbol, since the stub code needs to load this
// library and lookup the address of this function in order to call it.
void * __rosetta_bind_helper( unsigned index, void * addressPtr,
                              void * data_table_addr )
{
    // cause access violation on error -- pagezero access
    void * result = NULL;

    // could be the first time this has been called
    if ( data_table == 0 )
    {
        data_table = (vm_address_t) data_table_addr;

        // well, it *could* happen
        if ( data_table != 0 )
        {
            // pull out the data into the globals
            struct rosetta_data_table_header *pHdr =
                (struct rosetta_data_table_header *) data_table;
            pthread_mutexattr_t mattrs;

            data_table_size = pHdr->data_table_size;
            string_table_size = pHdr->data_table_size;  // meh

            low_jump_table = pHdr->low_jump_table;
            low_table_size = pHdr->low_jump_table_size;
            high_jump_table = pHdr->high_jump_table;
            high_table_size = pHdr->high_jump_table_size;

            pInfoTable = (struct rosetta_info_table_entry *)
                         (data_table + pHdr->info_table_offset);
            info_count = pHdr->info_table_count;

            string_table = data_table + pHdr->string_table_offset;

            pthread_mutexattr_init( &mattrs );
            pthread_mutexattr_settype( &mattrs, PTHREAD_MUTEX_RECURSIVE );
            pthread_mutex_init( &rosetta_mutex, &mattrs );
            pthread_mutexattr_destroy( &mattrs );

            // also take this opportunity to get things deallocated on
            // exit:
            atexit( _rosetta_free_tables );
        }
    }

    if ( data_table != 0 )
    {
        if ( pthread_mutex_lock( &rosetta_mutex ) == 0 )
        {
            unsigned key = pInfoTable[index].patch_bundle_path_offset;
            const char * path = (const char *) string_table + key;
            unsigned offset = pInfoTable[index].patch_fn_offset;
            NSModule module = NULL;
            unsigned vmaddr_slide = 0;

            // attempt to load bundle
            // can't use NSAddImage, since that only works for dylib
            // binaries, not bundles

            // search for existing header
            module = __rosetta_lookup_module( key );
            if ( module == NULL )
            {
                NSSymbol sym = 0;
                patch_link_fn_t patchlink = NULL;

                // okay, it wasn't loaded yet, so load it
                module = __rosetta_load_bundle( path, key );

                // now we look up the patch-link function and call it;
                // this lets the newly loaded code in the bundle get
                // access to the re-entry island pointers ordinarily
                // returned from CreatePatch
                // again, no return-on-error, this will exit on failure
                sym = NSLookupSymbolInModule( module, kLinkPatchBundleSymbolName );
                patchlink = NSAddressOfSymbol( sym );

                // we pass it the address of a function here which will
                // lookup the appropriate pointer from the patch into
                // table
                patchlink( __rosetta_lookup_patch, path );

                // patch bundle is now up to date, it has re-entry
                // addresses for all functions it's patched, that sort
                // of thing
            }

            // lib_hdr should never be NULL here -- should exit on
            // error

            // get vmaddr slide of object image -- needs to look for
            // name in dyld image table to find it
            vmaddr_slide = __rosetta_get_vmaddr_slide( path );

            // calculate patch function address using address slide and
            // patch offset within image
            result = (void *) (vmaddr_slide + offset);

            // store this so that future calls go straight to the patch
            // function, rather than coming here
            *((void **)addressPtr) = result;
        }
    }

    return ( result );
}

void _rosetta_clear_data( void )
{
    if ( local_low_table != NULL )
    {
        free( local_low_table );
        local_low_table = NULL;
    }

    if ( local_high_table != NULL )
    {
        free( local_high_table );
        local_high_table = NULL;
    }

    if ( local_data_table != NULL )
    {
        free( local_data_table );
        local_data_table = NULL;
    }

    low_jump_table = 0;
    high_jump_table = 0;
    data_table = 0;
    string_table = 0;

    low_table_size = 0;
    high_table_size = 0;
    data_table_size = 0;
    string_table_size = 0;

    low_table_offset = 0;
    high_table_offset = 0;
    string_table_offset = 0;

    info_count = 0;
}

int _rosetta_commit_data( task_t taskPort )
{
    int result = 0;

    if ( ( low_jump_table != 0 ) &&
         ( high_jump_table != 0 ) &&
         ( data_table != 0 ) )
    {
        // finish off header block
        __rosetta_complete_data_header( );

        // copy local data blocks into remote process
        kern_return_t kr = vm_write( taskPort, low_jump_table,
                                     (vm_address_t) local_low_table,
                                     low_table_size );
        if ( kr == KERN_SUCCESS )
        {
            kr = vm_write( taskPort, high_jump_table,
                           (vm_address_t) local_high_table,
                           high_table_size );
            if ( kr == KERN_SUCCESS )
            {
                kr = vm_write( taskPort, data_table,
                               (vm_address_t) local_data_table,
                               data_table_size + string_table_size );
                if ( kr == KERN_SUCCESS )
                {
                    result = 1;

                    // byte-swap the address before writing it out
                    /*unsigned swapped = 0;
                    OSWriteBigInt32( &swapped, 0, data_table );
                    (void) vm_write( taskPort, _table_address_storage_addr,
                                     (vm_address_t) &swapped,
                                     sizeof(void *) );*/

                    (void) vm_msync( taskPort, low_jump_table,
                                     low_table_size,
                                     VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );
                    (void) vm_msync( taskPort, high_jump_table,
                                     high_table_size,
                                     VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );
                    (void) vm_msync( taskPort, data_table,
                                     data_table_size + string_table_size,
                                     VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );
                }
            }
        }
    }

    if ( result == 0 )
    {
        // deallocate remote memory now
        __rosetta_clear_remote_tables( taskPort );
    }

    return ( result );
}

unsigned _rosetta_install_patch_stub( void * targetAddr, unsigned patchOffset,
                                      const UInt8 * urlStr, task_t taskPort )
{
    unsigned result = 0;

    // not much point doing anything else if we can't get write access to patch the function...
    if ( !__rosetta_make_writable( taskPort, (vm_address_t) targetAddr ) )
    {
        // urgh, multiple return paths
        return( 0 );
    }

    // allocate memory for jump tables
    if ( high_jump_table == 0 )	// either both will be allocated, or neither
    {
        __rosetta_allocate_tables( taskPort );

        // allocate local versions to work on, too
        local_low_table = malloc( low_table_size );
        local_high_table = malloc( high_table_size );
        local_data_table = malloc( data_table_size + string_table_size );

        // generate initial header data
        __rosetta_create_data_header( );
    }

    if ( (high_jump_table != 0) &&
         ((high_table_offset + high_entry_size) <= high_table_size) &&
         ((low_table_offset + low_entry_size) <= low_table_size))
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
        vm_offset_t readData;
        vm_address_t fn_addr = (vm_address_t) targetAddr;
        vm_address_t low_table_entry = low_jump_table + low_table_offset;
        vm_address_t high_table_entry = high_jump_table + high_table_offset;

        void * low_entry_addr = local_low_table + low_table_offset;
        void * high_entry_addr = local_high_table + high_table_offset;

        size_t low_size = 0, high_size = 0;

        vm_address_t stub_addr = data_table + HDR_OFFSET(stub_helper_interface) + stub_code_offset;

        // read first instruction of function to patch
        vm_read_overwrite( taskPort, (vm_address_t) fn_addr, 4,
                           (vm_address_t) &saved_instruction, &readData );

        // generate jump table entry in low memory
        low_size = __rosetta_build_low_entry( low_table_entry, fn_addr + 4,
                                              saved_instruction, low_entry_addr );

        // generate high memory jump table entry
        high_size = __rosetta_build_high_entry( high_table_entry, low_table_entry,
                                                stub_addr, info_count, high_entry_addr );

        if ( (low_size > 0) && (high_size > 0) )
        {
            // get a branch absolute instruction to patch the target function
            // need to point to first instruction in high_table_entry (high_table_entry + 4, then)
            result = 0x48000002;   // branch instruction, with Absolute Address bit set
            result |= ((high_table_entry + 8) & 0x03FFFFFC);   // address, with high 6 & low 2 bits cleared

            // create the info table entry
            // this increments the info_count global
            (void) __rosetta_create_info_entry( (void *) (low_table_entry + 8),
                                                (const char *) urlStr,
                                                patchOffset );

            // update offsets, ready for the next patches
            low_table_offset += low_size;
            high_table_offset += high_size;

            // all done ! Actual patching of the target function is
            // done inside the remote process by injected code
        }
    }

    return ( result );
}
