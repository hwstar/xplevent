/*
* db.c
*
* Copyright (C) 2013 Stephen Rodgers
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*
*
* Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
*
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <talloc.h>
#include <time.h>
#include <errno.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "db.h"
#include "xplevent.h"

#define SQLITE_BHAND_MAX_RETRIES 10
#define DB_MAGIC 0x026DA723

typedef enum {TXTY_DEFERRED = 0, TXTY_IMMEDIATE, TXTY_EXCLUSIVE} txType_t;

typedef struct dbObj_s {
	unsigned magic;
	unsigned backoffMS;
	unsigned busyCount;
	sqlite3 *db;
} dbObj_t, *dbObjPtr_t;
	
	
typedef struct callbackData_s {
	TALLOC_CTX *ctx;
	String valueName;
	String res;
} callbackData_t;
	
typedef callbackData_t * callbackDataPtr_t;

/*
 * SQLITE Busy handler
 * 
 * This is registered when DBOpen is called.
 * 
 * Arguments
 * 
 * 1. Pointer to the database object.
 * 2. Number of times SQLITE has invoked this for the current transaction.
 * 
 * Return value:
 * 
 * TRUE if the operation is to continue, otherwise FALSE
 */
 

static int busyHandler(void *objPtr, int timesInvoked)
{
	dbObjPtr_t db = objPtr;
	struct timespec ts = (struct timespec){0, db->backoffMS * 100000};
	
	debug(DEBUG_EXPECTED,"db.c busyHandler invoked");
	
	if(timesInvoked > SQLITE_BHAND_MAX_RETRIES){
		return FALSE; /* Give up */
	}
	/* Wait for prescribed backoff time */

	
	for(;;){
		if(-1 == nanosleep(&ts, NULL)){
			if(EINTR == errno){
				continue;
			}
			else{
				debug(DEBUG_UNEXPECTED,"db.c busyHandler: nanosleep returned: %d", errno);
				return FALSE;
			}
		}
		break;
	}
			
	
	db->busyCount++;
	
	
	return TRUE;
	
		
}


/*
 * Log an error message and return FAIL
 * 
 * Arguments:
 * 
 * 1. Pointer to the database object
 * 2. Involing function name
 * 3. Message string
 * 
 * Return value:
 * 
 * Always FAIL
 */
 
static Bool logErr(dbObjPtr_t db, int line, const char *id, String msg)
{
	sqlite3_mutex *m;
	sqlite3 *sl3 = db->db;
	
	m = sqlite3_db_mutex(sl3);
	sqlite3_mutex_enter(m);
	String eStr = (String) sqlite3_errmsg(sl3);
	debug(DEBUG_UNEXPECTED, "File: %s, Line %d: %s: %s: %s", __FILE__, line, id, msg, eStr);
	sqlite3_mutex_leave(m);
	return FAIL;
}


/*
 * Perform a simple transaction which has no prepared statements.
 * 
 * Arguments:
 * 
 * 1. Pointer to the database object
 * 2. Invoking function name
 * 3. SQL string to execute
 * 4. Pointer to a place to store the result
 * 
 * Return value
 * 
 * PASS if successful, otherwise FAIL
 */
 

static Bool simpleTX(dbObjPtr_t db, const char *id, const char *sql, int *res)
{
	int r;
	sqlite3_stmt *stmt = NULL;
		
	/* Prepare */
	if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
		if(res){
			*res = r;
		}
		return logErr(db, __LINE__, id, "Error on sqlite3_prepare_v2()");
	}
	
	/* Step */
	if(stmt){
		if(SQLITE_DONE != (r = sqlite3_step(stmt))){
			logErr(db, __LINE__, id, "Error on sqlite3_step()");
		}
		sqlite3_reset(stmt);
	}

	/* Finalize */
	if(SQLITE_OK != (r = sqlite3_finalize(stmt))){
	
		if(res){
			*res = r;
		}
		debug(DEBUG_UNEXPECTED, "%s: Sqlite error on finalize: %s, error code = %d " , id, sql, r);
		return FAIL;
	}

	return PASS;
}

/*
* Begin a database transaction
*
* Arguments:
*
* 1. Pointer to the database handle
* 2. Text string for logging debug information.
*
* Returns:
*
* Boolean. Returns PASS if successful, FAIL if otherwise.
*/

