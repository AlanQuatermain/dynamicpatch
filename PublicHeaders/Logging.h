/*
 *  Logging.h
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

#ifndef __DP_LOGGING_H__
#define __DP_LOGGING_H__

#include "DPAPI.h"

#include <stdarg.h>

/*!
 @header Logging Utilities
 @abstract The DynamicPatch framework's integrated logging interface.
 @discussion There are four main classes of logging: Debug, Messages, Errors,
        and Emergencies. Appended to this is the ability to write to a log with
        a given name.

        NB: By default, this API works as described below. However, there
        is a preprocessor flag which will make it use the syslog
        functions instead.

        Each log type conditionally writes to one or more files. In addition, 
        the Emergency log routines may optionally put up a general alert dialog.
        This behaviour is user-modifiable.

        Each of the four main log operations will write to a logfile whose name
        takes the format '[name].log', where [name] is the value provided to the
        @link InitLogs InitLogs @/link function. In addition, the Error and
        Emergency log operations will write the same data, along with the 
        specified name, to a file called 'errors.log'.

        All logfiles will be automatically rolled when they reach 2Mb in size.
        There will be a maximum of five logfiles (named xxx.log, xxx.log.1, etc.)
        for each filename in existence at any one time. As such, each application
        can potentially use up to 10Mb of disk space for logs. However, a cron
        job (such as that used on /var/log) can easily clean up or compress old
        files, if required.

        The Debug log will not print information to any logfile by default. To
        enable output from the DebugLog function, the end user must create a
        folder called 'Debug' within the log folder (currently
        '/Library/Logs/DynamicPatch'). Similarly, the Emergency log will not put
        up user alerts by default, but can be enabled by creating a folder
        called 'Notify' within the same log folder.

        In addition to printing information to these logfiles,  LogEmergency will
        write to the syslog using the LOG_CRIT priority. Emergency-level logs are
        generally not expected to happen, and should only be use in dire need.

        Since emergencies get filed to the system log as critical errors, messages
        will appear in all command-line terminals in the event of an emergency, and
        those terminal will in that case issue a beep.

        For assistance in providing useful debugging information, it is recommended
        that the debug log be accessed through the @link DEBUGLOG DEBUGLOG @/link
        macro. That macro will feed file/line information into the debug log, to
        aid in tracking down errors. This is especially useful when dealing with
        repetitive network communication handlers, where a general error message
        of "Server returned error: -5006" doesn't help to locate the associated
        code, but where it isn't desirable to type detailed location information
        with slight differences a hundred times over.

        A side benefit of these logging functions is access to an authoritative
        application name, where that might be useful; for instance, in a loadable
        bundle or framework. The @link ApplicationName ApplicationName @/link
        function will store the application name in a static buffer on first
        call, and will return the address of this buffer to the caller. In addition,
        it is CFM-aware: if it detects a Unix application name of 'LaunchCFMApp', it
        will dynamically bind to the Carbon framework, which by definition should 
        already be loaded, and will use the CopyProcessName function to get the
        real name of the application.

        <b>Usage:</b><br />
        Before calling any logging functions, call @link InitLogs InitLogs @/link,
        passing the name of your application (or the desired name for your logfile,
        if different). Until this call is made, no logging functions will work. 

        After initialization, use the basic logging functions as you would printf,
        and if necessary, you can use the va_list versions to perform part of the 
        processing yourself, or to feed your own vararg logging routines through 
        this API.

        It is not necessary to manually close the logging system. It installs an 
        exit handler to release any allocated resources. Currently, the only
        action taken on exit is to close the system log, but this is potentially
        subject to change.

 @ignore DP_API
*/

// see the implementation file to switch this between logging to a
// folder and using syslog.

/*! @functiongroup General Utilities */
/*!
 @function InitLogs
 @abstract Initialize the logging subsystem.
 @discussion This will ensure that the appropriate logging folders are created, and
        that they have the correct permissions. It will also fill in some static 
        string buffers with the paths for each logfile, and will open the system
        log using the provided name.

        This can be called more than once; in that eventuality, the
        stored log name will be updated to use the new value.
 @param logName The name for the logfile to which messages are to be written.
*/
DP_API void InitLogs( const char * logName );

/*!
 @function ApplicationName
 @abstract Get the name of the current application.
 @discussion This function will get the name of the current application by calling
        _NSGetExecutablePath(). If that indicates a value of 'LaunchCFMApp', then
        this function will look up and bind to the Carbon library's CopyProcessName()
        function, and will use that to find out the application name instead.

        The value is looked up once and written to a static buffer. On subsequent 
        calls, the address of the buffer is simply returned as-is.
 @result Returns the address of a static buffer containing the name of the current
        application. The caller should not free this pointer.
*/
DP_API const char * ApplicationName( void );

#pragma mark -

/*! @functiongroup Basic Logging API */
/*!
 @function DebugLog
 @abstract Log information useful when debugging.
 @discussion This will write data to the application's logfile; however, it will
        only do this if a file/folder called 'Debug' exists in the log folder
        (/Library/Logs/DynamicPatch).
 @param format A printf-style format string.
*/
DP_API void DebugLog( const char * format, ... );

