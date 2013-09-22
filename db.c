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
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "db.h"
#include "xplevent.h"

typedef struct callbackData_s {
	TALLOC_CTX *ctx;
	String valueName;
	String res;
} callbackData_t;
	
typedef callbackData_t * callbackDataPtr_t;


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

static Bool dbTxBegin(void *db, String id)
{
	String errorMessage;
	
	/* Transaction start */
	sqlite3_exec((sqlite3 *) db, "BEGIN TRANSACTION", NULL, NULL, &errorMessage);
	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite error on %s begin tx: %s", id, errorMessage);
		sqlite3_free(errorMessage);
		return FAIL;
	}
	return PASS;
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
* None
*/

static void dbTxEnd(void *db, String id, Bool type)
{
	String errorMessage;
	
	/* Transaction commit */
	if(type == PASS){
		sqlite3_exec((sqlite3 *)db, "COMMIT TRANSACTION", NULL, NULL, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite error on %s commit : %s", id, errorMessage);
			sqlite3_free(errorMessage);
			return;
		}
	}
	/* Transaction rollback */
	else{
		sqlite3_exec((sqlite3 *) db, "ROLLBACK TRANSACTION", NULL, NULL, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite rollback error on %s: %s", id, errorMessage);
			sqlite3_free(errorMessage);
			return;
		}
	}
}


/*
 * DBReadField callback function
 *
 * This is a standard SQLITE callback funcion used with an dbReadField function which
 * stores the selected result in a data structure for use by the caller of dBReadField.
 * This function looks for a field name match, and if found, allocates a String to hold
 * the value of the field and inserts it into the data structure passed in.
 * 
 * Arguments:
 *
 * 1. Generic pointer to callback data structure filled in by dbReadField.
 * 2. Number of fields.
 * 3. An array of field value strings filled in by SQLite.
 * 4. An array of column names filled in by SQLite. NULL-terminated.
 *
 * Return value:
 * Integer. Always 0.
 */

