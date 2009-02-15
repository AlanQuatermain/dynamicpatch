/*
 *  main.c
 *  DynamicPatch/iPatchTester
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

#include <stdio.h>
#include <DynamicPatch/DynamicPatch.h>
#include <ApplicationServices/ApplicationServices.h>
#include <mach-o/dyld.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

typedef int (*target_fn_t)( int );
static target_fn_t g_target_fn = (target_fn_t)0;

static int target_fn( int param )
{
    printf( "target_fn called with argument '%d'\n", param );
    return ( param + 10 );
}

static int patch_fn( int param )
{
    printf( "patch_fn called with argument '%d', incrementing.\n", param );
    param++;
    
    if ( g_target_fn != 0 )
        return ( g_target_fn( param ) );
    
    return ( param );
}

static void print_dwords( void * ptr, size_t s )
{
    size_t i;
    size_t sz = ( s / sizeof(unsigned int) );
    unsigned int *p = (unsigned int *) ptr;
    for ( i = 0; i < sz; i++ )
    {
        if ( (i % 4) == 0 )
            printf( "\n" );
        
        printf( " 0x%08x", p[i] );
    }
    
    printf( "\n" );
}

int main (int argc, const char * argv[])
{
    int arg = 23;
    printf( "Argument value is '23'\n\n" );
    
    printf( "target_fn = 0x%08x\n", (unsigned int) target_fn );
    printf( "instr at target_fn = 0x%08x\n", *( (unsigned int *) target_fn ) );
    
    int result = target_fn( arg );
    printf( "target_fn returned value '%d'\n", result );
    
    result = patch_fn( arg );
    printf( "patch_fn returned value '%d'\n\n", result );
    
    g_target_fn = DPCreatePatch( &target_fn, &patch_fn );
    
    if ( g_target_fn != NULL )
    {
        printf( "new instr at target_fn = 0x%08x\n", *( (unsigned int *) target_fn ) );
        printf( "new target_fn address is '0x%08x\n", (unsigned int) g_target_fn );
        //print_dwords( (void *) (g_target_fn - 8), 40 );
        
        result = target_fn( arg );
        printf( "target_fn returned value '%d'\n", result );
    }
    
    return ( 0 );
}