/*!
 @function LogMessage
 @abstract Log general operational information.
 @discussion This will write data to the application's logfile. It will also pass
        the same message onto the system log, using the LOG_DEBUG priority.
 @param format A printf-style format string.
*/
DP_API void LogMessage( const char * format, ... );

/*!
 @function LogError
 @abstract Log information about an error.
 @discussion This will write data to the application's logfile, and also to the
        global error logfile. It also passes the same message onto the system
        log, using the LOG_ERR priority. As such, the message will be written to
        disk three times, and will appear in the system logfile.

        It is recommended that this function only be used to print out information
        on errors which are likely to adversely affect the operation of a software
        component. If an error is recoverable, or not likely to have a readily
        apparent effect to the end user, then it is probably safest to use
        @link LogMessage LogMessage @/link instead. However, if the error really
        should never happen, even if recoverable, this is probably the best place
        to put it.
 @param format A printf-style format string.
*/
DP_API void LogError( const char * format, ... );

/*!
 @function LogEmergency
 @abstract Log information about a critical error.
 @discussion This will write data to both the application's logfile and the global
        error logfile, and also to the system log, at the LOG_CRIT priority. As
        such, it gets replicated three times on disk, sent to all open terminals, 
        and causes a system beep. It may also, if the folder 'Notify' exists in
        the log folder, put up a user-level alert dialog containing the given message.

        It is highly recommended that this function not be used. Instead, it is 
        suggested to simply Write Your Software Well, So It Handles Errors. You 
        should use this routine only if a software component has encountered a 
        disastrous circumstance, and therefore cannot continue running, causing
        an obvious error and a failure of a larger system. For
        instance, there are only a few calls to this in the patching
        API, and they are always when something very much unexpected
        happens which will hobble the whole thing, such as being unable
        to allocate memory for the patch jump tables, or being unable
        to reset protection on a memory region.

        In short: You Have Been Warned.
 @param format A printf-style format string.
*/
DP_API void LogEmergency( const char * format, ... );

/*!
 @function NamedLog
 @abstract Log information to a specifically-named logfile.
 @discussion This will write data only to a logfile with the name specified. It is 
        unique among these logging functions in that it doesn't require a prior call
        to @link InitLogs InitLogs @/link in order to function.

        In syslog mode, this will print a LOG_INFO message with the
        given name prepended.
 @param name The name of the logfile; a name of 'bob' will result in a file called
        'bob.log'.
 @param format A printf-style format string.
*/
DP_API void NamedLog( const char * name, const char * format, ... );

#pragma mark -

/*! @functiongroup Vararg Logging API */
// these functions actually implement the logging - they can be called by other vararg functions
// if using syslog, these calls are all unused

/*! @function vDebugLog See @link DebugLog DebugLog @/link for details. */
DP_API void vDebugLog( const char * format, va_list args );

/*! @function vLogMessage See @link LogMessage LogMessage @/link for details. */
DP_API void vLogMessage( const char * format, va_list args );

/*! @function vLogError See @link LogError LogError @/link for details. */
DP_API void vLogError( const char * format, va_list args );

/*! @function vLogEmergency See @link LogEmergency LogEmergency @/link for details. */
DP_API void vLogEmergency( const char * format, va_list args );

/*! @function vNamedLog See @link NamedLog NamedLog @/link for details. */
DP_API void vNamedLog( const char * name, const char * format, va_list args );

#pragma mark -

/*! @functiongroup Helper Routines */

/*!
 @function DebugLogMacro
 @discussion Helper function for the @link DEBUGLOG DEBUGLOG @/link macro. 
        Takes a file/line argument and prepends their data to the format string 
        whilst writing the log message. It will trim the <code>file</code> argument
        to just the last path component, rather than printing the entire path in the
        logfile.

        This calls through to @link vDebugLog vDebugLog @/link to write data.
 @param file Filename; usually provided by the __FILE__ macro.
 @param line Line number; usually provided by the __LINE__ macro.
 @param format A print-style format string.
*/
DP_API void DebugLogMacro( const char * file, int line, const char * format, ... );

/*!
 @defined DEBUGLOG
 @abstract Convenience for accessing the @link DebugLog DebugLog @/link function.
 @discussion It is advised that this macro be used to implement debug logging, as it
        will embed file and line number information into the debug log.
 @param format A printf-style format string.
*/
#define DEBUGLOG( format, args... ) DebugLogMacro( __FILE__, __LINE__, format, ##args )

/*!
 @function FileFromFQPN
 @abstract Get the last path component of an FQPN.
 @discussion This will scan the string for the last instance of a path delimiter, and
        return a pointer to the next character after that value. It checks internally
        for trailing delimiters, and ensures that the value returns is a valid name.
 @param fqpn A fully qualified path name, in POSIX format (using '/' as a path delimiter).
 @result A pointer to the last path component, within the given string.
*/
DP_API const char * FileFromFQPN( const char * fqpn );

#endif  /* __DP_LOGGING_H__ */
