/*
 *  main.c
 *  TargetPatcher/PatchBundle
 *
 *  Created by jim on 5/4/2006.
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

#include <DynamicPatch/DynamicPatch.h>
#include <CoreFoundation/CodeFoundation.h>

// our patched function is in the CoreFoundation framework
#define FMWK_PATH "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"

// function pointer used by patching calls
typedef SInt32 (* CFUserNotificationDisplayNotice_fn_t)( CFTimeInterval, CFOptionFlags, CFURLRef,
    CFURLRef, CFURLRef, CFStringRef, CFStringRef, CFStringRef );

// our reentry function ptr
static CFUserNotificationDisplayNotice_fn_t gCFUserNotify = 0;

// the patch function
SInt32 CFUserNotificationDisplayNotice_patch( CFTimeInterval timeout, CFOptionFlags opts,
                                              CFURLRef iconURL, CFURLRef soundURL,
                                              CFURLRef localizationURL,
                                              CFStringRef title, CFStringRef message,
                                              CFStringRef defaultButtonTitle )
{
    // put up our own dialog first, for five seconds
    (void) gCFUserNotify( 5.0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL,
                          CFSTR( "Patch Message" ), CFSTR( "This alert has been patched" ),
                          NULL );

    // call the original function
    return ( gCFUserNotify( timeout, opts, iconURL, soundURL, localizationURL,
                            title, message, defaultButtonTitle ) );
}

#pragma mark -

// native patch start point

int PatchMain( CFBundleRef myBundle )
{
    int result = 0;

    // install our patch
    gCFUserNotify = DPCreatePatch( CFUserNotificationDisplayNotice,
                                   CFUserNotificationDisplayNotice_patch );

    // if the patch failed, we tell the caller to unload this bundle
    if ( gCFUserNotify != 0 )
        result = 1;

    return ( result );
}

#pragma mark -

// non-native (Rosetta) patch entry points here

int WillPatchApplication( CFBundleRef myBundle, pid_t targetPid,
                          const char * appName )
{
#pragma unused(myBundle)
#pragma unused(targetPid)
#pragma unused(appName)

    return ( 1 );
}

// not really necessary *here*, but this is a useful way of handling
// multiple patches
typedef struct __pbuf
{
    const char *    fn_name;
    const char *    patch_name;
    void *          fn_addr;

} __pbuf;

#define PBUF_ENTRY(fn) { "_" #fn, "_" #fn "_patch", 0 }

static __pbuf _pbufs[ ] =
{
    PBUF_ENTRY(CFUserNotificationDisplayNotice),
    { NULL, NULL, 0 }
};

void GetPatchDetails( patch_detail_cb callback, int arch, void * info )
{
    // for each function, get the address in the appropriate
    // architecture, and pass that and the patch function's name into
    // the given callback
    __pbuf *pbuf = _pbufs;

    // first we look up the names, exiting on errors
    while ( pbuf->fn_name != NULL )
    {
        // NB: This assumes all patches are in the same framework
        pbuf->fn_addr = FindFunctionForArchitecture( pbuf->fn_name,
            FMWK_PATH, arch );

        if ( pbuf->fn_addr == NULL )
        {
            LogError( "Unable to locate address of function %s",
                      pbuf->fn_name );
            return;
        }

        pbuf++;
    }

    // now hand off these details
    pbuf = _pbufs;
    while ( pbuf->fn_name != NULL )
    {
        callback( pbuf->fn_addr, pbuf->patch_name, info );
        pbuf++;
    }
}

// called when we're first loaded into the target application
void LinkPatches( patch_lookup_fn lookup, const char * exec_path )
{
#pragma unused(exec_path)
    gCFUserNotify = (CFUserNotificationDisplayNotice_fn_t)
                    lookup( CFUserNotificationDisplayNotice );

    // this is all we need to do here
}