static Bool dbTxBegin(dbObjPtr_t db, const char *id, txType_t txType)
{
	int res;
	static String beginTx = "BEGIN DEFERRED";
	static String beginImmedTx = "BEGIN IMMEDIATE";
	static String beginExclTx = "BEGIN EXCLUSIVE";
	String sql;
	switch(txType){
		case TXTY_DEFERRED:
			sql = beginTx;
			break;
			
		case TXTY_IMMEDIATE:
			sql = beginImmedTx;
			break;
			
		case TXTY_EXCLUSIVE:
			sql = beginExclTx;
			break;
			
		default:
			ASSERT_FAIL(0);
	}
	
	return simpleTX(db, id, sql, &res);
}

/*
* Commit or rollback a database transaction
*
* Arguments:
*
* 1. Pointer to the database handle
* 2. Text string for logging debug information.
* 3. Flag to indicate commit or rollback. PASS = commit, FAIL = rollback.
*
* Returns:
*
* PASS if successful, otherwise FAIL
*/

static Bool dbTxEnd(dbObjPtr_t db, const char *id, Bool type)
{
	int res;
	
	/* Transaction commit */
	if(type == PASS){
		return simpleTX(db, id, "COMMIT", &res);
	}
	/* Transaction rollback */
	else{
		return simpleTX(db, id, "ROLLBACK", &res);
	}
}


/*
 * Return a single result from a select.
 *
 * Arguments:
 *
 * 1. Generic pointer to database
 * 2. Talloc context to hang the result off of.
 * 3. ID String for debugging purposes
 * 4. String containing the name of the table to access
 * 5. String containing the name of the column for the key
 * 6. A String containing the key to look up
 * 7. The name of the column of the return value.
 *
 * Return Value:
 *
 * The value in the column referenced argument as a String.
 * A NULL indicates the record was not found, or an error occurred.
 * Result must be talloc_free'd when no longer required
 */

static const String dbReadField(dbObjPtr_t db, TALLOC_CTX *ctx, const char *id, String table, 
String keyColName, String key, String valueColName)
{
	int r, index ;
	String sql,p,q;
	sqlite3_stmt *stmt = NULL;

	
	/* Generate query */
	sql = talloc_asprintf(ctx , "SELECT %s AS result FROM %s WHERE %s=:key LIMIT 1", valueColName, table, keyColName);
	MALLOC_FAIL(sql);
	
	debug(DEBUG_INCOMPLETE, "%s: Sql = %s", id, sql);
	
	/* Parse Sql */
	if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
		logErr(db, __LINE__, id, "Error on sqlite3_prepare()");
		talloc_free(sql);
		return NULL;

	}
	/* Free sql string */
	talloc_free(sql);
	
	/* Bind the key */
	index = sqlite3_bind_parameter_index(stmt, ":key");
	r = sqlite3_bind_text(stmt, index, key, -1, SQLITE_TRANSIENT);
	
	if(SQLITE_OK == r){
		/* Execute the parsed statement */
		r = sqlite3_step(stmt);
		if(SQLITE_ROW == r){
			/* Extract value from column */
			p = (String) sqlite3_column_text(stmt, 0);
			if(p){
				debug(DEBUG_INCOMPLETE, "%s: sqlite3_column_text() returned result: %s", id, p );
				MALLOC_FAIL(q = talloc_strdup(ctx, p))
				sqlite3_finalize(stmt);
				return q;
			}
			logErr(db, __LINE__, id, "sqlite3_column_text() returned NULL");
			sqlite3_finalize(stmt);
			return NULL;
		}
		else{
			sqlite3_finalize(stmt);
			if(SQLITE_DONE == r){
				debug(DEBUG_INCOMPLETE, "%s: Record not found", id);
				return NULL;
			}
			else{
				logErr(db, __LINE__, id,"Error on sqlite3_step()");
				return NULL;
			}	
		}
	}
	else{
		sqlite3_finalize(stmt);
		logErr(db, __LINE__, id, "Error on sqlite_bind_text()");
		return NULL;
	}	
	
}

/*
 * Delete a row from a table
 *
 * Arguments:
 *
 * 1. Generic pointer to the database
 * 2. Talloc context to use for internal strings
 * 3. ID string for debugging purposes
 * 4. Name of the table as a string
 * 5. Name of the column to use with the key
 * 6. The key to be used to search for the record to delete.
 *
 * Return value:
 *
 * Boolean. PASS indicates success,  FAIL indicates failure.
 */
 
