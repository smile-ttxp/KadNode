
#ifndef _LOG_H_
#define _LOG_H_

#include <syslog.h>

// Verbosity levels
#define VERBOSITY_QUIET 0
#define VERBOSITY_VERBOSE 1
#define VERBOSITY_DEBUG 2

#define log_err(...) if( _log_check(LOG_ERR) ) { _log_print(LOG_ERR, __VA_ARGS__); }
#define log_info(...) if( _log_check(LOG_INFO) ) { _log_print(LOG_INFO, __VA_ARGS__); }
#define log_warn(...) if( _log_check(LOG_WARNING) ) { _log_print(LOG_WARNING, __VA_ARGS__); }
#define log_debug(...) if( _log_check(LOG_DEBUG) ) { _log_print(LOG_DEBUG, __VA_ARGS__); }


// Get milliseconds since program start as string
char *log_time();

void log_setup( void );
void log_free( void );

// Check if the log message is going to be printed
int _log_check( int priority );

// Print a log message
void _log_print( int priority, const char format[], ... );

#endif // _LOG_H_
