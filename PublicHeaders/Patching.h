/*
 *  Patching.h
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

#ifndef __DP_PATCHING_H__
#define __DP_PATCHING_H__

#include "DPAPI.h"

/*!
 @header Patching
 @author Jim Dovey
 @copyright 2003-2006 Jim Dovey. Some Rights Reserved.
 @ignore DP_API
 */

/*!
 @function DPCreatePatch
 @abstract Install a patch funtion on top of an existing function.
 @seealso //apple_ref/c/func/DPRemovePatch
 @seealso //apple_ref/c/func/DPCocoaMethodSwizzle
 @discussion This routine will patch an existing function. It takes the address
        of the function to patch, along with the address of a replacement function.

        The patch function should be declared in exactly the same manner as the
        function it is designed to patch. So, if the original function has Pascal
        linkage, so should the new one. If the original function was C++, the new
        one should also be C++; in general, C and C++ seem interchangeable
        on Mac OS X, however.

        This function works by installing a 'head patch'. The internal implementation
        overwrites the first instruction of the target function with a branch 
        instruction which will lead to a small error-checking 'jump table' entry - 
        a small chunk of compiled code which will ultimately call the provided patch 
        function. The patch function can call the original function through the
        pointer returned from this routine - it should NOT just call the original
        function, as that will ultimately just call the patch again.

        There are no limits on where in a patch funtion the original can be called. 
        In this manner, it is possible to implement a pseudo-tail patch, simply by
        writing the patch function such that the first thing it does is to call the 
        original function.

        For Example:<br />
        <pre>
        @textblock
// Function to patch.
extern int SomeFunction( float a, int * b );

// Typedef a pointer to that function
typedef ( *SomeFunction_Ptr )( float a, int * b );

// create somewhere to store returned 'original function pointer'
static SomeFunction_Ptr gSomeFunction = NULL;

// define our patch function
static int SomeFunction_Patch( float a, int * b )
{
    if ( a < 1.0f )
        a += 1.0f;

    if ( *b > 10 )
        *b = 10;

    // if this function has been called, the patch worked & this pointer
    //  is guaranteed to be valid.
    return ( gSomeFunction( a, b ) );
}

// routine to install the patch
void InstallPatch
{
    void * fn_addr = &SomeFunction;

    if ( fn_addr == NULL )
    {
        // linker couldn't find function address - look using API
        // Use the 'exact' version, since this is not a C++ function
        //  with a mangled name
        fn_addr = LookupExactFunctionAddress( "SomeFunction", "SomeModule" );
    }

    if ( fn_addr != NULL )
    {
        gSomeFunction = DPCreatePatch( fn_addr, &SomeFunction_Patch );
    }
}
@/textblock
        </pre>
 @param fn_addr The address of the function to patch. This address can be specified
        in one of three ways:<br />
        <ol type="1">
        <li>Pass in 'SomeFunction'</li>
        <li>Pass in '&SomeFunction'</li>
        <li>Pass the return value of FindFunctionAddress("SomeFunction", "SomeModule")
            or FindVagueFunctionAddress("SomeFunction", "SomeModule").</li>
        </ol>
        1 and 2 are functionally identical. For private external symbols, however, it 
        will likely be necessary to use method 3 to lookup the addresses in the
        symbol table of the module which defines them.
 @param patch_addr The address of the user-defined patch function. This should be passed
        in using methods 1 or 2 above (3 won't be necessary). For safety's sake, it is
        recommended that patch functions all reside in one file, along with any routines
        which install them, and that these patch functions are declared static (C-style
        static, accessible only from this file) to avoid potentially harmful namespace
        pollution, or the potential for the patch function to be called directly.

 @result This function returns an address which can be used to call the original function.
        Note that this is NOT the same as the address of the original function - it is 
        actually the address of a 'jump table' entry which will ultimately branch into
        the original function's implementation without going to the patch code first.
 */
DP_API void * DPCreatePatch( void * fn_addr, void * patch_addr );

/*!
 @function DPRemovePatch
 @abstract Removes a patch from the specified function.
 @seealso //apple_ref/c/func/DPCreatePatch
 @discussion This will remove a patch installed by the
         @link DPCreatePatch DPCreatePatch @/link instruction. It does
         this by following the patch branch instruction to the re-entry
         table, reading the saved instruction(s) from there, and
         putting them back into the patched function.
 @param fn_addr The address of the original (patched) function, from
         which to remove the patch.
 */
DP_API void DPRemovePatch( void * fn_addr );

/*!
 @function DPCocoaMethodSwizzle
 @abstract Patch a function implemented within an Objective-C object.
 @seealso //apple_ref/c/func/DPCreatePatch
 @discussion This function is an alternative to @link DPCreatePatch DPCreatePatch @/link for
        Objective-C member functions, which does not require any runtime compilation
        of patch code. Instead, it simply rewrites the content of the method table
        for the given object (or category) such that the given patch function address
        is recorded as the implementation address for the object's handler for a
        particular selector.

        The caller can specify whether to patch a class or instance method.

        The routine uses the Objective-C runtime itself to perform lookups, and as such
        will check at runtime to see if that is available; if not, this function will
        simply return NULL -- this library does not link against or automatically load
        the Objective-C runtime.

        See @link DPCreatePatch DPCreatePatch @/link for a detailed example of how to use 
        the patching routines; in place of the CreatePatch() call, however, you would
        use this function.

        For example, to patch a method within object 'MyObject' with the prototype:

        <code>-(void) setName: (NSString *) name ofItem: (int) item;</code>

        ...you would use the following call:

        <code>original_fn = DPCocoaMethodSwizzle("MyObject", "setName:ofItem:", &patch_fn, 0);</code>

        Note that, in C terms, an Objective-C function takes two extra parameters, so
        the patch function used above would have the following prototype:

        <code>void setName_ofItem_patchfn(id _self, SEL _sel, NSString * name, int item);</code>

        <b>Removing Patches</b>
        Unlike @link DPCreatePatch DPCreatePatch @/link, there is no
        corresponding 'unpatch' call for this function. Since it only
        overwrites a function address in memory, it can be easily
        reverted to its original value by passing the re-entry address
        (as returned from this function when setting up the patch) back
        into this function. This will have the effect of re-instating
        the original implementation address.

        NB: Method swizzling generally seems to work as expected.
        However, the Objective-C runtime does maintain a cache of
        method definitions, which can mean that the changed function
        pointer isn't accessed.
 @param class_name The name of the class implementing the method to patch.
 @param selector_name A string defining the selector of the method to patch.
 @param patch_addr The address of the patch function.
 @param class_method Pass zero if the method to patch is an instance method, or
        nonzero if it is a class method.
 @result Returns the original implementation address of the specified method. This is
        suitable for calling the original function from within your patch code. It can
        be called at any time, or not at all.
*/
DP_API void * DPCocoaMethodSwizzle( const char * class_name, const char * selector_name, 
                                    void * patch_addr, int class_method );

#endif  /* __DP_PATCHING_H__  */