static Bool dbDeleteRow(dbObjPtr_t db, TALLOC_CTX *ctx, const char *id, String table, String colName, String key)
{
	String sql;
	Bool res = PASS;
	int r,index;
	sqlite3_stmt *stmt = NULL;
	
	
	sql = talloc_asprintf(ctx, "DELETE FROM %s WHERE %s=:key", table, colName);
	MALLOC_FAIL(sql)
	
	debug(DEBUG_INCOMPLETE, "%s: Sql = %s", id, sql);
	
	/* Parse Sql */
	if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
		talloc_free(sql);
		return logErr(db, __LINE__, id, "Error on prepare statement");
	}
	/* Release the SQL string */
	talloc_free(sql);
	
	/* Bind the key */
	index = sqlite3_bind_parameter_index(stmt, ":key");
	r = sqlite3_bind_text(stmt, index, key, -1, SQLITE_TRANSIENT);
	
	if(SQLITE_OK == r){
		/* Execute the parsed statement */
		r = sqlite3_step(stmt);
		if(SQLITE_DONE != r){
			res = logErr(db, __LINE__, id, "Error on step statement");
		}
	}
	else{
		res = logErr(db, __LINE__, id, "Error on sqlite_bind_text()");
	}	
	

	sqlite3_finalize(stmt);
	
	return res;
}

/*
* Open the database
*
* Arguments:
*
* 1. The path to the database file to open
*
* Return value:
*
* A generic pointer as the database handle, or NULL if there was an error.
*/

void *DBOpen(TALLOC_CTX *ctx, String file)
{
	dbObjPtr_t db;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(file)
	
	MALLOC_FAIL(db = talloc_zero(ctx, dbObj_t))
	db->magic = DB_MAGIC;
	db->backoffMS = 250;
	
	
	/* Open Database File */
	if(!access(file, R_OK | W_OK)){
		if((sqlite3_open_v2(file, &(db->db), SQLITE_OPEN_READWRITE, NULL))){
			debug(DEBUG_UNEXPECTED,"Sqlite file open error on file: %s", file);
			return NULL;
		}
	}
	else{
		debug(DEBUG_UNEXPECTED,"Sqlite file access error on file: %s", file);
		return NULL;
	}
	/* busyHandler(db, 0); Test call our handler */
	sqlite3_busy_handler(db->db, busyHandler, db);
	return (void *) db;
}


/* 
* Close the database
*
* Arguments:
*
* 1. A generic pointer to the database handle opened previously.
*
* Return Value:
*
* None
*/

void DBClose(void *dbObjPtr)
{
	dbObjPtr_t db = dbObjPtr;
	if(db){
		ASSERT_FAIL(DB_MAGIC == db->magic)
		sqlite3_close(db->db);
		db->magic = 0;
		talloc_free(db);
	}
}


/*
* Read a value from the nvstate table
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the search key.
*
* Return value:
*
* A string containing the value, or NULL if not found.
* Result must be talloc_free'd when no longer required
*/

const String DBReadNVState(TALLOC_CTX *ctx, void *dbObjPtr, const String key)
{


	String p = NULL;
	dbObjPtr_t db = dbObjPtr;

	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(key)
		
	/* Transaction start */
	if(dbTxBegin(db, __func__, TXTY_DEFERRED) != PASS){
		return NULL;
	}
		
	p = dbReadField(db, ctx, __func__, "nvstate", "key", key, "value");
	
	/* Transaction commit */
	
	dbTxEnd(db, __func__, PASS);

	return p;
}

/*
* Write a value to the nvstate table
*
* If the value already exists, it will be overwritten.
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the search key.
* 4. A String containing the value to be written.
*
* Return value:
*
* Boolean. PASS = success, FAIL = failure.
*/

