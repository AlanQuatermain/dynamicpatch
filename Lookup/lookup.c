/*
 *  lookup.c
 *  DynamicPatch
 *
 *  Created by jim on 25/3/2006.
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

#include <string.h>
#include <nlist.h>
#include <stab.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/dyld.h>

// get architecture constants
#include "DynamicPatch.h"

// This file basically implements a cross-architecture nlist call.
// Originally it was written when I was interested in enumerating the
// contents of mach-o binary files on disk, and the code was
// re-purposed (with various other bits excised) for function lookup
// later. The reson I don't use the nlist() function call is because
// the process doing the looking doesn't necessarily have the library
// loaded that it wishes to search, and it doesn't necessarily run in
// the same architeture mode. This version lets the caller specify
// which architecture to search, and will byte-swap anything and
// everything necessary to implement that.

// Note that symbol addresses will always be returned in the byte order
// of the running architecture -- they *will* be byte-swapped if we're
// reading from an alien arch.


// if necesssary, ppc64 support could go here. But there are
// different file command structures for 64-bit ppc, wheras ia-32 &
// ppc32 both use the same structures. So, it'd need some macros or
// new types being defined to avoid awkward rewrites of the code
// above.

#if defined (__ppc__)
 static int native_arch = kInsertionArchPPC;
#elif defined(__i386__)
 static int native_arch = kInsertionArchIA32;
#else
 #error Unsupported architecture
#endif

static void * FindSymbolInSymtab( const char *pSymbol, const char *pBuffer,
                                  struct symtab_command *pSymtab,
                                  int exact, int swap )
{
    void * result = NULL;

    uint32_t symoff = pSymtab->symoff;
    uint32_t stroff = pSymtab->stroff;
    uint32_t nsyms  = pSymtab->nsyms;

    if ( swap )
    {
        symoff = OSSwapInt32(symoff);
        stroff = OSSwapInt32(stroff);
        nsyms  = OSSwapInt32(nsyms);
    }

    const char * bufPtr = pBuffer + symoff;
    struct nlist * pNlist = (struct nlist *) bufPtr;
    const char *pStringTable = pBuffer + stroff;
    int i = 0;

    for ( i = 0; i < nsyms; i++ )
    {
        // ignore debug symbols and undefined (imported) symbols
        if ( ((pNlist->n_type & N_STAB) == 0) &&
             ((pNlist->n_type & N_TYPE) != N_UNDF) )
        {
            uint32_t n_name = (uint32_t) pNlist->n_name;
            if (swap)
                n_name = OSSwapInt32(n_name);

            const char * pName = pStringTable + n_name;

            if ( exact == 0 )
            {
                if ( strstr( pName, pSymbol ) != NULL )
                {
                    // found it (well, presumably)
                    result = (void *) pNlist->n_value;
                    if ( swap )
                        result = (void *) OSSwapInt32( (uint32_t)result );
                    break;
                }
            }
            else
            {
                if ( strcmp( pName, pSymbol ) == 0 )
                {
                    result = (void *) pNlist->n_value;
                    if ( swap )
                        result = (void *) OSSwapInt32( (uint32_t)result );
                    break;
                }
            }
        }

        bufPtr += sizeof(struct nlist);
        pNlist  = (struct nlist *) bufPtr;
    }

    return ( result );
}

static void * FindSymbolInBuffer( const char *pSymbol, const char *pBuffer,
                                  int exact, int swap )
{
    void * result = NULL;

    const char * bufPtr = pBuffer;
    struct mach_header * pHeader = (struct mach_header *) bufPtr;
    bufPtr += sizeof(struct mach_header);
    struct load_command *pLoadCommand = (struct load_command *) bufPtr;
    int i = 0, ncmds = pHeader->ncmds;

    if ( swap )
        ncmds = OSSwapInt32(ncmds);

    for ( i = 0; i < ncmds; i++ )
    {
        uint32_t cmd = pLoadCommand->cmd;
        uint32_t cmdsize = pLoadCommand->cmdsize;

        if ( swap )
        {
            cmd = OSSwapInt32(cmd);
            cmdsize = OSSwapInt32(cmdsize);
        }

        if ( ( cmd & 0x7fffffff ) == LC_SYMTAB )
        {
            result = FindSymbolInSymtab( pSymbol, pBuffer,
                                         (struct symtab_command *) pLoadCommand,
                                         exact, swap );

            if ( result != NULL )
                break;
        }

        bufPtr += cmdsize;
        pLoadCommand = (struct load_command *) bufPtr;
    }

    return ( result );
}

static void * FindSymbolInFile( const char *pSymbol, const char *pFile,
                                int exact, int arch )
{
    void * result = NULL;
    off_t fileSize = 0;
    char * contentsBuffer = NULL;

    int fd = open( pFile, O_RDONLY, 0 );

    if ( fd != -1 )
    {
        struct stat statBuffer;

        if ( stat( pFile, &statBuffer ) != -1 )
        {
            fileSize = statBuffer.st_size;

            contentsBuffer = ( char * ) mmap( ( caddr_t ) 0, fileSize, PROT_READ,
                                              MAP_FILE | MAP_PRIVATE, fd, ( off_t ) 0 );
        }
    }

    if ( contentsBuffer != NULL )
    {
        // handle FAT binaries
        // look at the magic number
        unsigned long obj_magic = *( ( unsigned long * ) contentsBuffer );

        if ( obj_magic == FAT_MAGIC )
        {
            // fat header, native architecture
            // find the 32-bit stuff (no 64-bit until I get a Tiger dev machine)
            int i;
            struct fat_header *pHeader = ( struct fat_header * ) contentsBuffer;
            struct fat_arch *pArch = ( struct fat_arch * ) ( contentsBuffer + sizeof( struct fat_header ) );
            for ( i = 0; i < pHeader->nfat_arch; i++ )
            {
                if ( ( pArch[i].cputype == CPU_TYPE_POWERPC ) && ( arch == kInsertionArchPPC ) )
                {
                    result = FindSymbolInBuffer( pSymbol, contentsBuffer + pArch[i].offset, 
                        exact, (arch != native_arch) );
                    break;
                }
                else if ( ( pArch[i].cputype == CPU_TYPE_I386 ) && ( arch == kInsertionArchIA32 ) )
                {
                    result = FindSymbolInBuffer( pSymbol, contentsBuffer + pArch[i].offset,
                        exact, (arch != native_arch) );
                    break;
                }
            }
        }
        else if ( obj_magic == FAT_CIGAM )
        {
            // fat header, byteswapped architecture
            int i;
            struct fat_header *pHeader = (struct fat_header *) contentsBuffer;
            struct fat_arch *pArch = (struct fat_arch *)(contentsBuffer + sizeof(struct fat_header));
            uint32_t nfat_arch = OSSwapConstInt32(pHeader->nfat_arch);
            for ( i = 0; i < nfat_arch; i++ )
            {
                cpu_type_t cputype = OSSwapConstInt32(pArch[i].cputype);
                uint32_t offset =  OSSwapConstInt32(pArch[i].offset);

                if ( ( cputype == CPU_TYPE_POWERPC ) && ( arch == kInsertionArchPPC ) )
                {
                    result = FindSymbolInBuffer( pSymbol, contentsBuffer + offset,
                        exact, (arch != native_arch) );
                    break;
                }
                else if ( ( cputype == CPU_TYPE_I386 ) && ( arch == kInsertionArchIA32 ) )
                {
                    result = FindSymbolInBuffer( pSymbol, contentsBuffer + offset,
                        exact, (arch != native_arch) );
                    break;
                }
            }
        }
        else if ( ( obj_magic == MH_MAGIC ) && ( arch == native_arch ) )    // only works if looking for native
        {
            result = FindSymbolInBuffer( pSymbol, contentsBuffer, exact, 0 );
        }
    }

    if ( contentsBuffer != NULL )
    {
        munmap( contentsBuffer, fileSize );
    }

    if ( fd != -1 )
    {
        close( fd );
    }

    return ( result );
}

// this is a wrapper function which will ultimately call
// FindSymbolInFile() above. If the pHeader argument is not given, then
// pModule is assumed to be a FQPN, and is handed off directly.
// Otherwise, it looks through the load commands following the header
// to determine the path to the binary. Note that in this case, this
// function also determines whether the header belongs to the right
// library, and may be called once for each loaded library in the
// application.
// NB: This does not handle things like @executable_path. For built-in
// libraries, please pass a FQPN.
static void * FindSymbolInModule( const char *pSymbol, const char *pModule,
                                  const struct mach_header *pHeader,
                                  int exact, int arch )
{
    void * result = NULL;

    if ( pHeader == NULL )
    {
        // it's a fully-qualified path to the binary - just load it
        result = FindSymbolInFile( pSymbol, pModule, exact, arch );
    }
    else if ( pHeader->filetype == MH_DYLIB )
    {
        char * bufPtr = ( char * ) pHeader;
        bufPtr += sizeof( struct mach_header );
        struct load_command *pLoadCmd = ( struct load_command * ) ( bufPtr );
        int i = 0;

        for ( i = 0; i < pHeader->ncmds; i++ )
        {
            if ( ( pLoadCmd->cmd & 0x7fffffff ) == LC_ID_DYLIB )
            {
                char *pFoundModuleName = NULL;
                char *pLibFile = ( char * ) pLoadCmd;
                struct dylib_command *pCmd = ( struct dylib_command * ) pLoadCmd;
                pLibFile += pCmd->dylib.name.offset;

                if ( pLibFile != NULL )
                {
                    pFoundModuleName = strrchr( pLibFile, '/' );

                    if ( pFoundModuleName != NULL )
                        pFoundModuleName++;
                    else
                        pFoundModuleName = pLibFile;

                    if ( strncmp( pFoundModuleName, pModule, strlen( pFoundModuleName ) ) == 0 )
                    {
                        result = FindSymbolInFile( pSymbol, pLibFile, exact, arch );

                        if ( result != NULL )
                            break;
                    }
                }
            }

            bufPtr += pLoadCmd->cmdsize;
            pLoadCmd = ( struct load_command * ) bufPtr;
        }
    }

    return ( result );
}

#pragma mark -

// implementation function used by all the public lookup routines.
static void * _FindFunctionForArchitecture( const char *pName, const char *pModule,
                                            int exact, int arch )
{
    void * result = NULL;
    const struct mach_header *pCurrentImageHeader = NULL;
    long i = 0;
    long count = _dyld_image_count( );

    if ( pModule[ 0 ] == '/' )
    {
        // fully-qualified path to module - don't search memory for it, just load
        result = FindSymbolInModule( pName, pModule, NULL, exact, arch );
    }
    else
    {
        for ( i = 0; i < count; i++ )
        {
            pCurrentImageHeader = _dyld_get_image_header( ( unsigned long ) i );

            if ( pCurrentImageHeader != NULL )
            {
                result = FindSymbolInModule( pName, pModule, pCurrentImageHeader,
                                             exact, arch );

                if ( result != NULL )
                {
                    break;
                }
            }
        }
    }

    return ( result );
}

#pragma mark -

// uses strcmp() to find an exact match
void * DPFindFunctionAddress( const char *pFunctionName, const char *pModuleName )
{
    return ( _FindFunctionForArchitecture( pFunctionName, pModuleName, 1, native_arch ) );
}

// uses strstr() to find a symbol containing the name -- a quick &
// nasty hack for C++ mangled names. Deprecated, kinda.
void * DPFindVagueFunctionAddress( const char *pExactFunctionName, const char *pModuleName )
{
    return ( _FindFunctionForArchitecture( pExactFunctionName, pModuleName, 0, native_arch ) );
}

// always uses strcmp(), allows caller to specify which architecture to
// search
void * DPFindFunctionForArchitecture( const char *pName, const char *pModule, int arch )
{
    return ( _FindFunctionForArchitecture( pName, pModule, 1, arch ) );
}
