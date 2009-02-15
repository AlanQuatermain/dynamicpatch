/*
 *  CreatePatch.c
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

#include <stdlib.h>
#include "logging.h"

// this is the per-architecture function that implements the patching.
// see ppc_patch.c or ia32_patch.c for details
extern void * __create_patch( void * target, void * patch );

void * DPCreatePatch( void * target, void * patch )
{
    void * result = NULL;

    DEBUGLOG( "CreatePatch() called..." );

    if ( (target != NULL) && (patch != NULL) )
    {
        result = __create_patch( target, patch );
    }
    else
    {
        LogError( "NULL values supplied to CreatePatch() ! target = %#x, patch = %#x",
                  (unsigned) target, (unsigned) patch );
    }

    return ( result );
}
