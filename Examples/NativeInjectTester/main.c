/*
 *  main.c
 *  DynamicPatch/NativeInjectTester
 *
 *  Created by jim on 3/4/2006.
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

int PatchMain( CFBundleRef myBundle )
{
    // just do something to show that it worked
    CFUserNotificationDisplayNotice( 10.0, kCFUserNotificationNoteAlertLevel,
                                     NULL, NULL, NULL,
                                     CFSTR( "Patch Loaded" ),
                                     CFSTR( "This is your patch code. Looks like I worked." ),
                                     CFSTR( "OK" ) );

    // don't stay loaded
    return ( 0 );
}
