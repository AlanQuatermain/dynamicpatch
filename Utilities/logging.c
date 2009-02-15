/*
 *  logging.c
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <crt_externs.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <CoreFoundation/CoreFoundation.h>

// stuff for linking to Carbon library dynamically
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#include "logging.h"

// set this to 1 if you want logging to go via syslog
#define USE_SYSLOG      0

// Various folders we use here:
// this one is where the log files go
#define BASELOGDIR      "/Library/Logs/DynamicPatch"
#define DEBUG_ENABLER   "/Library/Logs/DynamicPatch/Debug"
#define NOTIFY_ENABLER  "/Library/Logs/DynamicPatch/Alert"

// debug & message info from any one app goes into
// 'BASELOGDIR/<appname>.log'

// error & emergency logs get echoed into this one, in addition to
// app-specific logfile
#define ERRORLOGFILE    "errors.log"

// allow this many files when rolling logs
#define kMaxFiles       5
// number of bytes in a log file when we roll it
#define kRollLogSize    (2048 * 1024)

#define kRolledLogStartMsg      "--- Start: Rolled log on: %s ---\n"
#define kRolledLogEndMsg        "--- End: Rolled log on: %s ---\n"
#define kRollFailedRenameMsg    "*** Error: Failed to roll log, rename error %d ***\n"

static int inited = 0;
static char app_name[256];
static char log_path[PATH_MAX];
static char error_path[PATH_MAX];
static char real_app_name[256];
static char real_app_path[256];

#pragma mark -

// hacky stuff
// if this is a Carbon app, we'll get LaunchCFMApp as the name from the
// usual APIs. If that's the case, we need to call CopyProcessName(),
// but I don't want to link everything against Carbon unnecessarily. So
// here, I look up the symbols if I need them.

/* // type for unique process identifier - from Processes.h
struct ProcessSerialNumber {
    unsigned long       highLongOfPSN;
    unsigned long       lowLongOfPSN;
};
typedef struct ProcessSerialNumber      ProcessSerialNumber;
*/
typedef OSStatus ( *GetCurrentProcessFn ) ( ProcessSerialNumber * );
typedef OSStatus ( *CopyProcessNameFn ) ( ProcessSerialNumber *, CFStringRef * );

static GetCurrentProcessFn my_GetCurrentProcess = NULL;
static CopyProcessNameFn my_CopyProcessName = NULL;

static int LinkCarbonFunctions( void )
{
    static int carbon_linked = 0;
    if ( !carbon_linked )
    {
        if ( ( NSIsSymbolNameDefined( "_GetCurrentProcess" ) ) &&
             ( NSIsSymbolNameDefined( "_CopyProcessName" ) ) )
        {
            NSSymbol getProcSym = NULL, copyNameSym = NULL;

            getProcSym = NSLookupAndBindSymbolWithHint( "_GetCurrentProcess", "Carbon" );
            copyNameSym = NSLookupAndBindSymbolWithHint( "_CopyProcessName", "Carbon" );

            if ( ( getProcSym != NULL ) && ( copyNameSym != NULL ) )
            {
                my_GetCurrentProcess = NSAddressOfSymbol( getProcSym );
                my_CopyProcessName = NSAddressOfSymbol( copyNameSym );

                // absolute paranoia
                if ( ( my_GetCurrentProcess != NULL ) &&
                     ( my_CopyProcessName != NULL ) )
                {
                    carbon_linked = 1;
                }
            }
        }
    }

    return ( carbon_linked );
}

#pragma mark -

static int DebugLogEnabled( void )
{
    int result = 0;
    struct stat statBuf;

    if ( stat( DEBUG_ENABLER, &statBuf ) != -1 )
        result = 1;

    return ( result );
}

static int EmergencyNotificationsEnabled( void )
{
    int result = 0;
    struct stat statBuf;

    if ( stat( NOTIFY_ENABLER, &statBuf ) != -1 )
        result = 1;

    return ( result );
}

static FILE * OpenFile( const char *pPath )
{
    FILE *pFile = fopen( pPath, "a+" );

    if ( pFile == NULL )
    {
        // have to create the file
        pFile = fopen( pPath, "w" );
    }

    // make sure ALL users can write to it
    chmod( pPath, 0666 );    // argh ! The beast, the beast !

    return ( pFile );
}

