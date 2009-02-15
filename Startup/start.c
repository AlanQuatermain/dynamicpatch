/*
 *  start.c
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

#include "logging.h"
#include "load_bundle.h"

// we need to make sure that malloc doesn't thing it's single threaded
// while we're doing stuff now
extern void set_malloc_singlethreaded( int );

#pragma mark -

// check against a list of apps we generally avoid patching
static int _check_valid_apps( void )
{
    int result = 1;
    const char *pName = ApplicationName( );

    if ( (strcmp( pName, "SystemUIServer" ) == 0) ||
         (strcmp( pName, "System Events" ) == 0) ||
         (strcmp( pName, "Transport Monitor" ) == 0) ||
         (strcmp( pName, "UniversalAccessApp" ) == 0) )
    {
        result = 0;
        LogMessage( "Quarantined application, no injection allowed." );
    }

    return ( result );
}

#pragma mark -

// these are external functions, they need to be looked up by the
// injected code block

void __start_all_patches( void )
{
    set_malloc_singlethreaded( 0 );
    InitLogs( "DynamicPatch" );

    DEBUGLOG( "Injector Start, all bundles" );

    // doing all patches, we filter out some target apps
    // generally this function is used when we're doing an
    // Unsanity-style 'patch every app'. So, avoid some apps.
    // Comment this out if you don't like it, but be careful when
    // attaching to certain system applications
    if ( _check_valid_apps( ) )
    {
        // enumerate installed patches
        load_patch_bundles( );
    }
}

void __start_one_patch( const char *pPathToPatch )
{
    set_malloc_singlethreaded( 0 );
    InitLogs( "DynamicPatch" );

    DEBUGLOG( "Injector Start, bundle '%s'", pPathToPatch );

    // in this one, we assume you're explicitly targetting something,
    // so you get the benefit of the doubt with regard to your target
    // application
    load_patch_bundle( pPathToPatch );
}
