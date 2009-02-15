/*
 *  Injector.cpp
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

#include <pthread.h>

#include <sys/stat.h>

#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/task.h>
#include <mach/thread_status.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <mach/vm_statistics.h>
#include <mach/machine/vm_types.h>

#include <mach-o/getsect.h>
#include <mach-o/dyld.h>

#include <machine/vmparam.h>
#include <machine/endian.h>

#include <architecture/ppc/cframe.h>

#include <dlfcn.h>

#include "Injector.h"
#include "rosetta_patch.h"
#include "atomic.h"
#include "load_bundle.h"
#include "logging.h"
#include "lookup.h"
#include "injection.h"
#include "apps.h"

#include "code_blocks.c"
#include "../Patching/stub_helper_code.c"

// set to 1 to have target tasks suspended while we write data
#define SUSPEND_TARGETS     0

// some weak imports of system functions. The idea is that
// dlopen/dlsym will only be used if they're available, 10.3 onwards.
#pragma weak dlopen
#pragma weak dlsym
#pragma weak NSAddImage
#pragma weak NSLookupAndBindSymbol

// hidden internal variables from the DynamicPatch framework
extern "C"
{
extern __loadimage_fn_ptr _newthread_lookup_fn;

// change this to be the real path to this framework. If it's installed
// inside your application, make it a settable value that you can tweak
// at run-time. It needs to be a FQPN.
static const char * lib_name = "/Library/Frameworks/DynamicPatch.framework/DynamicPatch";

// these are stored as NSLookup-friendly symbol names. If using
// dyld, we simply don't copy the first character
static const char * start_name = "___start_all_patches";
static const char * specific_name = "___start_one_patch";

// from pthread.c - actually some sort of syscall, IIRC
// actually a private symbol (gnh?), so I'll have to look it up
//  using the lookup routines...
//extern void _pthread_set_self( pthread_t );
extern int _pthread_create( pthread_t, const pthread_attr_t *, void *, const mach_port_t );

};

// these come from pthread.c
#if defined(__ppc__)
static const vm_address_t STACK_HINT  = 0xF0000000;
#elif defined(__i386__)
static const vm_address_t STACK_HINT  = 0xB0000000;
#else
#error Unsupported architecture
#endif

#pragma mark -

// this is used to double-check that we're not using the wrong patcher.
static bool IsRosettaTask( task_t taskPort )
{
    bool result = false;

    // check to see whether the target process is native (Intel) or running via Rosetta (PowerPC)
    u_int32_t rosettaHdr = 0;

    // don't segfault if it's not there - check with vm_region whether there's anything
    //  readable at that page first
    vm_address_t region_addr = 0xb8000000;
    vm_size_t region_size = 0;
    vm_region_flavor_t region_flavor = VM_REGION_BASIC_INFO;
    struct vm_region_basic_info region_info;
    mach_msg_type_number_t info_count = sizeof( struct vm_region_basic_info );
    memory_object_name_t region_object_name;

    kern_return_t kr = vm_region( taskPort, &region_addr, &region_size, region_flavor,
                                  ( vm_region_info_t ) &region_info,
                                  &info_count, &region_object_name );

    // vm_region will start at the given address & keep looking forwards until it finds an
    //  allocated region, so
    if ( ( kr == KERN_SUCCESS ) && ( region_addr == 0xb8000000 ) )
    {
        vm_offset_t readData = 0;
        if ( vm_read_overwrite( taskPort, region_addr, 4, (vm_address_t) &rosettaHdr, &readData ) == KERN_SUCCESS )
        {
            // we can read it - check what's there
            if ( ( rosettaHdr == MH_MAGIC ) ||
                 ( rosettaHdr == MH_CIGAM ) )
            {
                // highly unlikely this will be there in any other
                // circumstance
                result = true;
            }
        }
    }

    return ( result );
}

static void GetFunctionPointers( newthread_args_t *pArgs, bool specific )
{
    // get the address of _pthread_set_self()
    // In OS X 10.4 this function gained an underscore
    pArgs->setSelfFn = ( ___pthread_set_self_fn ) DPFindFunctionAddress( "___pthread_set_self",
        "libSystem.B.dylib" );
    if ( pArgs->setSelfFn == NULL )
    {
        pArgs->setSelfFn = ( ___pthread_set_self_fn ) DPFindFunctionAddress( "__pthread_set_self",
            "libSystem.B.dylib" );
        if ( pArgs->setSelfFn == NULL )
            LogEmergency( "ERROR: Address of __pthread_set_self is NULL !" );
    }

    // get the address of _pthread_create()
    pArgs->createFakeFn = ( ___pthread_create_int_fn ) _pthread_create;
    if ( pArgs->createFakeFn == NULL )
        LogEmergency( "ERROR: Address of _pthread_create is NULL !" );

    // get the address of pthread_create( )
    pArgs->createPthreadFn = ( __pthread_create_fn ) pthread_create;
    if ( pArgs->createPthreadFn == NULL )
        LogEmergency( "ERROR: Address of pthread_create is NULL !" );

    // get the address of NSAddImage() or dlopen()
    if ( dlopen == 0 )
    {
        pArgs->addImageFn = ( __loadimage_fn_ptr ) NSAddImage;
        if ( pArgs->addImageFn == NULL )
            LogEmergency( "ERROR: Address of NSAddImage is NULL !" );
    }
    else
    {
        // we *know* it's not zero now
        pArgs->addImageFn = ( __loadimage_fn_ptr ) dlopen;
    }

    // get the address of dlsym if possible
    if ( dlsym != 0 )
    {
        pArgs->lookupFn = ( __lookup_fn_ptr ) dlsym;
    }
    else
    {
        pArgs->lookupSymFn = ( __lookup_sym_fn_ptr ) NSLookupAndBindSymbol;
        if ( pArgs->lookupSymFn == NULL )
            LogEmergency( "ERROR: Address of NSLookupAndBindSymbol is NULL !" );

        pArgs->symAddrFn = ( __sym_addr_fn_ptr ) NSAddressOfSymbol;
        if ( pArgs->symAddrFn == NULL )
            LogEmergency( "ERROR: Address of NSAddressOfSymbol is NULL !" );
    }

    // get the address of thread_terminate()
    pArgs->terminateFn = ( __thr_term_fn ) thread_terminate;
    if ( pArgs->terminateFn == NULL )
        LogEmergency( "ERROR: Address of thread_terminate is NULL !" );

    // & we want to hand in the thread's descriptor, too
    pArgs->selfFn = ( __thr_me_fn ) mach_thread_self;
    if ( pArgs->selfFn == NULL )
        LogEmergency( "ERROR: Address of mach_thread_self is NULL !" );
}

#pragma mark -

Injector::Injector( pid_t target_pid, const char *pPatchToLoad ) :
    taskPort(MACH_PORT_NULL), vmaddr_slide(0), pPathToPatch(NULL)
{
    if ( task_for_pid( mach_task_self( ), target_pid, &taskPort ) == KERN_SUCCESS )
    {
        if ( pPatchToLoad != NULL )
            pPathToPatch = strdup( pPatchToLoad );
    }
}
Injector::Injector( task_t target_task, const char *pPatchToLoad ) :
    taskPort(target_task), vmaddr_slide(0), pPathToPatch(NULL)
{
    if ( pPatchToLoad != NULL )
        pPathToPatch = strdup( pPatchToLoad );
}
Injector::~Injector( )
{
    if ( pPathToPatch != NULL )
        ::free( pPathToPatch );
}
void Injector::Inject( )
{
    if ( taskPort != MACH_PORT_NULL )
    {
        // can't patch Rosetta apps from here, should be using hte
        // rosetta injector !
        if ( IsRosettaTask( taskPort ) )
            LogError( "Standard Injector, rosetta target; not patching." );
        else
            StartThreadInTarget( );
    }
}
bool Injector::SuspendTarget( )
{
#if SUSPEND_TARGETS
    bool result = false;
    if ( task_suspend( taskPort ) == KERN_SUCCESS )
        result = true;
#else
    return ( true );
#endif
}
void Injector::ResumeTarget( )
{
#if SUSPEND_TARGETS
    if ( task_resume( taskPort ) != KERN_SUCCESS )
        LogError( "Failed to resume target task '%#x'", taskPort );
#endif
}
void Injector::StartThreadInTarget( )
{
    kern_return_t kern_res = KERN_SUCCESS;
    size_t stacksize = 0;
    thread_act_t kernel_thread;
    pthread_attr_t attrs;
    vm_address_t stack;
    vm_address_t target_sectaddr, start_fn_address, pthread_start_addr, args_addr;
    newthread_args_t args;
    bool resume = true;
    bool specific = (pPathToPatch != NULL);

    bzero( &args, sizeof( newthread_args_t ) );

    // find out what the pthread library uses as a stack size, and use that ourselves
    pthread_attr_init( &attrs );
    pthread_attr_getstacksize( &attrs, &stacksize );

    // wrap allocations & copies with task suspension
    if ( !SuspendTarget( ) )
    {
        LogError( "Failed to suspend target task - NOT PATCHING !" );
        return;
    }

    // allocate a stack for the thread
    if ( !AllocateStack( stacksize, &stack ) )
    {
        LogError( "PatchLoader : Can't allocate stack !" );
    }
    else
    {
        GetFunctionPointers( &args, specific );

        // pass in the address of the stack base
        args.stack_base = ( void * ) stack;

        if ( args.lookupFn != NULL )
        {
            // copy in dlsym-type symbol entries (C function names)
            if ( specific )
                strncpy( args.fn_name, &specific_name[1], 32 );
            else
                strncpy( args.fn_name, &start_name[1], 32 );
        }
        else
        {
            // copy in NSLookup-type symbol entries (asm labels)
            if ( specific )
                strncpy( args.fn_name, specific_name, 32 );
            else
                strncpy( args.fn_name, start_name, 32 );
        }

        // add the library name in here
        strncpy( args.lib_name, lib_name, PATH_MAX );

        // if necessary, add the path to a specific patch to load...
        if ( specific )
            strncpy( args.patch_name, pPathToPatch, PATH_MAX );

        // setup pthread & attrs in advance
        // I'm setting up everything except the kernel thread, just out
        // of paranoia, really.
        pthread_attr_init( &args.fakeAttrs );
        (void) _pthread_create( &args.fakeThread, &args.fakeAttrs,
                                (void *) stack, MACH_PORT_NULL );

        if ( ( target_sectaddr = InjectCode( &args, &args_addr ) ) == 0 )
        {
            LogError( "PatchLoader : Can't copy section into target process !" );
        }
        else
        {
            // work out what the address is in the target task...
            start_fn_address = target_sectaddr + NewThreadFnOffset;

            // compute the address of our pthread starter function
            pthread_start_addr = target_sectaddr;

            // create the target thread
            kern_res = thread_create( taskPort, &kernel_thread );
            if ( kern_res != KERN_SUCCESS )
            {
                LogError( "PatchLoader : Failed to create new thread - %#x (%s) !",
                          kern_res, mach_error_string(kern_res) );
            }
            else
            {
                // set thread state appropriately
                SetupTargetThread( kernel_thread, start_fn_address, stack,
                                   pthread_start_addr, args_addr );

                // resume task here then wait a bit for pages to reach
                // the processor caches, etc.
                // I've seen some targets crash at the first
                // instruction because the virtual memory manager
                // doesn't seem to have it updated yet; this causes an
                // illegal instruction exception, which is a pain,
                // because with debuggers, anything like that, I can
                // only see *valid* instructions at the address. This
                // pause seems to help. Meh. If anyone can give me a
                // better solution, I'd gladly accept it.
                ResumeTarget( );
                resume = false;

                usleep( 10000 );     // wait for 1/10th of a second

                kern_res = thread_resume( kernel_thread );
                if ( kern_res != KERN_SUCCESS )
                {
                    LogError( "PatchLoader : thread_resume(): %d (%s)",
                              kern_res, mach_error_string(kern_res) );

                    // destroy the thread
                    (void) thread_terminate( kernel_thread );
                }
            }
        }
    }

    if ( resume )
    {
        // looks like something went wrong and we didn't create thread
        // & resume like we should have done
        ResumeTarget( );
    }
}
bool Injector::AllocateStack( size_t stacksize, vm_address_t *pStack )
{
/*
    Notes on this function:

    It attempts to allocate memory around the normal place on PPC, which is at 0xF0000000. As
    far as I know, the stack on PPC/Mach-O grows downwards.
    It allocates the complete stack, plus one extra 'guard' page.
    First of all, it will attempt to use vm_map to just create a memory-mapped region of zeroes,
    rather than explicitly creating/allocating memory. If that fails, however, it will fall
    back on a normal vm_allocate() call.

    If the stack grows upwards, it will turn off all access to the highest page of the mapped region.
    If the stack grows downwards, it will turn off access to the lowest page of the mapped region,
    and then return the address of the first byte *after* the whole thing (grows downwards into
    allocated memory).
*/
    kern_return_t kr;

    // try to just map in a zero-filled section of memory first, then try to explicitly allocate
    //  if that fails...
    *pStack = STACK_HINT;
    kr = vm_map( taskPort, pStack, stacksize + vm_page_size, vm_page_size - 1,
                 VM_MAKE_TAG( VM_MEMORY_STACK ) | VM_FLAGS_ANYWHERE, MEMORY_OBJECT_NULL,
                 0, FALSE, VM_PROT_DEFAULT, VM_PROT_ALL, VM_INHERIT_DEFAULT );

    if ( kr != KERN_SUCCESS )
    {
        kr = vm_allocate( taskPort, pStack, stacksize + vm_page_size,
                          VM_MAKE_TAG( VM_MEMORY_STACK ) | VM_FLAGS_ANYWHERE );
    }

    if ( kr != KERN_SUCCESS )
    {
        LogError( "Stack allocation via vm_allocate() failed - %d (%s)", 
                  kr, mach_error_string( kr ) );
        return ( false );
    }

    // here we set no-access permissions on the guard page for the stack