Bool DBWriteNVState(TALLOC_CTX *ctx, void *dbObjPtr, const String key, const String value)
{
	Bool res = PASS;
	int r, index;
	dbObjPtr_t db = dbObjPtr;
	sqlite3_stmt *stmt = NULL;
	
	String sql = NULL;
	String p;
	time_t now;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(key)
	ASSERT_FAIL(value)
	
	time(&now);
		
	/* Transaction begin */
	
	if(dbTxBegin(db, __func__, TXTY_EXCLUSIVE) != PASS){
		return FAIL;
	}
	


	p = dbReadField(db, ctx, __func__, "nvstate", "key", key, "value");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, __func__, "nvstate", "key",  key);
	}
	
	if(PASS == res){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (key,value,timestamp) VALUES (:key,:value,'%lld')",
		"nvstate",(long long) now);
	
		MALLOC_FAIL(sql)
		
		debug(DEBUG_INCOMPLETE, "%s: Sql = %s", __func__, sql);
		
		/* Parse Sql */
		if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
			res = logErr(db, __LINE__, __func__, "Error on sqlite3_prepare_v2()");
		}
		/* Release the SQL string */
		talloc_free(sql);
		
		if(PASS == res){
			/* Bind the key */
			index = sqlite3_bind_parameter_index(stmt, ":key");
			r = sqlite3_bind_text(stmt, index, key, -1, SQLITE_TRANSIENT);
		
			/* Bind the value */
			if(SQLITE_OK == r){
				index = sqlite3_bind_parameter_index(stmt, ":value");
				r = sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
			}
			if(SQLITE_OK == r){
				/* Execute the parsed statement */
				r = sqlite3_step(stmt);
				if(SQLITE_DONE != r){
					res = logErr(db, __LINE__, __func__, "Error on sqlite3_step()");
				}
			}
			else{
				res = logErr(db, __LINE__, __func__, "Error on sqlite3_bind_text()");
			}	
	
			sqlite3_finalize(stmt);
		}
	}
	/* Transaction end */
	
	dbTxEnd(db, __func__, res);

	return res;
	
}

/*
* Fetch a script from the script table
*
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the script name
*
* Return value:
*
* A string containing the value, or NULL if not found.
* Result must be talloc_free'd when no longer required
*
*/


const String DBFetchScript(TALLOC_CTX *ctx, void *dbObjPtr, const String scriptName)
{

	String script = NULL;
	dbObjPtr_t db = dbObjPtr;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(scriptName)

	if(dbTxBegin(db, __func__, TXTY_DEFERRED) == FAIL){
		return NULL;
	}
	
	script = dbReadField(db, ctx, __func__, "scripts", "scriptname", scriptName, "scriptcode");
	
	dbTxEnd(db, __func__, PASS);
	
	return script;
}

/*
* Fetch script given trigger tag/subaddress
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the tag/subaddress.
*
* Return value:
*
* A string containing the value, or NULL if not found.
* Result must be talloc_free'd when no longer required
*
*/

const String DBFetchScriptByTag(TALLOC_CTX *ctx, void *dbObjPtr, const String tagSubAddr)
{

	String scriptName = NULL;
	String script = NULL;
	dbObjPtr_t db = dbObjPtr;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(tagSubAddr)

	if(dbTxBegin(db, __func__, TXTY_DEFERRED) == FAIL){
		return NULL;
	}
	
	scriptName = dbReadField(db, ctx, __func__, "trigaction", "source", tagSubAddr, "action");
	
	if(scriptName){
		script = dbReadField(db, ctx, __func__, "scripts", "scriptname", scriptName, "scriptcode");
		talloc_free(scriptName);
	}
	
	dbTxEnd(db, __func__, PASS);
	
	return script;
}


/*
* Update the trigger log
*
* If the source already exists, it will be overwritten.
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the source to be updated
* 4. A string containing the schema to be written
* 5. A String containing the nvpairs to be written
*
* Return value:
*
* Boolean. PASS = success, FAIL = failure.
*
* 
*/

