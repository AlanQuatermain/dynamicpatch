/*
 *  rosetta_patch.h
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

#ifndef __DP_ROSETTA_PATCH_H__
#define __DP_ROSETTA_PATCH_H__

#include <sys/cdefs.h>
#include <mach/task.h>

/*!
 @header Rosetta Patching/Injection
 @discussion This file declares the interface used by the Rosetta
         injector/patch code. The idea when using this is that you
         should call
         @link _rosetta_install_patch_stub _rosetta_install_patch_stub @/link
         for each patch you want to install. This will then build up
         the data tables necessary for the patches themselves to be
         loaded later on. The caller can then use
         @link _rosetta_commit_data _rosetta_commit_data @/link to
         actually copy this into the target task. It's up to the
         injector to actually patch the target function itself.
 @copyright 2004-2006 Jim Dovey. Some Rights Reserved.
 @author Jim Dovey
 */

__BEGIN_DECLS

/*!
 @function _rosetta_clear_data
 @abstract Clears the contents of the local data tables.
 @discussion Prior to starting the injection/patch process, this
         function should be called to make sure no data from earlier
         invocations remains in the locally-allocated data tables.
 */
void _rosetta_clear_data( void );

/*!
 @function _rosetta_commit_data
 @abstract Copy the completed data tables into the target task.
 @discussion This function will write the completed data tables into
         the target task, making the last few modifications (updating
         counts, addresses, etc.) in the process. Until this is called,
         the target task remains more-or-less unmolested, save for the
         allocation of some jump tables and making the target
         function's vm page copy-on-write. No data is actually changed
         in the target task, however, until this function is called.
 @param taskPort The Mach port for the target task object.
 @result Returns 1 if all data was copied across, 0 otherwise.
 */
int _rosetta_commit_data( task_t taskPort );

/*!
 @function _rosetta_install_patch_stub
 @abstract Build a patch entry for a function in a given task.
 @discussion This function causes most of the work to happen. It will
         ensure that the page containing <code>targetAddr</code> in
         the target task is writable, and it will ensure that the
         target task can allocate memory to hold the jump tables. It
         will then build entries in the patch list, containing the
         given information, which will then be used to load and link
         the patch function when the stub function is called in the
         target task. For more information, see the accompanying
         documentation.
 @param targetAddr The address of the function to patch, in the target
         process.
 @param patchOffset The offset within the patch bundle to the patch
         function itself.
 @param urlStr A URL (used to create a CFURLRef) indicating the
         location of the patch bundle.
 @param taskPort The Mach port of the target task.
 @result Returns the PowerPC branch absolute instruction, which the
         caller will need to install in the target function.
 */
unsigned int _rosetta_install_patch_stub( void * targetAddr, unsigned patchOffset,
                                          const UInt8 * urlStr, task_t taskPort );

__END_DECLS

#endif  /* __DP_ROSETTA_PATCH_H__ */