// okay, this one comes from Apple; it's the algorithm DirectoryService
// uses when rolling its own logs
static void RollLog( const char *pLogFile )
{
    int                     i               = 0;
    register ssize_t        lWrite          = 0;
    register struct tm *    tmPtr           = NULL;
    char *                  pBuff_1         = NULL;
    char *                  pBuff_2         = NULL;
    time_t                  seconds         = 0;
    int                     error           = 0;
    char                    dateStr[ 256 ];
    FILE *                  fFileRef        = NULL;
    size_t                  buffSize        = 0;
    int                     strSize         = 0;

    fFileRef = OpenFile( pLogFile );

    if ( fFileRef == NULL )
        return;

    seconds = time( NULL );

    // create temp buffers
    //  name of the file plus the new extension plus more
    buffSize = strlen( pLogFile ) + 1024;

    pBuff_1 = ( char * ) calloc( buffSize, sizeof( char ) );

    if ( pBuff_1 == NULL )
        return;

    pBuff_2 = ( char * ) calloc( buffSize, sizeof( char ) );

    if ( pBuff_2 == NULL )
        return;

    // Remove the oldest
    sprintf( pBuff_1, "%s.%d", pLogFile, kMaxFiles );

    // It may not exist so ignore any errors
    ( void ) remove( pBuff_1 );

    // Now we rename the files
    for ( i = ( kMaxFiles - 1 ); i >= 0; i-- )
    {
        // New name
        sprintf( pBuff_1, "%s.%d", pLogFile, i + 1 );

        // Old name
        if ( i == 0 )
            sprintf( pBuff_2, "%s", pLogFile );
        else
            sprintf( pBuff_2, "%s.%d", pLogFile, i );

        // Rename it
        // It may not exist so ignore the error except for the current file
        error = rename( pBuff_2, pBuff_1 );

        if ( ( error != 0 ) && ( i == 0 ) )
        {
            // log the error and bail
            sprintf( pBuff_1, kRollFailedRenameMsg, errno );
            lWrite = fwrite( pBuff_1, sizeof( char ), strlen( pBuff_1 ), fFileRef );

            if ( lWrite == -1 )
            {
                free( pBuff_1 );
                free( pBuff_2 );
                return;
            }

            fflush( fFileRef );

            free( pBuff_1 );
            free( pBuff_2 );
            return;
        }

        if ( i == 0 )
        {
            // Log the end tag
            tmPtr = localtime( ( time_t * ) &seconds );
            strftime( dateStr, 255, "%b %e %Y %X", tmPtr ); // Dec 25 1998 12:00:00

            sprintf( pBuff_1, kRolledLogEndMsg, dateStr );
            strSize = strlen( pBuff_1 );

            lWrite = fwrite( pBuff_1, sizeof( char ), strSize, fFileRef );
            if ( lWrite == -1 )
            {
                free( pBuff_1 );
                free( pBuff_2 );
                return;
            }

            fflush( fFileRef );
        }
    }

    // close the old file and open a new one
    fclose( fFileRef );
    fFileRef = OpenFile( pLogFile );

    // Tag the head of the new log
    sprintf( pBuff_1, kRolledLogStartMsg, dateStr );
    strSize = strlen( pBuff_1 );

    lWrite = fwrite( pBuff_1, sizeof( char ), strSize, fFileRef );
    if ( lWrite == -1 )
    {
        free( pBuff_1 );
        free( pBuff_2 );
        return;
    }

    fflush( fFileRef );

    // Free up the memory
    free( pBuff_1 );
    free( pBuff_2 );

    fclose( fFileRef );
}

static void PotentiallyRollLog( const char *pLogFile )
{
    struct stat statBuf;

    if ( stat( pLogFile, &statBuf ) != -1 )
    {
        if ( statBuf.st_size > kRollLogSize )
            RollLog( pLogFile );
    }
}

// called via atexit()
static void ExitLogs( void )
{
    // close our connection to the system log
    closelog( );
}

static void LogToFile( const char *pMessage, const char *pLogFile )
{
    FILE *pFile = NULL;

    if ( pLogFile != NULL )
    {
        PotentiallyRollLog( pLogFile );
        pFile = OpenFile( pLogFile );
    }

    if ( pFile != NULL )
    {
        int need_newline = 0;
        time_t now = 0;
        struct tm *tmTime = NULL;
        pid_t myPid = 0;
        int len = strlen( pMessage );

        if ( pMessage[ len - 2 ] != '\n' )
            need_newline = 1;

        now = time( NULL );
        tmTime = localtime( &now );
        myPid = getpid( );

        fprintf( pFile, "%04d-%02d-%02d %02d:%02d:%02d %s [%u] - ",
                 tmTime->tm_year + 1900, tmTime->tm_mon + 1, tmTime->tm_mday,
                 tmTime->tm_hour, tmTime->tm_min, tmTime->tm_sec, tmTime->tm_zone,
                 myPid );

        fprintf( pFile, "%s", pMessage );

        if ( need_newline )
            fprintf( pFile, "\n" );

        fflush( pFile );

        fclose( pFile );
    }
}