Bool DBUpdateTrigLog(TALLOC_CTX *ctx, void *dbObjPtr, const String source, const String schema, const String nvpairs)
{
	Bool res = PASS;
	String sql = NULL;
	int r, index;
	sqlite3_stmt *stmt = NULL;
	dbObjPtr_t db = dbObjPtr;
	String p;
	time_t now;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(source)
	ASSERT_FAIL(schema)
	ASSERT_FAIL(nvpairs)
	
	time(&now);
		
	/* Transaction begin */
	
	if(dbTxBegin(db, __func__, TXTY_EXCLUSIVE) != PASS){
		return FAIL;
	}
	


	p = dbReadField(db, ctx, __func__, "triglog", "source", source, "nvpairs");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, __func__, "triglog", "source", source);
	}
	
	if(res == PASS){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (source,schema,nvpairs,timestamp) VALUES (:source,:schema,:nvpairs,'%lld')",
		"triglog",(long long) now);
	
		MALLOC_FAIL(sql)
		
		debug(DEBUG_INCOMPLETE, "%s: Sql = %s", __func__, sql);	
		
		/* Parse Sql */
		if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
			res = logErr(db,  __LINE__, __func__, "Error on sqlite3_prepare_v2()");
		}
		/* Release the SQL string */
		talloc_free(sql);
		
		if(PASS == res){
			/* Bind the source */
			index = sqlite3_bind_parameter_index(stmt, ":source");
			r = sqlite3_bind_text(stmt, index, source, -1, SQLITE_TRANSIENT);
		
			/* Bind the schema */
			if(SQLITE_OK == r){
				index = sqlite3_bind_parameter_index(stmt, ":schema");
				r = sqlite3_bind_text(stmt, index, schema, -1, SQLITE_TRANSIENT);
			}
			
			/* Bind the nvpairs */
			if(SQLITE_OK == r){
				index = sqlite3_bind_parameter_index(stmt, ":nvpairs");
				r = sqlite3_bind_text(stmt, index, nvpairs, -1, SQLITE_TRANSIENT);
			}
			
			if(SQLITE_OK == r){
				/* Execute the parsed statement */
				r = sqlite3_step(stmt);
				if(SQLITE_DONE != r){
					res = logErr(db, __LINE__, __func__, "Error on sqlite3_step()");
				}
			}
			else{
				res = logErr(db, __LINE__, __func__, "Error on sqlite3_bind_text()");
			}	
	
			sqlite3_finalize(stmt);
		}
	
	
	}
	
	/* Transaction end */
	
	dbTxEnd(db, __func__, res);

	return res;
	
}

/*
* Update the heartbeat log
*
* 
* If the source already exists, it will be overwritten.
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the source to be updated
*
* Return value:
*
* Boolean. PASS = success, FAIL = failure.
*
*/

Bool DBUpdateHeartbeatLog(TALLOC_CTX *ctx, void *dbObjPtr, const String source)
{
	Bool res = PASS;
	int r,index;
	sqlite3_stmt *stmt = NULL;
	dbObjPtr_t db = dbObjPtr;
	String sql = NULL;
	String p;
	time_t now;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(source)

	time(&now);
	
	/* Transaction begin */
	
	if(dbTxBegin(db, __func__, TXTY_EXCLUSIVE) != PASS){
		return FAIL;
	}
	
	p = dbReadField(db, ctx, __func__, "hbeatlog", "source", source, "source");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, __func__, "hbeatlog", "source", source);
	}
	
	if(res == PASS){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (source,timestamp) VALUES (:source,'%lld')",
		"hbeatlog", (long long) now);
	
		MALLOC_FAIL(sql)
		
		debug(DEBUG_INCOMPLETE, "%s: Sql = %s", __func__, sql);
				
		/* Parse Sql */
		if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
			res = logErr(db, __LINE__, __func__, "Error on sqlite3_prepare_v2()");
		}
		/* Release the SQL string */
		talloc_free(sql);
		
		if(PASS == res){
			/* Bind the source */
			index = sqlite3_bind_parameter_index(stmt, ":source");
			r = sqlite3_bind_text(stmt, index, source, -1, SQLITE_TRANSIENT);

			
			if(SQLITE_OK == r){
				/* Execute the parsed statement */
				r = sqlite3_step(stmt);
				if(SQLITE_DONE != r){
					res = logErr(db, __LINE__, __func__, "Error on sqlite3_step()");
				}
			}
			else{
				res = logErr(db, __LINE__, __func__, "Error on sqlite3_bind_text()");
			}	
			sqlite3_finalize(stmt);
		}
	
	}	

	/* Transaction end */
	
	dbTxEnd(db, __func__, res);

	return res;
	
}


/*
* Insert or replace script by script name
*
* If the source already exists, it will be overwritten.
*
* Arguments
*
* 1. A talloc context to hang the result off of.
* 2. A generic pointer to the database
* 3. A String containing the name of the script to be updated.
* 4. A string containing the script to be updated.
*
* Return value:
*
* Boolean. PASS = success, FAIL = failure.
*
*/

