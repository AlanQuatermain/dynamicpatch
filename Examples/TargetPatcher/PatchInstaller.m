#import "PatchInstaller.h"

@implementation PatchInstaller

- (IBAction) installPatch: (id) sender
{
    NSWorkspace * ws = [ NSWorkspace sharedWorkspace ];

    NSArray * apps = [ ws launchedApplications ];
    NSEnumerator * enumerator = [ apps objectEnumerator ];
    NSDictionary * obj = nil;
    pid_t procid = 0;

    while ( (obj = [enumerator nextObject]) != nil )
    {
        NSString * str = [ obj objectForKey: @"NSApplicationName" ];

        if ( [ str isEqualToString: @"PatchTargetApp" ] )
        {
            NSNumber * num = [ obj objectForKey: @"NSApplicationProcessIdentifier" ];
            if ( num != nil )
                procid = [ num unsignedIntValue ];

            break;
        }
    }

    if ( procid != 0 )
    {
        NSBundle * me = [ NSBundle mainBundle ];
        NSString * bundlePath = [ me pathForResource: @"PatchBundle"
                                              ofType: @"patch" ];

        if ( bundlePath != nil )
        {
            DPPatchRemoteTask( procid, [bundlePath UTF8String] );
        }
        else
        {
            NSRunAlertPanel( @"Error", @"Couldn't get patch bundle path !",
                             @"Argh!", @"", @"" );
        }
    }
    else
    {
        NSRunAlertPanel( @"Error", @"PatchTargetApp is not running",
                         @"D'oh", @"", @"" );
    }
}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed: (NSApplication *) app
{
    return ( YES );
}

@end