void NotifyEmergency( const char *pMessage )
{
    CFStringRef header = CFStringCreateWithFormat( NULL, NULL, CFSTR( "Emergency in %s" ), app_name );
    CFStringRef message = CFStringCreateWithCString( NULL, pMessage, kCFStringEncodingUTF8 );

    ( void ) CFUserNotificationDisplayNotice( 30.0, kCFUserNotificationStopAlertLevel, 
                                              NULL, NULL, NULL, header, message, NULL );
}

void EnsureLogFolder( void )
{
    struct stat statBuf;

    // we expect that /Library/Logs already exists
    // note that when we create our log folder we make sure it's
    // world-writable
    if ( stat( BASELOGDIR, &statBuf ) == -1 )
    {
        mkdir( BASELOGDIR, 0777 );
        chmod( BASELOGDIR, 0777 );
    }
}


#pragma mark -
#pragma mark === Public API ===

void InitLogs( const char * logName )
{
    // we allow callers to change the name we use to identify them
#if USE_SYSLOG
    // open syslog -- if not using syslog-only logging, then we
    // don't use openlog. That way, we don't undo anything caused
    // by the real application opening the syslog.
    openlog( logName, LOG_CONS | LOG_PID, LOG_DAEMON );
#else
    // set up paths according to log name
    // app name for logging purposes is whatever we were given
    strncpy( app_name, logName, 255 );
    app_name[ 255 ] = '\0';
#endif

    if ( !inited )
    {
#if USE_SYSLOG == 0
        // using our own logging facility
        // make sure our log folder exists
        EnsureLogFolder( );

        // log_path first
        sprintf( log_path, "%s/%s.log", BASELOGDIR, logName );

        // then error path
        sprintf( error_path, "%s/%s", BASELOGDIR, ERRORLOGFILE );
#endif
        atexit( ExitLogs );

        inited = 1;
    }
}

DP_API const char * ApplicationName( void )
{
    if ( real_app_name[ 0 ] == '\0' )
    {
        const char * pStr = NULL;
        unsigned int bufsize = PATH_MAX;
        ( void ) _NSGetExecutablePath( real_app_path, &bufsize );

        pStr = strrchr( real_app_path, '/' );
        if ( pStr != NULL )
            pStr++;
        else
            pStr = real_app_path;

        if ( strcmp( pStr, "LaunchCFMApp" ) )
        {
            // fiddly stuff - have to link to Carbon functions now
            // I don't link to Carbon normally, as I don't need it and it's too big
            //  to merit including just for this one simple purpose
            // However, since this appears to be in a CFM app, then by definition it's
            //  got to have the Carbon libraries loaded already, so there's almost no
            //  overhead to this call
            if ( LinkCarbonFunctions( ) )
            {
                ProcessSerialNumber psn;
                if ( my_GetCurrentProcess( &psn ) == noErr )
                {
                    CFStringRef procName;
                    if ( my_CopyProcessName( &psn, &procName ) == noErr )
                    {
                        // copy name into our app name buffer
                        CFStringGetCString( procName, real_app_name, 255,
                                            kCFStringEncodingASCII );
                        real_app_name[ 255 ] = '\0';
                        CFRelease( procName );
                    }
                }
            }
        }
        else
        {
            strcpy( real_app_name, pStr );
        }
    }

    return ( real_app_name );
}

#pragma mark -

DP_API void DebugLog( const char * format, ... )
{
    va_list args;

    va_start( args, format );
#if USE_SYSLOG
    vsyslog( LOG_DEBUG, format, args );
#else
    vDebugLog( format, args );
#endif
    va_end( args );
}

DP_API void LogMessage( const char * format, ... )
{
    va_list args;

    va_start( args, format );
#if USE_SYSLOG
    vsyslog( LOG_INFO, format, args );
#else
    vLogMessage( format, args );
#endif
    va_end( args );
}

DP_API void LogError( const char * format, ... )
{
    va_list args;

    va_start( args, format );
#if USE_SYSLOG
    vsyslog( LOG_ERR, format, args );
#else
    vLogError( format, args );
#endif
    va_end( args );
}

DP_API void LogEmergency( const char * format, ... )
{
    va_list args;

    va_start( args, format );
#if USE_SYSLOG
    vsyslog( LOG_CRIT, format, args );
#else
    vLogEmergency( format, args );
#endif
    va_end( args );
}