Bool DBIRScript(TALLOC_CTX *ctx, void *dbObjPtr, const String name, const String script)
{
	dbObjPtr_t db = dbObjPtr;
	Bool res = PASS;
	int i,index,r;
	unsigned scriptBufSize = 2048;
	unsigned scriptByteCount = 0;
	sqlite3_stmt *stmt = NULL;
	String scriptBuf;
	String sql = NULL;
	String p;

	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(name)
	ASSERT_FAIL(script)

	
	/* Copy the script into a new buffer and escape any single quote so that sqlite doesn't get confused */
	
	/* Allocate an initial buffer size */
	MALLOC_FAIL(scriptBuf = talloc_array(ctx, char, scriptBufSize))
	
	/* Copy script. Escape the single quote */
	for(i = 0; script[i]; i++){
		/* Grow the buffer if necessary */
		if(scriptBufSize - scriptByteCount <= 3){
			scriptBufSize <<= 1;
			MALLOC_FAIL(scriptBuf = talloc_realloc(ctx, scriptBuf, char, scriptBufSize))
		}
		/* Look for a single quote, and if found, and repeat it in the output buffer */
		if(script[i] == '\''){
			scriptBuf[scriptByteCount++] = '\'';
		}
		/* Copy the byte to the output buffer */
		scriptBuf[scriptByteCount++] = script[i];

	}
	
	/* Terminate the end of the new string */
	scriptBuf[scriptByteCount] = 0;
	
		
	/* Transaction begin */
	
	if(dbTxBegin(db, __func__, TXTY_EXCLUSIVE) != PASS){
		talloc_free(scriptBuf);
		return FAIL;
	}
	
	p = dbReadField(db, ctx, __func__, "scripts", "scriptname", name, "scriptcode");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, __func__, "scripts", "scriptname", name);
	}
	
	if(PASS == res){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (scriptname,scriptcode) VALUES (:scriptname,:scriptcode)",
		"scripts");
	
		MALLOC_FAIL(sql)
		
		debug(DEBUG_INCOMPLETE, "%s: Sql = %s", __func__, sql);	
		
		/* Parse Sql */
		if(SQLITE_OK != (r = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL))){
			res = logErr(db, __LINE__, __func__, "Error on sqlite3_prepare_v2()");
		}
		/* Release the SQL string */
		talloc_free(sql);
		
		if(PASS == res){
			/* Bind the script name */
			index = sqlite3_bind_parameter_index(stmt, ":scriptname");
			r = sqlite3_bind_text(stmt, index, name, -1, SQLITE_TRANSIENT);
		
			/* Bind the script code */
			if(SQLITE_OK == r){
				index = sqlite3_bind_parameter_index(stmt, ":scriptcode");
				r = sqlite3_bind_text(stmt, index, scriptBuf, -1, SQLITE_TRANSIENT);
			}

			if(SQLITE_OK == r){
				/* Execute the parsed statement */
				r = sqlite3_step(stmt);
				if(SQLITE_DONE != r){
					res = logErr(db, __LINE__, __func__, "Error on sqlite3_step()");
				}
			}
			else{
				res = logErr(db, __LINE__, __func__, "Error on sqlite3_bind_text()");
			}	
	
			sqlite3_finalize(stmt);
		}
	}
	
	/* Free our local copy of the script */
	talloc_free(scriptBuf);

	/* Transaction end */
	
	dbTxEnd(db, __func__, res);

	return res;
	
}

/*
 * Return a multiple results from a wildcard select.
 * 
 * Warning: Do not use this on large tables. Uses sqlite3_exec. Do not call with tainted data.
 *
 * Arguments:
 *
 *
 * 1. Talloc context to hang the result off of.
 * 2. Pointer to the database
 * 3. Data pointer to be passed to callback
 * 4. Table to be accessed
 * 5. A limit on the number of records to be returned through the callback function
 * 6. Callback function
 *
 *
 * Return Value:
 * 
 * PASS if successful, otherwise FAIL.
 *
 *
 */

