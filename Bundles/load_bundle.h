/*
 *  load_bundle.h
 *  DynamicPatch
 *
 *  Created by jim on 24/3/2006.
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

#ifndef __DP_LOAD_BUNDLE_H__
#define __DP_LOAD_BUNDLE_H__

/*!
 @header Bundle Utilities
 @discussion The functions defined here are used to load patch bundles
         in a 'native' patching environment. The Rosetta patch loader
         by necessity uses a different method.
 @copyright 2004-2006 Jim Dovey. Some Rights Reserved.
 @author Jim Dovey
 */

#include <CoreFoundation/CFBundle.h>

#include <sys/cdefs.h>

__BEGIN_DECLS

/*!
 @function load_patch_bundles
 @abstract Look in the standard place for patches and load them.
 @discussion This is used when no specific patch path has been
         supplied. It will look through the contents of a predefined
         folder (e.g. /Library/Application Support/DynamicPatch/) for
         bundles with a predefined file extension (e.g. .patch), and
         will load each of them, calling their initialization routines.
 */
void load_patch_bundles( void );

/*!
 @function load_patch_bundle
 @abstract Load and launch a patch bundle at a specified path.
 @discussion Use this to load a specific bundle, for instance one
         located within the resource folder of your application bundle.

         The bundle's file suffix is not expected to be any particular
         value.
 @param pPatchToLoad The fully-qualified POSIX path to the patch bundle.
 */
void load_patch_bundle( const char *pPatchToLoad );

/*!
 @function list_patch_bundles
 @abstract Returns a list of patch bundles in the standard folder.
 @discussion This function is public purely for the benefit of the
         Rosetta code, which does different things with the bundles
         once they've been identified. If no specific bundle is
         provided, it will call this function to generate a list of all
         bundles it might conceivably load, in the same manner as
         @link load_patch_bundles load_patch_bundles @/link above.
 @result An array containing CFBundleRef objects.
 */
CFArrayRef list_patch_bundles( void );

__END_DECLS

#endif  /* __DP_LOAD_BUNDLE_H__ */