DP_API void NamedLog( const char * name, const char * format, ... )
{
    va_list args;
#if USE_SYSLOG
    char * buffer;
#endif

    va_start( args, format );

#if USE_SYSLOG
    buffer = asprintf( format, args );
    if ( buffer != NULL )
    {
        syslog( LOG_INFO, ":%s: %s", name, buffer );
        free( buffer );
    }
#else
    vNamedLog( name, format, args );
#endif

    va_end( args );
}

#pragma mark -

DP_API void vDebugLog( const char * format, va_list args )
{
    if ( ( inited ) && ( DebugLogEnabled( ) ) )
    {
        char *pMessage = NULL;

        vasprintf( &pMessage, format, args );

        if ( pMessage != NULL )
        {
            LogToFile( pMessage, log_path );
            free( pMessage );
        }
    }
}

DP_API void vLogMessage( const char * format, va_list args )
{
    if ( inited )
    {
        char *pMessage = NULL;

        vasprintf( &pMessage, format, args );

        if ( pMessage != NULL )
        {
            LogToFile( pMessage, log_path );
            free( pMessage );
        }
    }
}

DP_API void vLogError( const char * format, va_list args )
{
    if ( inited )
    {
        char *pMessage = NULL;
        // need a seperate buffer for the error log - need to identify the app in there
        char *pErrorMessage = NULL;

        vasprintf( &pMessage, format, args );

        if ( pMessage != NULL )
        {
            LogToFile( pMessage, log_path );

            asprintf( &pErrorMessage, "-%s- %s", app_name, pMessage );

            if ( pErrorMessage != NULL )
            {
                LogToFile( pErrorMessage, error_path );
                free( pErrorMessage );
            }

            free( pMessage );
        }
    }
}

DP_API void vLogEmergency( const char * format, va_list args )
{
    if ( inited )
    {
        char *pMessage = NULL;
        // need a seperate buffer for the error log - need to identify the app in there
        char *pErrorMessage = NULL;

        vasprintf( &pMessage, format, args );

        if ( pMessage != NULL )
        {
            LogToFile( pMessage, log_path );
            syslog( LOG_CRIT, "%s", pMessage ); // not a syslog-style emergency; just a severe/critical error

            asprintf( &pErrorMessage, "-%s- %s", app_name, pMessage );

            if ( pErrorMessage != NULL )
            {
                LogToFile( pErrorMessage, error_path );
                free( pErrorMessage );
            }

            if ( EmergencyNotificationsEnabled( ) )
                NotifyEmergency( pMessage );

            free( pMessage );
        }
    }
}

DP_API void vNamedLog( const char * name, const char * format, va_list args )
{
    if ( inited )
    {
        // 52 bytes for BASELOGDIR, NAME_MAX for filename, 1 byte for terminator
        char logpath[ 53 + NAME_MAX ];
        char *pMessage = NULL;

        // avoid buffer overruns, but don't go arbitrarily calling strlen() all the time
        // leave four bytes in buffer for '.log' to be appended
        snprintf( logpath, 49 + NAME_MAX, "%s/%s", BASELOGDIR, name );

        // append '.log'
        strcat( logpath, ".log" );

        // got path to logfile set up - now we just write to it
        vasprintf( &pMessage, format, args );

        if ( pMessage != NULL )
        {
            LogToFile( pMessage, logpath );
            free( pMessage );
        }
    }
}

#pragma mark -

DP_API void DebugLogMacro( const char * file, int line, const char * format, ... )
{
    if ( ( inited ) && ( DebugLogEnabled( ) ) )
    {
        va_list args;
        char *pMessage = NULL;
        char *pEntry = NULL;

        va_start( args, format );
        vasprintf( &pEntry, format, args );
        va_end( args );

        if ( pEntry != NULL )
        {
            const char *_file = FileFromFQPN( file );

            asprintf( &pMessage, "%s:%d : %s", _file, line, pEntry );

            if ( pMessage != NULL )
            {
                LogToFile( pMessage, log_path );
                free( pMessage );
            }

            free( pEntry );
        }
    }
}

DP_API const char * FileFromFQPN( const char * fqpn )
{
    const char * result = fqpn;
    const char * found = strchr( result, '/' );

    // have to use strchr() - strrchr() on a const string leaves no way to search before returned char
    while ( found != NULL )
    {
        if ( found[ 1 ] == '\0' )
            break;  // trailing slash - use previous instance

        // move chosen ptr to char after slash
        result = ++found;

        // see if there's a later one
        found = strchr( result, '/' );
    }

    // by now result will be either a ptr to a valid string or the original input value

    return ( result );
}