#ifdef STACK_GROWS_UP
    // guard page is the page one higher than the stack
    // stack base is at lowest address
    // deny access to highest page of allocate range
    kr = vm_protect( taskPort, *pStack + stacksize, vm_page_size, FALSE, VM_PROT_NONE );
#else
    // guard page is as lowest address
    // stack base is at highest address
    // deny access to lowest page of allocated range
    kr = vm_protect( taskPort, *pStack, vm_page_size, FALSE, VM_PROT_NONE );
    // redefine stack ptr
    *pStack += stacksize + vm_page_size;
#endif
    return ( true );
}
void Injector::SetupTargetThread( thread_act_t thread, vm_address_t routine_addr,
                                  vm_address_t vsp, vm_address_t pthread_start_addr,
                                  vm_address_t args_addr )
{
    kern_return_t kr;
    unsigned int count;
#if defined(__ppc__)
    struct ppc_thread_state state;
    struct ppc_thread_state *ts = &state;

    count = PPC_THREAD_STATE_COUNT;
    kr = thread_get_state( thread, PPC_THREAD_STATE, ( thread_state_t ) &state, &count );

    // current instruction/frame address
    ts->srr0 = ( int ) routine_addr;
    // current stack pointer
    ts->r1 = vsp - C_ARGSAVE_LEN - C_RED_ZONE;

    // arguments start at register 3
    ts->r3 = pthread_start_addr;
    ts->r4 = args_addr;

    thread_set_state( thread, PPC_THREAD_STATE, ( thread_state_t ) &state, PPC_THREAD_STATE_COUNT );
#elif defined(__i386__)
    i386_thread_state_t state = {0}, *ts = &state;
    unsigned int localstack[5];

    count = i386_THREAD_STATE_COUNT;
    kr = thread_get_state( thread, i386_THREAD_STATE, (thread_state_t) &state, &count );

    // instruction pointer -- rosetta-specific changes could go here
    ts->eip = (unsigned int) routine_addr;

    // Theory section:
    // Arguments go on stack, prior to current stack frame address.
    // Stack should stay 16-byte aligned. We're adding 12 bytes, and the
    // starting value is 16-byte aligned already, so we need to pad it
    // by 4 bytes -- this is a one-time operation, so we'll just push
    // zero:
    // <-------------------------------------- 16-byte alignment
    // *--sp = 0; <--------------------------- 4 bytes
    // *--sp = args_addr; <------------------- 8 bytes
    // *--sp = pthread_start_addr; <---------- 12 bytes
    // *--sp = 0; <--------------------------- 16 bytes
    // <-------------------------------------- 16-byte alignment
    // Note that the last item on the stack is the 'dummy' return
    // address for the start function

    // arguments
    localstack[0] = 0;
    localstack[1] = (unsigned int) pthread_start_addr;
    localstack[2] = (unsigned int) args_addr;
    localstack[3] = 0;
    localstack[4] = 0;

    (void) vm_write( taskPort, (vsp - 20), (vm_address_t) localstack, sizeof(localstack) );

    // set stack pointer
    ts->esp = (int) vsp - 20;

    (void) thread_set_state( thread, i386_THREAD_STATE, (thread_state_t) &state,
                             i386_THREAD_STATE_COUNT );
#else
# error unsupported architecture
#endif
}
vm_address_t Injector::InjectCode( newthread_args_t *pArgs, vm_address_t *pArgsAddr )
{
    vm_address_t result = 0;
    unsigned int code_size = 0;
    vm_offset_t code = 0;
    vm_address_t target = 0;
    kern_return_t kr = KERN_SUCCESS;
    size_t argsize = sizeof(newthread_args_t);
    vm_size_t calignsize = 0, aalignsize = 0, totalsize = 0;

    code_size = sizeof(insertion_code);
    code = (vm_offset_t) insertion_code;

    // make sure sizes & addresses are dword-aligned
    aalignsize = ((argsize + 3) & ~3);
    //calignsize = ((code_size + 3) & ~3);
    // code gets a full page to itself
    (void) host_page_size( mach_host_self( ), &calignsize );

    totalsize = calignsize + aalignsize;

    // allocate enough space to hold everything
    kr = vm_allocate( taskPort, &target, (vm_size_t) totalsize, TRUE );
    if ( kr == KERN_SUCCESS )
    {
        // write code block into start of allocated region
        kr = vm_write( taskPort, target, code, (mach_msg_type_number_t) code_size );
        if ( kr == KERN_SUCCESS )
        {
            // write argument block
            // code block size is already dword-aligned, so args will be
            //  copied to a dword-aligned address
            vm_address_t argaddr = target + (vm_address_t) calignsize;
            kr = vm_write( taskPort, argaddr, (vm_offset_t) pArgs,
                           (mach_msg_type_number_t) argsize );
            if ( kr == KERN_SUCCESS )
            {
                // ensure permissions allow execution
                (void) vm_protect( taskPort, target, (vm_size_t) totalsize, TRUE, VM_PROT_ALL );
                (void) vm_protect( taskPort, target, (vm_size_t) totalsize, FALSE, VM_PROT_ALL );
                (void) vm_inherit( taskPort, target, (vm_size_t) totalsize,
                                   VM_INHERIT_DEFAULT /*VM_INHERIT_NONE*/ );

                (void) vm_msync( taskPort, target, (vm_size_t) totalsize,
                                 VM_SYNC_INVALIDATE | VM_SYNC_SYNCHRONOUS );
#if defined(__ppc__)
                // this needs an address on Intel processors, which
                // means it's not much use outside the target process
                DPCodeSync( NULL );
#endif
                result = target;
                *pArgsAddr = argaddr;
            }
            else
            {
                LogError( "Couldn't write arguments into target process: %d (%s)",
                          kr, mach_error_string( kr ) );
                (void) vm_deallocate( taskPort, target, (vm_size_t) totalsize );
            }
        }
        else
        {
            LogError( "Couldn't write code into target process: %d (%s)",
                      kr, mach_error_string( kr ) );
            (void) vm_deallocate( taskPort, target, (vm_size_t) totalsize );
        }
    }
    else
    {
        LogError( "Unable to allocate space for code & arguments: %d (%s)",
                  kr, mach_error_string( kr ) );
    }

    return ( result );
}