static int dbReadFieldCallback(void *objptr, int argc, String *argv, String *colnames)
{
	callbackDataPtr_t cbd = objptr;
	int i;
	
	ASSERT_FAIL(objptr)
	ASSERT_FAIL(cbd->ctx)
	ASSERT_FAIL(cbd->valueName)
	
	/* Find index to column */
	
	for(i = 0; colnames[i]; i++){
		if(!strcmp(colnames[i], cbd->valueName)){
			break;
		}
	}
	if(colnames[i] && i < argc){ /* If a result was found */
		cbd->res = talloc_strdup(cbd->ctx, argv[i]);
		MALLOC_FAIL(cbd->res);
	
	}
	
	return 0;
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
 * The value in the column referenced argument 7 as a String.
 * A NULL indicates the record was not found.
 * Result must be talloc_free'd when no longer required
 */

static const String dbReadField(void *db, TALLOC_CTX *ctx, String id, String table, 
String keyColName, String key, String valueColName)
{
	String errorMessage;
	String sql;
	callbackData_t cbd;
	
	
	ASSERT_FAIL(table)
	ASSERT_FAIL(keyColName)
	ASSERT_FAIL(key)
	ASSERT_FAIL(valueColName)
	ASSERT_FAIL(id)
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	
	cbd.valueName = valueColName;
	cbd.res = NULL;
	cbd.ctx = ctx;
	
	sql = talloc_asprintf(ctx , "SELECT * FROM %s WHERE %s='%s' LIMIT 1", table, keyColName, key);
	MALLOC_FAIL(sql);
	sqlite3_exec((sqlite3 *) db, sql, dbReadFieldCallback, (void *) &cbd , &errorMessage);
	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite select error on %s select: %s", id, errorMessage);
		sqlite3_free(errorMessage);
	}

	talloc_free(sql);
	
	return cbd.res;		
	
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
 
static Bool dbDeleteRow(void *db, TALLOC_CTX *ctx, String id, String table, String colName, String key)
{
	String sql;
	String errorMessage;
	Bool res = PASS;
	
	ASSERT_FAIL(id)
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(table)
	ASSERT_FAIL(colName)
	ASSERT_FAIL(key)
	ASSERT_FAIL(db)
	
	
	sql = talloc_asprintf(ctx, "DELETE FROM %s WHERE %s='%s'", table, colName, key);
	MALLOC_FAIL(sql)
	
	sqlite3_exec((sqlite3 *) db, sql, NULL, NULL, &errorMessage);

	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite DELETE error on %s: %s", id, errorMessage);
		sqlite3_free(errorMessage);
		res = FAIL;
	}
	
	talloc_free(sql);
	
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

void *DBOpen(String file)
{
	sqlite3 *db;
	
	ASSERT_FAIL(file)
	/* Open Database File */
	if(!access(file, R_OK | W_OK)){
		if((sqlite3_open_v2(file, &db, SQLITE_OPEN_READWRITE, NULL))){
			debug(DEBUG_UNEXPECTED,"Sqlite file open error on file: %s", file);
			return NULL;
		}
	}
	else{
		debug(DEBUG_UNEXPECTED,"Sqlite file access error on file: %s", file);
		return NULL;
	}
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

void DBClose(void *db)
{
	if(db){
		sqlite3_close((sqlite3 *) db);
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

const String DBReadNVState(TALLOC_CTX *ctx, void *db, const String key)
{


	String p = NULL;
	static const String id = "DBReadNVState";
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(key)
		
	/* Transaction start */
	if(dbTxBegin(db, id) != PASS){
		return NULL;
	}
		
	p = dbReadField(db, ctx, id, "nvstate", "key", key, "value");
	
	/* Transaction commit */
	
	dbTxEnd(db, id, PASS);

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

Bool DBWriteNVState(TALLOC_CTX *ctx, void *db, const String key, const String value)
{
	Bool res = PASS;
	String errorMessage;
	static const String id = "DBWriteNVState";
	String sql = NULL;
	String p;
	time_t now;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(key)
	ASSERT_FAIL(value)
	
	time(&now);
		
	/* Transaction begin */
	
	if(dbTxBegin(db, id) != PASS){
		return FAIL;
	}
	


	p = dbReadField(db, ctx, id, "nvstate", "key", key, "value");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, id, "nvstate", "key",  key);
	}
	
	if(res == PASS){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (key,value,timestamp) VALUES ('%s','%s','%lld')",
		"nvstate", key, value, (long long) now);
	
		MALLOC_FAIL(sql)
	
		sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite INSERT error on %s: %s", id, errorMessage);
			sqlite3_free(errorMessage);
			res = FAIL;
		}
	}
	
	if(sql){
		talloc_free(sql);
	}
	

	/* Transaction end */
	
	dbTxEnd(db, id, res);

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


const String DBFetchScript(TALLOC_CTX *ctx, void *db, const String scriptName)
{

	String script = NULL;
	static const String id = "DBFetchScript";
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(scriptName)

	if(dbTxBegin(db, id) == FAIL){
		return NULL;
	}
	
	script = dbReadField(db, ctx, id, "scripts", "scriptname", scriptName, "scriptcode");
	
	dbTxEnd(db, id, PASS);
	
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

const String DBFetchScriptByTag(TALLOC_CTX *ctx, void *db, const String tagSubAddr)
{

	String scriptName = NULL;
	String script = NULL;
	static const String id = "DBFetchScriptName";
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(tagSubAddr)

	if(dbTxBegin(db, id) == FAIL){
		return NULL;
	}
	
	scriptName = dbReadField(db, ctx, id, "trigaction", "source", tagSubAddr, "action");
	
	if(scriptName){
		script = dbReadField(db, ctx, id, "scripts", "scriptname", scriptName, "scriptcode");
		talloc_free(scriptName);
	}
	
	dbTxEnd(db, id, PASS);
	
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

Bool DBUpdateTrigLog(TALLOC_CTX *ctx, void *db, const String source, const String schema, const String nvpairs)
{
	Bool res = PASS;
	String errorMessage;
	static const String id = "DBUpdateTrigLog";
	String sql = NULL;
	String p;
	time_t now;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(source)
	ASSERT_FAIL(schema)
	ASSERT_FAIL(nvpairs)
	
	time(&now);
		
	/* Transaction begin */
	
	if(dbTxBegin(db, id) != PASS){
		return FAIL;
	}
	


	p = dbReadField(db, ctx, id, "triglog", "source", source, "nvpairs");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, id, "triglog", "source", source);
	}
	
	if(res == PASS){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (source,schema,nvpairs,timestamp) VALUES ('%s','%s','%s','%lld')",
		"triglog", source, schema, nvpairs, (long long) now);
	
		MALLOC_FAIL(sql)
	
		sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite INSERT error on %s: %s", id, errorMessage);
			sqlite3_free(errorMessage);
			res = FAIL;
		}
	}
	
	if(sql){
		talloc_free(sql);
	}	


	/* Transaction end */
	
	dbTxEnd(db, id, res);

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

Bool DBUpdateHeartbeatLog(TALLOC_CTX *ctx, void *db, const String source)
{
	Bool res = PASS;
	String errorMessage;
	static const String id = "DBUpdateTrigLog";
	String sql = NULL;
	String p;
	time_t now;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(source)

	time(&now);
	
	/* Transaction begin */
	
	if(dbTxBegin(db, id) != PASS){
		return FAIL;
	}
	
	p = dbReadField(db, ctx, id, "hbeatlog", "source", source, "source");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, id, "hbeatlog", "source", source);
	}
	
	if(res == PASS){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (source,timestamp) VALUES ('%s','%lld')",
		"hbeatlog", source, (long long) now);
	
		ASSERT_FAIL(sql)
	
		sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite INSERT error on %s: %s", id, errorMessage);
			sqlite3_free(errorMessage);
			res = FAIL;
		}
	}
	
	if(sql){
		talloc_free(sql);
	}	

	/* Transaction end */
	
	dbTxEnd(db, id, res);

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

