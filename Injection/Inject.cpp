/*
 *  Inject.c
 *  DynamicPatch
 *
 *  Created by jim on 30/3/2006.
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

// This file provides the C implementation of the PatchRemoteTask
// function. It calls through to real C++ code.

#include "Injector.h"
#include "Logging.h"

#if defined(__i386__)

#include <sys/sysctl.h>

// based on code from Apple's Universal Binary Programming Guidelines,
// Second Edition
static int sysctlbyname_with_pid( const char * name, pid_t pid,
                                  void *oldp, size_t *oldlenp,
                                  void *newp, size_t newlen )
{
    if ( pid == 0 )
    {
        // check this process
        if ( sysctlbyname( name, oldp, oldlenp, newp, newlen ) == -1 )
        {
            LogError( "sysctlbyname_with_pid(0): sysctlbyname failed: %d (%s)",
                      errno, strerror(errno) );
            return ( -1 );
        }
    }
    else
    {
        int mib[ CTL_MAXNAME ];
        size_t len = CTL_MAXNAME;

        if ( sysctlnametomib( name, mib, &len ) == -1 )
        {
            LogError( "sysctlbyname_with_pid(%u): sysctlnametomib failed: %d (%s)",
                      pid, errno, strerror(errno) );
            return ( -1 );
        }

        mib[len] = pid;
        len++;
        if ( sysctl( mib, len, oldp, oldlenp, newp, newlen ) == -1 )
        {
            LogError( "sysctlbyname_with_pid(%u): sysctl failed: %d (%s)",
                      pid, errno, strerror(errno) );
            return ( -1 );
        }
    }

    return ( 0 );
}

static bool IsRosettaProcess( pid_t pid )
{
    bool result = false;
    int ret = 0;
    size_t sz = sizeof(ret);

    if ( sysctlbyname_with_pid( "sysctl.proc_native", pid,
                                &ret, &sz, NULL, 0 ) != -1 )
    {
        if ( ret == 0 )
            result = true;
    }

    return ( result );
}

#endif

#pragma mark -

extern "C" void DPPatchRemoteTask( pid_t pid, const char *pPathToPatch )
{
    Injector *pObj = NULL;

    // currently, Rosetta patching is only supported from IA-32
    // processes
#if defined(__i386__)
    if ( IsRosettaProcess( pid ) )
    {
        // use rosetta-handling injector
        pObj = new RosettaInjector( pid, pPathToPatch );
    }
    else
    {
#endif
        // use standard injector
        pObj = new Injector( pid, pPathToPatch );
#if defined(__i386__)
    }
#endif

    pObj->Inject( );
    delete pObj;
}