#pragma mark -

patch_entry_list::patch_entry_list( ) :
    entries(NULL), count(0), allocated(8)
{
    entries = (patch_entry_t *) malloc( allocated * sizeof(patch_entry_t) );
}
patch_entry_list::~patch_entry_list( )
{
    if ( entries != NULL )
        free( entries );
}
void patch_entry_list::AddEntry( patch_entry_t obj )
{
    if ( count == allocated )
    {
        allocated += 8;
        entries = (patch_entry_t *) realloc( entries,
                                             allocated * sizeof(patch_entry_t) );
    }

    // in here, we perform any necessasry byte-swapping

    // keep address in host byte order -- because inserted thread will
    // run native
    entries[count].fn_addr = obj.fn_addr;

    // instruction is non-native though, so must be byte-swapped
    entries[count].ba_instr = OSSwapHostToBigInt32(obj.ba_instr);

    count++;
}

#pragma mark -

RosettaInjector::RosettaInjector( pid_t target_pid, const char *pPatchToLoad ) :
    Injector(target_pid, pPatchToLoad), patchEntries( ),
    kernel_thread(0), code_addr(0), currentBundle(NULL)
{
}
RosettaInjector::RosettaInjector( task_t target_task, const char *pPatchToLoad ) :
    Injector(target_task, pPatchToLoad), patchEntries( ),
    kernel_thread(0), code_addr(0), currentBundle(NULL)
{
}
void RosettaInjector::Inject( )
{
    if ( taskPort != MACH_PORT_NULL )
    {
        // rosetta injector never suspends; I can't remember why that
        // is, now

        // clear the rosetta globals
        _rosetta_clear_data( );
        // load data from patch bundles
        LoadPatches( );

        // only go further if the target is to actually have patched
        // functions
        if ( patchEntries.EntryCount( ) > 0 )
        {
            // store data tables stub code
            if ( _rosetta_commit_data( taskPort ) != 0 )
                StartThreadInTarget( );
        }

        // if we started a thread going, we should wait for it to finish
        if ( kernel_thread != 0 )
            EndPatchThread( );
    }
}
void RosettaInjector::StartThreadInTarget( )
{
    vm_address_t args_addr = 0;
    pthread_attr_t attrs;
    size_t stacksize;
    vm_address_t stack;

    pthread_attr_init( &attrs );
    pthread_attr_getstacksize( &attrs, &stacksize );

    // allocate stack space for the new thread
    if ( AllocateStack( stacksize, &stack ) )
    {
        // install code to link up these fellers
        code_addr = InjectRosettaCode( patchEntries.EntryList( ),
                                       patchEntries.EntryCount( ),
                                       &args_addr );

        if ( code_addr != 0 )
        {
            kern_return_t kr = thread_create( taskPort, &kernel_thread );

            if ( kr == KERN_SUCCESS )
            {
                // for Rosetta, we insert some very simple code which
                // doesn't muck about
                SetupRosettaThread( kernel_thread, code_addr, stack, args_addr,
                                    patchEntries.EntryCount( ) );

                kr = thread_resume( kernel_thread );
                if ( kr != KERN_SUCCESS )
                {
                    LogError( "thread_resume failed -- %d (%s)", kr,
                              mach_error_string( kr ) );

                    // delete & zero it so we don't wait for it later
                    (void) thread_terminate( kernel_thread );
                    kernel_thread = 0;
                }
            }
            else
            {
                LogError( "thread_create failed -- %d (%s)", kr,
                          mach_error_string( kr ) );
            }
        }
    }
    else
    {
        LogError( "Failed to allocate stack !" );
    }
}
void RosettaInjector::SetupRosettaThread( thread_act_t thread,
                                          vm_address_t codeblock_addr,
                                          vm_address_t stack_ptr,
                                          vm_address_t args_addr,
                                          natural_t args_count )
{
#if defined(__i386__)
    kern_return_t kr;
    unsigned int count;
    i386_thread_state_t state = {0}, *ts = &state;
    unsigned int localstack[5];

    count = i386_THREAD_STATE_COUNT;
    kr = thread_get_state( thread, i386_THREAD_STATE, (thread_state_t) &state, &count );

    // instruction pointer -- rosetta-specific changes could go here
    ts->eip = (unsigned int) (codeblock_addr + _rosetta_code_offset);

    localstack[0] = 0;                              // return address
    localstack[1] = (unsigned int) args_addr;       // arg 1
    localstack[2] = (unsigned int) args_count;      // arg 2
    localstack[3] = (unsigned int) codeblock_addr;  // arg 3
    localstack[4] = 0;                              // pad

    (void) vm_write( taskPort, (stack_ptr - 20), (vm_address_t) localstack,
                     sizeof(localstack) );

    // set stack pointer
    ts->esp = (int) stack_ptr - 20;

    (void) thread_set_state( thread, i386_THREAD_STATE, (thread_state_t) &state,
                             i386_THREAD_STATE_COUNT );
#else
# warning RosettaInjector not implemented for this architecture
#endif
}
vm_address_t RosettaInjector::InjectRosettaCode( patch_entry_t *pArgs,
    size_t count, vm_address_t *pArgsAddr )
{
    vm_address_t result = 0;
    unsigned int code_size = 0;
    vm_offset_t code = 0;
    vm_address_t target = 0;
    kern_return_t kr = KERN_SUCCESS;
    size_t argsize = sizeof(patch_entry_t) * count;
    vm_size_t calignsize = 0, aalignsize = 0, totalsize = 0;

    code_size = sizeof(_rosetta_code);
    code = (vm_offset_t) _rosetta_code;

    // make sure sizes & addresses are dword-aligned
    aalignsize = ((argsize + 3) & ~3);
    calignsize = ((code_size + 3) & ~3);

    totalsize = calignsize + aalignsize;

    // allocate enough space to hold everything
    kr = vm_allocate( taskPort, &target, (vm_size_t) totalsize, TRUE );
    if ( kr == KERN_SUCCESS )
    {
        // write code block into start of allocated region
        kr = vm_write( taskPort, target, code, (mach_msg_type_number_t) code_size );
        if ( kr == KERN_SUCCESS )
        {
            // write argument block
            // code block size is already dword-aligned, so args will be
            //  copied to a dword-aligned address
            vm_address_t argaddr = target + (vm_address_t) calignsize;
            kr = vm_write( taskPort, argaddr, (vm_offset_t) pArgs,
                           (mach_msg_type_number_t) argsize );
            if ( kr == KERN_SUCCESS )
            {
                // ensure permissions allow execution
                (void) vm_protect( taskPort, target, (vm_size_t) totalsize, TRUE,
                                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE );
                (void) vm_protect( taskPort, target, (vm_size_t) totalsize, FALSE,
                                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE );
                (void) vm_inherit( taskPort, target, (vm_size_t) totalsize, VM_INHERIT_NONE );

                result = target;
                *pArgsAddr = argaddr;
            }
            else
            {
                LogError( "Couldn't write arguments into target process: %d (%s)",
                          kr, mach_error_string( kr ) );
                (void) vm_deallocate( taskPort, target, (vm_size_t) totalsize );
            }
        }
        else
        {
            LogError( "Couldn't write code into target process: %d (%s)",
                      kr, mach_error_string( kr ) );
            (void) vm_deallocate( taskPort, target, (vm_size_t) totalsize );
        }
    }
    else
    {
        LogError( "Unable to allocate space for code & arguments: %d (%s)",
                  kr, mach_error_string( kr ) );
    }

    return ( result );
}
void RosettaInjector::EndPatchThread( )
{
    // no thread_terminate() function available to our
    // inserted thread, so we have to stop it from here
    // it'll turn the first byte of code to 0xFF when
    // it's done.

    bool done = false;
    kern_return_t kr = KERN_SUCCESS;

    do
    {
        vm_offset_t readData = 0;
        unsigned char byte = 0;
        if ( vm_read_overwrite( taskPort, code_addr, 1,
                                (vm_address_t) &byte,
                                &readData ) == KERN_SUCCESS )
        {
            // see what we found
            if ( byte == 0xFF )
                done = true;
            else
                usleep( 1000 );    // wait 1 millisecond
        }

    } while ( !done );

    // kill the patching thread now
    kr = thread_terminate( kernel_thread );
    if ( kr != KERN_SUCCESS )
    {
        LogError( "thread_terminate failed -- %d (%s)", kr,
                  mach_error_string( kr ) );
    }
}

