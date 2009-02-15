/*
 *  cocoa_lookup.c
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#include "atomic.h"

// The headers for the Objective-C runtime aren't entirely pure C.
// They're headers for .m files, so we can't really include them here.
// This framework doesn't link against the Cocoa frameworks at all, so
// we just redefine a few things here, to match the prototypes in
// <objc/objc-class.h> and others.

// The Objective-C routines in this file require that the Objective-C
// runtime already be linked. Since the idea of this whole framework is
// to patch other things, it's fairly pointless for it to cause the
// Objective-C runtime to load in an application that otherwise
// wouldn't use it, just to patch a function which wouldn't even be
// resident in memory without our interference. Hence all the
// function-pointer abstraction going on below.

struct objc_method
{
    char * method_name;
    char * method_types;
    void * method_imp;
};

typedef struct objc_method * Method;
typedef struct objc_selector * SEL;

typedef void * ( *objc_getClassFn ) ( const char * name );
typedef Method ( *class_getInstanceMethodFn ) ( void * inClass, SEL selector );
typedef Method ( *class_getClassMethodFn ) ( void * inClass, SEL selector );
typedef SEL ( *sel_getUidFn ) ( const char * pMsgName );

static objc_getClassFn my_objc_getClass = NULL;
static class_getInstanceMethodFn my_class_getInstanceMethod = NULL;
static class_getClassMethodFn my_class_getClassMethod = NULL;
static sel_getUidFn my_sel_getUid = NULL;

static int LinkedObjcRuntime = 0;

// this function is defined by the local patching routines, and is used
// by the method-swizzler defined below.
extern int __make_writable( void * addr );

#pragma mark -

static int SetupForObjcLookup( void )
{
    if ( !LinkedObjcRuntime )
    {
        // see if libobjc is loaded
        if ( NSIsSymbolNameDefined( "_objc_getClass" ) )
        {
            _dyld_lookup_and_bind_with_hint( "_objc_getClass", "libobjc",
                                             (void **) &my_objc_getClass, NULL );
            _dyld_lookup_and_bind_with_hint( "_class_getInstanceMethod", "libobjc",
                                             (void **) &my_class_getInstanceMethod, NULL );
            _dyld_lookup_and_bind_with_hint( "_class_getClassMethod", "libobjc",
                                             (void **) &my_class_getClassMethod, NULL );
            _dyld_lookup_and_bind_with_hint( "_sel_getUid", "libobjc",
                                             (void **) &my_sel_getUid, NULL );

            if ( my_objc_getClass != NULL )
                LinkedObjcRuntime = 1;
        }
    }

    return ( LinkedObjcRuntime );
}

static Method LookupMethod( const char * class, const char * message, int class_method )
{
    void * classObj = NULL;
    Method methodObj = NULL;
    SEL selector = my_sel_getUid( message );

    if ( selector != NULL )
    {
        classObj = my_objc_getClass( class );

        if ( classObj != NULL )
        {
            if ( class_method )
            {
                methodObj = my_class_getClassMethod( classObj, selector );
            }
            else
            {
                methodObj = my_class_getInstanceMethod( classObj, selector );
            }
        }
    }

    return ( methodObj );
}

static void * CocoaLookup( const char *pDecl )
{
    void * result = NULL;
    int i = 0;
    int class_method = -1;
    char className[ 255 ];
    char messageName[ 512 ];

    // need to have a '+' or '-' first in string
    if ( pDecl[ 0 ] == '+' )
        class_method = 1;
    else if ( pDecl[ 0 ] == '-' )
        class_method = 0;

    if ( class_method != -1 )
    {
        int declsize = strlen( pDecl );
        int in_class = 0;
        int in_message = 0;
        int j = 0;

        for ( i = 1; i < declsize; i++ )
        {
            if ( pDecl[ i ] == '[' )
            {
                in_class = 1;
                continue;
            }

            if ( in_class )
            {
                if ( pDecl[ i ] == ' ' )
                {
                    in_class = 0;
                    in_message = 1;
                    className[ j ] = '\0';
                    j = 0;
                    continue;
                }

                className[ j++ ] = pDecl[ i ];
                continue;
            }

            if ( in_message )
            {
                if ( pDecl[ i ] == ']' )
                {
                    messageName[ j ] = '\0';
                    break;
                }

                messageName[ j++ ] = pDecl[ i ];
                continue;
            }
        }

        if ( ( className[ 0 ] != '\0' ) && ( messageName[ 0 ] != '\0' ) )
        {
            Method methodObj = LookupMethod( className, messageName, class_method );

            if ( methodObj != NULL )
                result = methodObj->method_imp;
        }
    }

    return ( result );
}

#pragma mark -

// public entry points
void * DPLookupCocoaFunctionAddressFromDeclaration( const char *pDecl )
{
    void * result = NULL;

    if ( SetupForObjcLookup( ) )
        result = CocoaLookup( pDecl );

    return ( result );
}

void * DPLookupCocoaFunctionAddress( const char *pClass, const char *pSelector,
                                     int class_method )
{
    void * result = NULL;

    if ( SetupForObjcLookup( ) )
    {
        Method methodObj = LookupMethod( pClass, pSelector, class_method );

        if ( methodObj != NULL )
            result = methodObj->method_imp;
    }

    return ( result );
}

void * DPCocoaMethodSwizzle( const char *pClass, const char *pSelector,
                             int class_method, void * patch_addr )
{
    void * result = NULL;

    if ( ( patch_addr != NULL ) && ( SetupForObjcLookup( ) ) )
    {
        Method methodObj = LookupMethod( pClass, pSelector, class_method );

        if ( methodObj != NULL )
        {
            int swapResult = 0;

            // make sure we can modify this structure
            __make_writable( methodObj );

            do
            {
                result = methodObj->method_imp;
                swapResult = DPCompareAndSwap( (unsigned int) result,
                    (unsigned int) patch_addr,
                    (unsigned int *) methodObj->method_imp );

            } while ( swapResult == 0 );

            // synchronize instruction & data caches with memory
            DPCodeSync( &methodObj->method_imp );
        }
    }

    return ( result );
}
