/*
 *  apps.c
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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static int GetProcessDetails( pid_t targetPid, struct kinfo_proc **ppProc )
{
    int err = 0;
    int done = 0;
    const int name[ ] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, targetPid, 0 };
    size_t name_size = sizeof(name);
    struct kinfo_proc * procInfo = NULL;
    int procCount = 0;

    size_t length = 0;

    do
    {
        // Call sysctl with a null buffer
        err = sysctl( ( int * ) name, (name_size / sizeof(*name)) - 1,
                      NULL, &length, NULL, 0 );

        if ( err == -1 )
        {
            err = errno;
        }

        // Allocate an appropriately sized buffer based on the results
        //  from the previous call...
        if ( err == 0 )
        {
            procInfo = ( struct kinfo_proc * ) malloc( length );

            if ( procInfo == NULL )
            {
                err = ENOMEM;
            }
        }

        // Call sysctl again with the new buffer. If we get ENOMEM we dump the buffer
        //  and start again
        if ( err == 0 )
        {
            err = sysctl( ( int * ) name, (name_size / sizeof(*name)) - 1,
                          procInfo, &length, NULL, 0 );

            if ( err == -1 )
            {
                err = errno;
            }
            if ( err == 0 )
            {
                done = 1;
            }
            else if ( err == ENOMEM )
            {
                if ( procInfo != NULL )
                {
                    free( procInfo );
                }

                procInfo = NULL;
                procCount = 0;
                err = 0;
            }
        }
    } while ( ( err == 0 ) && ( !done ) );

    // Clean up & establish post conditions
    if ( ( err != 0 ) && ( procInfo != NULL ) )
    {
        free( procInfo );
        procInfo = NULL;
    }

    if ( err == 0 )
    {
        procCount = length / sizeof( struct kinfo_proc );
    }

    *ppProc = procInfo;

    return ( procCount );
}

const char * NameForProcessID( pid_t target_pid )
{
    const char * result = NULL;
    struct kinfo_proc *pProc = NULL;
    int count = GetProcessDetails( target_pid, &pProc );

    if ( count > 0 )
    {
        // should only be one item, but...
        int i;
        for ( i = 0; i < count; i++ )
        {
            if ( pProc[i].kp_proc.p_pid == target_pid )
            {
                result = strdup( pProc[i].kp_proc.p_comm );
                break;
            }
        }
    }

    if ( pProc != NULL )
        free( pProc );

    return ( result );
}