#pragma mark -

void RosettaInjector::LoadPatches( )
{
    if ( pPathToPatch != NULL )
    {
        CFStringRef str;
        CFURLRef url;

        str = CFStringCreateWithCString( NULL, pPathToPatch, kCFStringEncodingUTF8 );
        url = CFURLCreateWithFileSystemPath( NULL, str, kCFURLPOSIXPathStyle, TRUE );

        currentBundle = CFBundleCreate( NULL, url );

        if ( currentBundle != NULL )
        {
            HandleCurrentBundle( );
            CFRelease( currentBundle );
        }

        CFRelease( url );
        CFRelease( str );
    }
    else
    {
        CFArrayRef bundles = list_patch_bundles( );

        if ( bundles != NULL )
        {
            CFIndex i, count = CFArrayGetCount( bundles );

            for ( i = 0; i < count; i++ )
            {
                currentBundle = (CFBundleRef) CFArrayGetValueAtIndex( bundles, i );

                if ( currentBundle != NULL )
                    HandleCurrentBundle( );
            }

            CFRelease( bundles );
        }
    }
}
void RosettaInjector::HandleCurrentBundle( )
{
    if ( CFBundleLoadExecutable( currentBundle ) )
    {
        int willPatch = 0;
        __bundle_will_patch_fn willPatchFn = 0;
        __get_patch_details_fn getDetailsFn = 0;
        CFStringRef willPatchFuncName = CFSTR(kWillPatchFunctionName);
        CFStringRef getDetailsFuncName = CFSTR(kGetPatchDetailsFunctionName);

        willPatchFn = (__bundle_will_patch_fn) CFBundleGetFunctionPointerForName( currentBundle,
            willPatchFuncName );
        getDetailsFn = (__get_patch_details_fn) CFBundleGetFunctionPointerForName( currentBundle,
            getDetailsFuncName );

        DEBUGLOG( "Loaded bundle executable, fn addrs are %#x & %#x\n",
                  (unsigned int) willPatchFn, (unsigned int) getDetailsFn );

        if ( (willPatchFn != 0) && (getDetailsFn != 0) )
        {
            pid_t appPid = 0;
            const char * appName = NULL;
            (void) pid_for_task( taskPort, &appPid );
            appName = NameForProcessID( appPid );

            if ( appName != NULL )
            {
                willPatch = willPatchFn( currentBundle, appPid, appName );

                if ( willPatch != 0 )
                {
                    // get actual patches -- provide a callback to handle
                    // patching & storing relevant data
                    getDetailsFn( RosettaInjector::_handle_patch_details,
                                  kInsertionArchPPC, (void *) this );

                    // at this point, patch code (using stub functions)
                    // and associated stub-binding data is all
                    // installed
                    // the patch entry list has been updated with the
                    // relevant details
                }

                // this was created using strdup()
                free( (void *) appName );
            }
            else
            {
                LogError( "Failed to get target application's name !" );
            }
        }
        else
        {
            LogError( "Couldn't find bundle startup functions !\n" );
        }

        CFBundleUnloadExecutable( currentBundle );
    }
    else
    {
        LogError( "Failed to load bundle !" );
    }
}

