/*
 *  newthread.h
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

#ifndef __DP_NEWTHREAD_H__
#define __DP_NEWTHREAD_H__

#include <pthread.h>
#include <mach/kern_return.h>
#include <mach/thread_act.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/ldsyms.h>
#include <sys/syslimits.h>

#include <sys/cdefs.h>

typedef void * ( *__pthread_start_fn ) ( void * );
typedef void ( *___pthread_set_self_fn ) ( pthread_t );
typedef int ( *___pthread_create_int_fn ) ( pthread_t, const pthread_attr_t *,
                                            void *, const mach_port_t );
typedef int ( *__pthread_create_fn ) ( pthread_t *, const pthread_attr_t *,
                                       __pthread_start_fn, void * );
typedef const void * ( *__loadimage_fn_ptr ) ( const char *, unsigned long );
typedef void * ( *__lookup_fn_ptr ) ( void *, const char * );
typedef void * ( *__lookup_sym_fn_ptr ) ( const char * );
typedef void * ( *__sym_addr_fn_ptr ) ( void * );
typedef kern_return_t ( *__thr_term_fn ) ( thread_act_t );
typedef thread_act_t ( *__thr_me_fn ) ( void );

typedef struct __newthread_args
{
    // we lookup the (static) addresses of some system functions here,
    // to save lots of hairy stuff in the injected code
    ___pthread_set_self_fn      setSelfFn;
    ___pthread_create_int_fn    createFakeFn;
    __pthread_create_fn         createPthreadFn;
    __loadimage_fn_ptr          addImageFn;
    __lookup_fn_ptr             lookupFn;
    __lookup_sym_fn_ptr         lookupSymFn;
    __sym_addr_fn_ptr           symAddrFn;
    __thr_term_fn               terminateFn;
    __thr_me_fn                 selfFn;

    // need ptr to stack base (to create fake pthread)
    void *                      stack_base;

    char fn_name[ 32 ];

    // these make the structure bigger than I'd like, but it's the only
    // reliable way of getting access to the starter function if the
    // patch library loads at a different address than we request.
    char lib_name[ PATH_MAX ];
    char patch_name[ PATH_MAX ];

    // need persistent storage for 'fake' pthread structure
    struct _opaque_pthread_t    fakeThread;
    pthread_attr_t              fakeAttrs;

} newthread_args_t;

__BEGIN_DECLS

void * PthreadStartFunction( void * arg );

void NewThreadStartFunction( __pthread_start_fn pthr_st_fn,
                             newthread_args_t *pArgs );

__END_DECLS

#endif  /* __DP_NEWTHREAD_H__*/
