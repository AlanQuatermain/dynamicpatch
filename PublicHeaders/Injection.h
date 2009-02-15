/*
 *  Injection.h
 *  DynamicPatch
 *
 *  Created by jim on 31/3/2006.
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

#ifndef __DP_INJECTION_H__
#define __DP_INJECTION_H__

#include "DPAPI.h"

#include <CoreFoundation/CFBundle.h>
#include <unistd.h> // for pid_t

/*!
 @header Injection
 @author Jim Dovey
 @copyright 2003-2006 Jim Dovey. Some Rights Reserved.
 @ignore DP_API
*/

/*!
 @defined kStartFunctionName
 @abstract The name of the bundle startup function. 
 @discussion This is a hard-coded value at present, but you could
         always have the name read from the bundle's Info.plist
         if you wanted. That would need a change to the underlying
         code.
 */
#define kStartFunctionName              "PatchMain"

/*!
 @defined kWillPatchFunctionName
 @abstract The name of the Rosetta 'Will Patch Application' function.
 @discussion As with <code>kStartFunctionName</code>, this is currently
         hard-coded, but can be read from a bundle's info.plist with a
         few changes to the code.
 */
#define kWillPatchFunctionName          "WillPatchApplication"

/*!
 @defined kGetPatchDetailsFunctionName
 @abstract The name of the Rosetta 'Get Patch Details' function.
 @discussion As with <code>kStartFunctionName</code>, this is currently
         hard-coded, but can be read from a bundle's info.plist with a
         few changes to the code.
 */
#define kGetPatchDetailsFunctionName    "GetPatchDetails"

/*!
 @defined kLinkPatchBundleSymbolName
 @abstract The assembler-style symbol name of the 'Link Patches' function.
 @discussion This is an actual assembler symbol name, because unlike
         the others, passed to <code>CFBundleGetAddressOfSymbol()</code>,
         this one is passed to <code>NSLookupSymbolInModule</code>,
         which requires the leading underscore to be included.
 */
#define kLinkPatchBundleSymbolName      "_LinkPatches"

/*!
 @typedef __bundle_start_fn
 @abstract Function type for native patch bundle entry point.
 @discussion Upon being loaded by this API, the bundle is expected to
         export a function whose name matches the value of
         @link kStartFunctionName kStartFunctionName @/link. It
         should conform to this prototype.
 @param b The patch bundle's object.
 @result Return 1 to stay in memory, 0 to be unloaded.
 */
typedef int (*__bundle_start_fn)(CFBundleRef b);

/*!
 @typedef __bundle_will_patch_fn
 @abstract Prototype for Rosetta patch bundles.
 @discussion The patching system expects to find a function whose name
         matches the value of
         @link kWillPatchFunctionName kWillPatchFunctionName @/link,
         which should conform to this prototype.
 @param b The bundle object for the patch bundle.
 @param p The ID of the target process.
 @param a The name of the target process.
 @result Return 1 if you will patch the target, 0 otherwise.
 */
typedef int (*__bundle_will_patch_fn)(CFBundleRef b, pid_t p, const char * a);

/*!
 @typedef __patch_details_cb
 @abstract A callback, used by a Rosetta patch bundle.
 @discussion The patch bundle will be given a pointer to a function
         with this prototype. It should call this function with the
         details of any functions it wishes to patch in the target
         process.
 @param addr The arch-specific address of the function to patch.
 @param name The name of the patch function exported by the bundle.
 @param info An info value, supplied to the patch bundle by the caller.
 */
typedef void (*__patch_details_cb)(void * addr, const char * name, void * info);

/*!
 @typedef __get_patch_details_fn
 @abstract Prototype for a function used to get Rosetta patch details.
 @discussion The Rosetta patching implementation will look for a
         function whose name matches the value of
         @link kGetPatchDetailsFunctionName kGetPatchDetailsFunctionName @/link
         within the patch bundle. It will then call that function,
         providing a callback the bundle can use to tell the injector
         which functions to patch.
 @param cb The function to call with details of each prospective patch.
 @param arch The architecture to use when looking up function addresses.
 @param info This should be passed back into the callback untouched.
 */
typedef void (*__get_patch_details_fn)(__patch_details_cb cb, int arch, void * info);

/*!
 @typedef patch_lookup_fn_t
 @abstract Callback used by a rosetta bundle to get reentry addresses.
 @discussion A pointer to a function matching this prototype is passed
         into the @link patch_link_fn_t patch_link_fn_t @/link
         implemented by the Rosetta patch bundle.
 @param patched_fn_addr The address of the target function.
 @result The re-entry address normally returned from
         @link //apple_ref/c/func/DPCreatePatch DPCreatePatch @/link.
 */
typedef void * (*patch_lookup_fn_t)( void * patched_fn_addr );

// this one is implemented by the bundle, and it's what we call to tell
// it that it's been loaded, and that it should call the given         
// patch_lookup_fn_t function to get the re-entry addresses for its    
// patched functions. It takes a function pointer for the lookup       
// routine, and the path to itself (this helps bundles create the      
// CFBundleRef for themselves, that they may expect to have since the  
// native injection routines pass this into the bundle start function).
/*!
 @typedef patch_link_fn_t
 @abstract Implemented by a Rosetta bundle to link to reentry islands.
 @discussion This function is implemented by the Rosetta patch bundle,
         and is called to give it the opportunity to link its re-entry
         function pointers; it is called when the bundle is first
         loaded into the target application's address space. It is
         given a @link patch_lookup_fn_t callback @/link to use when
         requesting these addresses, and is given its own executable
         path (within its bundle), such that it can recreate the
         CFBundleRef it would be passed in the native patching
         environment if so desired.
 @param fn A @link patch_lookup_fn_t callback @/link to retrieve
         reentry addresses.
 @param exec_path The path to this bundle's executable file.
 */
typedef void (*patch_link_fn_t)( patch_lookup_fn_t fn, const char * exec_path );

/*!
 @function DPPatchRemoteTask
 @abstract Starts a thread in the given mach task.
 @discussion Calling this function will begin a new thread in the given
         task, which will then proceed to load one or more patch
         bundles. If the second (optional) parameter is NULL, then all
         bundles in the ControlPlugins folder will be loaded. Otherwise,
         the bundle at the supplied path will be loaded.

         For processes running via Rosetta translation, the method is
         slightly different. Instead of inserting some startup code in
         a new thread, then making that new thread do the work of
         loading and installing the patches, the injection process must
         do most of the work. So, the injection process will load each
         patch in turn, call its check function to see if it should
         load into the target process, and if so, gets a list of
         functions to patch. It then sets up patch blocks for these,
         with branch targets initially pointing to a stub helper
         function which will load the relevant bundle & libraries. It
         then creates a new thread (which will run in the host
         architecture) and pass it a list of these patches. That
         routine will then atomically patch these functions before
         exiting.
 @param pid The process ID of the process to patch.
 @param path_to_patch The path to a specific single patch bundle to
         load. Can be NULL.
 */
DP_API void DPPatchRemoteTask( pid_t pid, const char * path_to_patch );

#endif  /* __DP_INJECTION_H__*/