Bool DBIRScript(TALLOC_CTX *ctx, void *db, const String name, const String script)
{
	Bool res = PASS;
	String errorMessage;
	static const String id = "DBUpdateScript";
	String sql = NULL;
	String p;

	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(name)
	ASSERT_FAIL(script)

		
	/* Transaction begin */
	
	if(dbTxBegin(db, id) != PASS){
		return FAIL;
	}
	
	p = dbReadField(db, ctx, id, "scripts", "scriptname", name, "scriptcode");
	if(p){
		talloc_free(p);
		res = dbDeleteRow(db, ctx, id, "scripts", "scriptname", name);
	}
	
	if(res == PASS){
		sql = talloc_asprintf(ctx, "INSERT INTO %s (scriptname,scriptcode) VALUES ('%s','%s')",
		"scripts", name, script);
	
		ASSERT_FAIL(sql)
	
		sqlite3_exec(db, sql, NULL, NULL, &errorMessage);
	
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite INSERT error on %s: %s", id, errorMessage);
			sqlite3_free(errorMessage);
			res = FAIL;
		}
	}
	
	if(sql){
		talloc_free(sql);
	}

	/* Transaction end */
	
	dbTxEnd(db, id, res);

	return res;
	
}

/*
 * Return a multiple results from a wildcard select.
 * 
 * Warning: Do not use this on large tables.
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

Bool DBReadRecords(TALLOC_CTX *ctx, void *db,  void *data, String table, 
	unsigned limit, DBRecordCallBack_t callback)
{
	Bool res;
	String errorMessage;
	String sql;
	String id = "DBReadRecords";
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(table)
	ASSERT_FAIL(callback)
	
	res = dbTxBegin(db, id);
	if(res == PASS)
		sql = talloc_asprintf(ctx , "SELECT * FROM %s LIMIT %u", table, limit);
		MALLOC_FAIL(sql);
		sqlite3_exec((sqlite3 *) db, sql, callback, data , &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"%s: Sqlite select error on select: ", id, errorMessage);
			sqlite3_free(errorMessage);
			res = FAIL;
		}
	
	dbTxEnd(db, id, res);
	
	talloc_free(sql);
	
	
	return res;		
	
}

	


