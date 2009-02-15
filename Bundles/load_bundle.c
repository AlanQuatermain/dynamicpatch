/*
 *  load_bundle.c
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

#include <CoreFoundation/CoreFoundation.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "logging.h"

// get the function name constants from here
#include "Injection.h"

// there's actually a CF wrapper around this, but it's not in a
// public header
#include <NSSystemDirectories.h>

// change this along with the constant below to change where we look
// for patch bundles. Default is (~)/Library/Application Support/.
const NSSearchPathDirectory srchDir = NSApplicationSupportDirectory;

// change this as appropriate, to search within enumerated directories
const char *pBundleSubpath      = "/DynamicPatch/";

// change this if you want to use a different extension for your plugin
// bundles
const char *pBundleType         = "patch";

// this is the expected prototype for the startup function. It should
// return zero if it is to be unloaded from memory (if it decided not
// to patch anything, or if some error occurred), and nonzero if
// everything went okay.
typedef int (*__bundle_start_fn_ptr)( CFBundleRef bundle );

// Once upon a time, there was this thing called Toll-Free Bridging,
// which enabled one to interchange CFStringRef and NSString* objects
// freely. And it just so happened that the CoreFoundation side of this
// appeared to check the CFType id of the given object against those of
// the known CFStringRef 'classes', assuming that any value not in its
// list was an NSString. However, it appeared to miss
// kCFConstantString. So, as a result, sometimes when a constant
// string, created using the CFSTR() macro, was passed into a function,
// the application would crash because the Objective-C runtime would
// try to treat is as a real NSString instance. However, there was
// neither rhyme nor reason to its occurence, so the only reliable way
// around it was to just not use the CFSTR macro, but to dynamically
// allocate CFStringRef wrappers around constant C-Strings. Hence, this
// macro was created to take away some of the copious typing necessary:
#if 1
# define DEFINE_CFSTR(str)  CFStringCreateWithCString(NULL, str, kCFStringEncodingASCII)
#else
# define DEFINE_CFSTR(str)  CFSTR(str)
#endif

// the patch bundles are expected to implement 

#pragma mark -

// this is called for each bundle found
static int handle_a_bundle( CFBundleRef bundle )
{
    // NB: At this point, we could search through the bundle's
    // Info.plist looking for information we could use to predicate
    // whether we'll even load it. The original code does this, but
    // since that's an implementation of the (private) framework of
    // which the patching is just a part, I've left it out here.

    int result = 0;

    if ( CFBundleLoadExecutable( bundle ) )
    {
        __bundle_start_fn_ptr fn = 0;
        CFStringRef funcName = DEFINE_CFSTR(kStartFunctionName);

        fn = (__bundle_start_fn_ptr) CFBundleGetFunctionPointerForName( bundle, funcName );

        DEBUGLOG( "Loaded bundle executable, start fn addr is 0x%08X", (unsigned) fn );

        if ( fn != 0 )
        {
            result = fn( bundle );
            DEBUGLOG( "Called startup function, result is %d", result );
        }
        else
        {
            LogError( "Couldn't find startup function!" );
        }

        if ( result == 0 )
        {
            DEBUGLOG( "Unloading bundle." );
            // bundle startup failed, cancelled, or we couldn't find
            // the function
            CFBundleUnloadExecutable( bundle );
        }

        if ( funcName != NULL )
            CFRelease( funcName );
    }

    return ( result );
}

#pragma mark -

void load_patch_bundles( void )
{
    NSSearchPathEnumerationState srchState;
    struct stat statBuf;
    CFStringRef bundleType = DEFINE_CFSTR(pBundleType);
    char path[ PATH_MAX ];

    srchState = NSStartSearchPathEnumeration( srchDir,
                                              NSUserDomainMask | NSLocalDomainMask );

    while ( (srchState = NSGetNextSearchPathEnumeration(srchState, path)) != 0 )
    {
        // append our subfolder
        // the system doesn't seem to mind double-separators
        strlcat( path, pBundleSubpath, PATH_MAX );

        if ( stat( path, &statBuf ) != -1 )
        {
            // it exists, enumerate its contents
            CFStringRef bundlePath = NULL;
            CFURLRef url = NULL;
            CFArrayRef bundles = NULL;

            bundlePath = CFStringCreateWithCString( NULL, path, kCFStringEncodingUTF8 );
            url = CFURLCreateWithFileSystemPath( NULL, bundlePath, kCFURLPOSIXPathStyle, TRUE );
            bundles = CFBundleCreateBundlesFromDirectory( NULL, url, bundleType );

            if ( bundles != NULL )
            {
                CFBundleRef bundle = NULL;
                CFIndex i = 0;
                CFIndex num = CFArrayGetCount( bundles );

                DEBUGLOG( "Got %u bundles in folder '%s'", num, path );

                for ( i = 0; i < num; i++ )
                {
                    bundle = (CFBundleRef) CFArrayGetValueAtIndex( bundles, i );

                    if ( bundle != NULL )
                    {
                        // if the bundle loads stuff, we need to keep
                        // it around when we release the array
                        if ( handle_a_bundle( bundle ) )
                            CFRetain( bundle );
                    }
                    else
                    {
                        LogError( "Failed to get bundle at index '%u'", i );
                    }
                }

                CFRelease( bundles );
            }

            if ( url != NULL )
                CFRelease( url );
            if ( bundlePath != NULL )
                CFRelease( bundlePath );
        }
    }

    if ( bundleType != NULL )
        CFRelease( bundleType );
}

void load_patch_bundle( const char *pPatchToLoad )
{
    // I could check for a null pointer, etc. But let's just assume
    // that you're not going to be silly & pass null. What would be the
    // point?

    CFStringRef path = CFStringCreateWithCString( NULL, pPatchToLoad,
        kCFStringEncodingASCII );
    if ( path != NULL )
    {
        CFURLRef url = CFURLCreateWithFileSystemPath( NULL,
            path, kCFURLPOSIXPathStyle, TRUE );
        if ( url != NULL )
        {
            CFBundleRef bundle = CFBundleCreate( NULL, url );
            if ( bundle != NULL )
            {
                // if we don't need it to stay around, unload the bundle
                if ( handle_a_bundle( bundle ) == 0 )
                    CFRelease( bundle );
                // if we've loaded code, we can't release the bundle,
                // as that will unload the code. Eep.
            }
            else
            {
                LogError( "Failed to load bundle '%s' !",
                          pPatchToLoad );
            }

            CFRelease( url );
        }
        else
        {
            LogError( "Failed to create CF url with path '%s' !",
                      pPatchToLoad );
        }

        CFRelease( path );
    }
    else
    {
        LogError( "Failed to create CFString of '%s' !",
                  pPatchToLoad );
    }
}

CFArrayRef list_patch_bundles( void )
{
    CFMutableArrayRef result = NULL;

    NSSearchPathEnumerationState srchState;
    struct stat statBuf;
    CFStringRef bundleType = DEFINE_CFSTR(pBundleType);
    char path[ PATH_MAX ];

    srchState = NSStartSearchPathEnumeration( srchDir,
                                              NSUserDomainMask | NSLocalDomainMask );

    while ( (srchState = NSGetNextSearchPathEnumeration(srchState, path)) != 0 )
    {
        // append our subfolder
        // the system doesn't seem to mind double-separators
        strlcat( path, pBundleSubpath, PATH_MAX );

        if ( stat( path, &statBuf ) != -1 )
        {
            // it exists, enumerate its contents
            CFStringRef bundlePath = NULL;
            CFURLRef url = NULL;
            CFArrayRef bundles = NULL;

            bundlePath = CFStringCreateWithCString( NULL, path, kCFStringEncodingUTF8 );
            url = CFURLCreateWithFileSystemPath( NULL, bundlePath, kCFURLPOSIXPathStyle, TRUE );
            bundles = CFBundleCreateBundlesFromDirectory( NULL, url, bundleType );

            if ( (bundles != NULL) && (CFArrayGetCount(bundles) > 0) )
            {
                if ( result == NULL )
                {
                    result = CFArrayCreateMutableCopy( NULL, 0, bundles );
                }
                else
                {
                    CFRange range = CFRangeMake( 0, CFArrayGetCount(bundles) );
                    CFArrayAppendArray( result, bundles, range );
                }
            }
        }
    }

    return ( result );
}
