/*
 *  Lookup.h
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

#ifndef __DP_LOOKUP_H__
#define __DP_LOOKUP_H__

#include "DPAPI.h"

/*!
 @header Lookup
 @author Jim Dovey
 @copyright 2003-2006 Jim Dovey. Some Rights Reserved.
 @ignore DP_API
 */

/*!
 @enum Platform Architecture Constants
 @discussion These constants are mostly used when 
 injecting PPC code into a Rosetta process. However, those functions
 also require cross-architecture function address lookups for finding
 PPC addresses from Intel code. As such, the constans are defined here
 and are passed to
 @link //apple_ref/c/func/FindFunctionForArchitecture FindFunctionForArchitecture @/link
 to enable those lookups.
 */
enum
{
    kInsertionArchPPC   = 0,      /*! PowerPC architecture */
    kInsertionArchIA32  = 1       /*! IA-32 (i386+) architecture */
};

/*!
 @function DPFindFunctionAddress
 @abstract Look up the address of a function.
 @discussion This function will lookup the address for a specified function in the 
        symbol table of the given module. It performs an exact, case-sensitive
        comparison using strcmp(), and as such can only be used with C++ functions
        if it is supplied with the C++ compiler's mangled name for a C++ function.

        See @link //apple_ref/c/func/LookupFunctionAddress LookupFunctionAddress @/link
        for more information on looking up address for C++ functions.
 @param pExactFunctionName Name of the function for which to search.
 @param pModuleName Name of the module (library, bundle) which defines the function.
 @result The address of the desired function.
 */
DP_API void * DPFindFunctionAddress( const char *pExactFunctionName, const char *pModuleName );

/*!
 @function DPFindVagueFunctionAddress
 @abstract Look up the address of a function with a potentially mangled name.
 @discussion This function will look for the function name given, inside the named
        code module, using strstr() as a comparison tool. It was created to support
        searching for functions whose names may have been mangled by a C++ compiler.

        The named module will NOT be loaded into the address space of the program by
        this call - it is assumed to already be there; if it isn't, no search will
        take place.

        The supplied function name will be compared against public and private 
        symbols declared in the given module using strstr(). This is to support 
        patches which may find themselves needing to patch C++ routines which may 
        have been built by different compilers (with different name mangling, 
        resulting in different symbol names). For instance, GCC 3.1 and GCC 3.3 both 
        use a different C++ ABI, with different function name mangling. This can 
        ultimately lead to C++ functions built into the 10.2 version of OS X to have 
        different symbol names to those in 10.3. Therefore, this function allows the 
        caller to specify the 'real' name of the C++ function for which to search, 
        and it should return the address of the correct function, regardless of name 
        mangling.

        However, this function is fundamentally unable to tell the difference between
        SomeFunc(int) and SomeFunc(float), since they will both contain the function
        name 'SomeFunc'. It will therefore return the address of the first function it
        finds. As such, it is strongly recommended to use LookupExactFunctionAddress(),
        passing the C++-mangled name of the function for which to search - this name
        can be found by using the 'nm' command line tool, as follows:<br />
        <pre>
 @textblock
 nm /path/to/module_executable | grep "SomeFunc"
 @/textblock
        </pre>
 @param pFunctionName Name of the function for which to search.
 @param pModuleName Name of the module (library, bundle) which defines the function.
 @result The address of the desired function.
 */
DP_API void * DPFindVagueFunctionAddress( const char *pFunctionName, const char *pModuleName );

/*!
 @function LookupCocoaFunctionAddressFromDeclaration
 @abstract Look up the address of an Objective-C instance method.
 @discussion This function will lookup the address of an instance method in an
        Objective-C object. It uses the Objective-C runtime APIs, and as such can 
        only operate if the Objective-C runtime is loaded into this process.

        Internally, it parses the given method signature, and proceeds to lookup
        the class object, then creates an Objective-C selector and looks up the
        address of the routine in the given class which implements it.

        This function will NOT find subclass implementation, nor superclass
        implementations. It is therefore important that in order to patch the
        'performClick:' method of any NSSecureTextField objects, that the actual
        class which implements the 'performClick:' method is patched. If
        NSSecureTextField does not implement this function, then this routine
        should be called with the superclass (NSTextField), or that class's
        superclass (NSControl), to find the address of its implementation.
 @param pCocoaDeclaration The Objective-C-style declaration of the function.

        For example, use <tt>-[NSObject selector:with:arguments]</tt> to look up
        an instance method, or <tt>+[NSObject classSelector:object:]</tt>. to
        look up a class method. 

        Note the following rules:<br />
        <ul type="disc">
        <li>MUST begin with '-' or '+'; no default is assumed.</li>
        <li>The remainder MUST be surrounded by square braces ([]).</li>
        <li>The first whole word inside the square braces is the class name.</li>
        <li>It next whole word is the selector, with no spaces or types.</li>
        </ul>

 @result The address of the function implementation.
 */
DP_API void * DPLookupCocoaFunctionAddressFromDeclaration( const char *pCocoaDeclaration );

/*!
 @function LookupCocoaFunctionAddress
 @abstract Look up the address of an Objective-C instance method.
 @discussion This function will lookup the address of an instance method in an
        Objective-C object. It uses the Objective-C runtime APIs, and as such can 
        only operate if the Objective-C runtime is loaded into this process.

        This version of the method takes separate class and selector
        strings, and therefore gets to skip the parse step utilized by
        the function above.
 @param pClass The name of the class implementing the function.
 @param pSelector The text of the selector for which to search.
 @param class_method Set to zero if the sought function is an instance
        method, or non-zero to search for a class method.

 @result The address of the function implementation.
 */
DP_API void * DPLookupCocoaFunctionAddress( const char *pClass, const char *pSelector,
                                     int class_method );

/*!
 @function FindFunctionForArchitecture
 @abstract Lookup a function-symbol address for a specified CPU architecture.
 @discussion This is essentially analogous to
        @link //apple_ref/c/func/LookupExactFunctionAddress LookupExactFunctionAddress @/link,
        with the addition of one extra parameter. This will cause the lookup engine
        to search within a fat binary for the architecture specified, and will
        then look for the symbol within that particular file, performing any necessary
        byte-swapping.
 @param pName Name of the function to locate (a symbol name, minus any leading underscore).
 @param pModule Name of the module which defines the function -- the original will be
        loaded from disk to be searched. You can pass a fully-qualified path to a new
        module (beginning with a '/' character) to arbitrarily load a binary to search.
 @param arch A @link //apple_ref/c/tag/PlatformArchitectureConstants constant @/link
        specifying which architecture within a fat binary to search for the named function.

 @result The address of the named function, or NULL if not found.
*/
DP_API void * DPFindFunctionForArchitecture( const char *pName, const char *pModule, int arch );

#endif  /* __DP_LOOKUP_H__*/
