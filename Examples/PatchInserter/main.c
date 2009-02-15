/*
 *  main.c
 *  DynamicPatch/PatchInserter
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sysexits.h>

#include <mach/mach_traps.h>

#include <DynamicPatch/DynamicPatch.h>

int main( int argc, const char * argv[ ] )
{
    pid_t target_pid = 0;
    task_t task_port = MACH_PORT_NULL;

    if ( argc != 3 )
    {
        printf( "Usage: PatchInserter <target_pid> <patch_path>\n" );
        exit( EX_USAGE );
    }

    target_pid = (pid_t) strtoul( argv[1], NULL, 10 );
    if ( target_pid != 0 )
    {
        InitLogs( "PatchInserter" );
        DPPatchRemoteTask( target_pid, argv[2] );
    }
    else
    {
        printf( "Failed to get task port for pid %u !\n", target_pid );
    }

    return ( EX_OK );
}
