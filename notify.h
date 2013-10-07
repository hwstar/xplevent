/*
*    rc65 - an RC-65 RS-485 thermostat to xPL bridge
*    Copyright (C) 2012  Stephen A. Rodgers
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*/

#ifndef NOTIFY_H
#define NOTIFY_H

/* Error levels for debug. */
#define DEBUG_UNEXPECTED 1
#define DEBUG_EXPECTED 2
#define DEBUG_STATUS 3
#define DEBUG_ACTION 4
#define DEBUG_INCOMPLETE 5
#define DEBUG_MAX 5

// Call to set the program name, and initialize the guarding mutex
void notify_init(const String pgmName);

// Set the path to the log file
void notify_logpath(const String path);

// Call to set debugging level
void notify_set_debug_level(int dbglvl);

// Call to get debug level
int notify_get_debug_level(void);

// Call to enable or disable time reporting in messages
void notify_timen(Bool ena);

// Fatal error handler with strerror(errno);
void fatal_with_reason(int error, const String message, ...);

/* Fatal error handler. */
void fatal(const String message, ...);

/* Assertion failure handler */
void assertion_failure(const String file, int line);

/* Memory allocation failure handler */
void malloc_failure(const String file, int line);

/* Debugging handler. */
void debug(int level, const String message, ...);

/* Debugging handler with hexdump feature */

void debug_hexdump(int level, const void *buf, int buflen, const String message, ...);

/* Normal error handler. */
void error(const String message, ...);

/* Warning handler. */
void warn(const String message, ...);

/* Note handler. */
void note(const String message, ...);


#endif
