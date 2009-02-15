/*
 *  PTAController.m
 *  PatchTargetApp
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

#import "PTAController.h"

@implementation PTAController

- (IBAction) showAlert: (id) sender
{
    // we use CFNotify API, because I don't want to mess about with
    // patching vararg routines like NSRunAlertPanel
    (void) CFUserNotificationDisplayNotice( 30.0, kCFUserNotificationNoteAlertLevel,
                                            NULL, NULL, NULL,
                                            (CFStringRef) [titleField stringValue],
                                            (CFStringRef) [msgField stringValue],
                                            NULL );
}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed: (NSApplication *) app
{
    return ( YES );
}

@end
