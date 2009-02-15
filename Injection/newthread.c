/*
 *  newthread.c
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

/*
 * The contents of this file are used purely to generate some assembler
 * and object code, which is then (fairly laboriously) transcribed into
 * the arrays in code_blocks.c. I use Epsilon for that, because it does
 * record & playback of macros, which are absolutely beautiful for that
 * sort of thing. Mmmmmmmmm, macros.....
 */

#include "newthread.h"

#if defined(__i386__)
# include <emmintrin.h>
#endif

typedef void (*startFn_t)( void );
typedef void (*specificFn_t)( const char * );

void * PthreadStartFunction( void * arg )
{
    newthread_args_t *pArgs = ( newthread_args_t * ) arg;

    const void * libThing = NULL;
    int using_dl = (pArgs->lookupFn != 0);
    startFn_t startFn = 0;

    // load the DynamicPatch framework
    if ( using_dl )
    {
        // using dlopen/dlsym:
        libThing = pArgs->addImageFn( pArgs->lib_name, 0x0A );
        if ( libThing != NULL )
            startFn = pArgs->lookupFn( libThing, pArgs->fn_name );
    }
    else
    {
        // using NSAddImage/NSLookupSymbolInImage/NSAddressOfSymbol
        libThing = pArgs->addImageFn( pArgs->lib_name, 0 );
        if ( libThing != NULL )
        {
            void * symObj = pArgs->lookupSymFn( pArgs->fn_name );
            if ( symObj != NULL )
                startFn = pArgs->symAddrFn( symObj );
        }
    }

    if ( startFn != 0 )
    {
        // see if we've got a specific patch to load
        if ( pArgs->patch_name[0] != '\0' )
            ((specificFn_t)startFn)( pArgs->patch_name );
        else
            startFn( );
    }

    return ( NULL );ing
}

void NewThreadStartFunction( __pthread_start_fn pthr_st_fn,
                             newthread_args_t *pArgs )
{
    pthread_t pthr;
    pthread_t fake;

    // set this first - to avoid bad memory dereferences
    // the structure we add here will be set up shortly.
    // for the moment it should be zeroed.
    fake = &pArgs->fakeThread;
    pArgs->setSelfFn( fake );
    // make a pthread structure for our kernel thread
    pArgs->createFakeFn( fake, &pArgs->fakeAttrs,
                         pArgs->stack_base, pArgs->selfFn( ) );

    // now that the above call makes us look & act like a pthread,
    //  start running in a *real* pthread
    // okay, so I'm paranoid
    // note that while _pthread_create requires a valid attribute
    // structure, pthread_create() does not; we can pass NULL to get
    // the default attributes
    pArgs->createPthreadFn( &pthr, NULL,
                            pthr_st_fn, ( void * ) pArgs );

    // we don't use pthread_exit, since we never got the chance to modify
    //  _pthread_count (which is a static variable inside pthread.c)
    pArgs->terminateFn( pArgs->selfFn( ) );
}

struct _r_args
{
    unsigned int *  addr;
    unsigned int    valu;
};

void RosettaPatchInstaller( struct _r_args *args,
                            unsigned int count,
                            void * flag_byte_addr )
{
    unsigned int i;
    for ( i = 0; i < count; i++ )
    {
        // value is big-endian already, addr is native
        *(args[i].addr) = args[i].valu;
#if defined(__ppc__) || defined(__ppc64__)
        __asm__ ( "isync" );
#elif defined(__i386__)
        _mm_clflush( (void *)(args[i].addr) );
#else
#error Unsupported architecture
#endif
    }

    *((unsigned char *) flag_byte_addr) = 0xFF;
    while ( 1 );
}
