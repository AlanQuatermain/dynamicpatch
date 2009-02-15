/*
 *  Injector.h
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

#ifndef __DP_INJECTOR_H__
#define __DP_INJECTOR_H__

#include <CoreFoundation/CoreFoundation.h>

#include <mach/mach.h>
#include <mach/task.h>
#include <mach/machine/vm_types.h>
#include <mach/thread_act.h>

#include <mach-o/loader.h>

#include <stdlib.h>
#include <unistd.h>

#include "newthread.h"

// This uses C++ for historical reasons; originally the injection code
// was part of a seperate project, a single application written using
// mostly C++. It was moved here later on, when injection was needed
// from other applications.

class Injector
{
    public:
        Injector( pid_t target_pid, const char *pPatchToLoad = NULL );
        Injector( task_t target_task, const char *pPatchToLoad = NULL );
        virtual ~Injector( );

        // Call this to have the target load your patch bundle(s)
        virtual void Inject( );

    protected:
        bool SuspendTarget( );
        void ResumeTarget( );

        virtual void StartThreadInTarget( );

        // this will allocate a stack, complete with red zone &
        // guard page, returning the address in pStack. It returns true
        // on success, false on failure.
        bool AllocateStack( size_t stacksize, vm_address_t *pStack );

        // this is a potential candidate for a vararg routine, taking n
        // arguments to the injected code. So far I've not needed to
        // think about types that won't fit into registers, and I don't
        // need to worry about using the stack (after more than 7
        // args), so I'm leaving well enough alone right now.
        virtual void SetupTargetThread( thread_act_t thread, vm_address_t routine_addr,
                                        vm_address_t vsp, vm_address_t pthread_start_addr,
                                        vm_address_t argblock_addr );

        // this copies the injection code into the target process, and
        // returns the address of the code block. It puts the address
        // of the argument block into pArgsAddr.
        vm_address_t InjectCode( newthread_args_t *pArgs, vm_address_t *pArgsAddr );

    protected:
        task_t taskPort;
        int vmaddr_slide;
        char *pPathToPatch;

};

// this holds everything used by the injected routine in a rosetta
// application: what to write, and where. Everything else has been done
// already by the time this is used.
typedef struct _patch_entry_struct
{
    vm_address_t    fn_addr;
    natural_t       ba_instr;

} patch_entry_t;

// helper class: builds a malloc'd list of patch_entry_t objects, ready
// to copy into the target process
class patch_entry_list
{
    public:
        patch_entry_list( );
        ~patch_entry_list( );

        void AddEntry( patch_entry_t obj );
        patch_entry_t * EntryList( ) { return ( entries ); }
        size_t EntryCount( ) { return ( count ); }

    protected:
        patch_entry_t * entries;
        size_t count;
        size_t allocated;

};

class RosettaInjector : public Injector
{
    public:
        RosettaInjector( pid_t target_pid, const char *pPatchToLoad = NULL );
        RosettaInjector( task_t target_task, const char *pPatchToLoad = NULL );
        virtual ~RosettaInjector( ) { }

        virtual void Inject( );

    protected:

        virtual void StartThreadInTarget( );

        // Different parameter types, so it uses a different function
        // name, to avoid confusion.
        virtual void SetupRosettaThread( thread_act_t thread, vm_address_t routine_addr,
                                         vm_address_t stack_ptr, vm_address_t args_addr,
                                         natural_t args_count );
        // this one takes different parameters, so gets its own
        // function too
        vm_address_t InjectRosettaCode( patch_entry_t *pArgs, size_t count,
                                        vm_address_t *pArgsAddr );

        // since the address of thread_terminate() in the Rosetta IA-32
        // execution environment isn't an external symbol, we need to
        // stop the thread another way; this is that way.
        void EndPatchThread( );

        // this will load either the specified bundle or get a list of
        // all applicable ones in the system, and will hand them to
        // HandleCurrentBundle. It uses a member variable to hold the
        // bundle because there are callback functions which need to
        // access it, and this is the simplest way to make that happen
        void LoadPatches( );
        void HandleCurrentBundle( );

        // this is a callback, whose address gets given to certain
        // functions within a patch bundle. The patch bundle uses this
        // callback to register which functions it would like patched
        static void _handle_patch_details( void * targetAddr,
                                           const char * patchFnName,
                                           void * info );
        // this is called by the callback to create a patch stub and an
        // entry in the patch entry list.
        void InstallPatchStub( void * targetAddr, const char * patchFnName );

        patch_entry_list patchEntries;
        thread_act_t kernel_thread;
        vm_address_t code_addr;
        CFBundleRef currentBundle;

};

#endif  /* __DP_INJECTOR_H__ */