#pragma mark -

void RosettaInjector::_handle_patch_details( void * targetAddr,
                                             const char * patchFnName,
                                             void * info )
{
    // try and be relatively safe in the event we get given some random
    // value...
    RosettaInjector *pObj = reinterpret_cast<RosettaInjector *>(info);
    if ( pObj != NULL )
    {
        pObj->InstallPatchStub( targetAddr, patchFnName );
    }
    else
    {
        LogError( "Handed invalid info ptr '%#x' !", (unsigned) info );
    }
}
void RosettaInjector::InstallPatchStub( void * targetAddr, const char * patchFnName )
{
    patch_entry_t entry = { (unsigned) targetAddr, 0 };
    unsigned patchOffset = 0;
    UInt8 urlStr[ PATH_MAX ];

    // get the URL of the current bundle as a C-string
    CFURLRef url = CFBundleCopyExecutableURL( currentBundle );
    (void) CFURLGetFileSystemRepresentation( url, TRUE, urlStr, PATH_MAX );

    // get the address of this function for PPC
    // value returned is actually an offset, relative to vm_addr of
    // zero
    if ( patchFnName[0] != '_' )
    {
        // use a leading underscore to locate the symbol
        char symbol[ 256 ];
        symbol[0] = '_';
        symbol[1] = '\0';
        strncat( symbol, patchFnName, 254 );
        symbol[255] = '\0';

        patchOffset = (unsigned) DPFindFunctionForArchitecture( symbol,
            (const char *) urlStr, kInsertionArchPPC );
    }
    else
    {
        patchOffset = (unsigned) DPFindFunctionForArchitecture( patchFnName,
            (const char *) urlStr, kInsertionArchPPC );
    }

    entry.ba_instr = _rosetta_install_patch_stub( targetAddr, patchOffset, urlStr, taskPort );

    if ( entry.ba_instr != 0 )
    {
        patchEntries.AddEntry( entry );
    }
    else
    {
        LogError( "Failed to create patch stub for patch function '%s'",
                  patchFnName );
    }
}