Bool DBReadRecords(TALLOC_CTX *ctx, void *dbObjPtr,  void *data, String table, 
	unsigned limit, DBRecordCallBack_t callback)
{
	Bool res;
	String errorMessage;
	String sql = NULL;
	dbObjPtr_t db = dbObjPtr;

	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(DB_MAGIC == db->magic)
	ASSERT_FAIL(table)
	ASSERT_FAIL(callback)
	
	res = dbTxBegin(db, __func__, TXTY_DEFERRED);
	if(res == PASS)
		sql = talloc_asprintf(ctx , "SELECT * FROM %s LIMIT %u", table, limit);
		MALLOC_FAIL(sql);
		sqlite3_exec(db->db, sql, callback, data , &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"%s: Sqlite select error on select: ", __func__, errorMessage);
			sqlite3_free(errorMessage);
			res = FAIL;
		}
	
	dbTxEnd(db, __func__, res);
	
	if(sql){
		talloc_free(sql);
	}
	
	return res;		
	
}

/*
 * Return a field value by name
 * 
 * 
 * Arguments:
 * 1. List of field values as an array of strings
 * 2. List of column names as an array of strings
 * 3. Column name to return the value for
 * 
 * Return value:
 * 
 * String with value, or NULL if column name does not exist
 * 
 */

const String DBGetFieldByName(const String *argv, const String *colnames, const String colname)
{
	int i;
	ASSERT_FAIL(argv)
	ASSERT_FAIL(colnames)
	ASSERT_FAIL(colname)
	for(i = 0; colnames[i]; i++){
		if(!strcmp(colnames[i], colname))
			break;
	}
	if(colnames[i]){
		return argv[i];
	}
	return NULL;
}



/*
 * Generate an empty database file with the correct tables in it
 * 
 * 
 * Note: This function will refuse to overwrite an existing file unless the force flag is set
 * 
 * Arguments:
 * 
 * 1. A talloc context for transitory data
 * 2. The path to the file to generate
 * 3. Flag to force the overwriting of an existing file
 * 
 * Return value:
 * 
 * None
 */

void DBGenFile(TALLOC_CTX *ctx, String theFile, Bool forceFlag)
{
	sqlite3 *db;
	String errorMessage = NULL;
	String sql = NULL;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(theFile)
	
	if(!forceFlag){
		if(!access(theFile, F_OK)){
			fatal("File %s exists.", theFile);
		}
	}
	else{
		unlink(theFile);
	}

	/* Open a new database */
	if((sqlite3_open_v2(theFile, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL))){
			fatal("Sqlite3 file create error on file: %s", theFile);
	}
	
	/* Create heartbeat log table */
	sql = "CREATE TABLE \"hbeatlog\" (\"source\" TEXT NOT NULL,\"timestamp\" INTEGER NOT NULL);";
	sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	if(errorMessage){
		fatal("Sqlite create table error on hbeatlog: %s", errorMessage);
	}
	
	/* Create triglog table */
	sql = "CREATE TABLE \"triglog\" ( \"source\" TEXT NOT NULL, \"schema\" TEXT NOT NULL,\"nvpairs\" TEXT,\"timestamp\" INTEGER NOT NULL);";
	sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	if(errorMessage){
		fatal("Sqlite create table error on triglog: %s", errorMessage);
	}	
	
	/* Create nvstate table */
	sql = "CREATE TABLE nvstate (\"nvstateid\" INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\"key\" TEXT NOT NULL,\"value\" TEXT,\"timestamp\" INTEGER NOT NULL);";
	sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	if(errorMessage){
		fatal("Sqlite create table error on nvstate: %s", errorMessage);
	}	
	
	
	/* Create scripts table */
	sql = "CREATE TABLE \"scripts\" (\"scriptsid\" INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\"scriptname\" TEXT NOT NULL,\"scriptcode\" TEXT NOT NULL);";
	sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	if(errorMessage){
		fatal("Sqlite create table error on scripts: %s", errorMessage);
	}	

	/* Create trigaction table */
	sql = "CREATE TABLE trigaction (\"trigactionid\" INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\"source\" TEXT NOT NULL,\"action\" TEXT NOT NULL);";
	sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	if(errorMessage){
		fatal("Sqlite create table error on trigaction: ", errorMessage);
		}	
	

	/* Create schedule table */
	sql = "CREATE TABLE schedule (\"scheduleid\" INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\"name\" TEXT NOT NULL,\"param\" TEXT NOT NULL,\"scriptname\" TEXT NOT NULL);";
	sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	if(errorMessage){
		fatal("Sqlite create table error on schedule: ", errorMessage);
		}	

	/* Close the database */
	sqlite3_close(db);
	note("sqlite3 database file created successfully");	
}


	


