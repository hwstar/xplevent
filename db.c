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
* Transaction begin
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
* Transaction end
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
 * Result must be talloc_free'd when no longer required
 */

static const String dbReadField(void *db, TALLOC_CTX *ctx, String id, String table, 
String colName, String key, String valueName)
{
	String errorMessage;
	String sql;
	callbackData_t cbd;
	
	
	ASSERT_FAIL(table)
	ASSERT_FAIL(colName)
	ASSERT_FAIL(key)
	ASSERT_FAIL(valueName)
	
	cbd.valueName = valueName;
	cbd.res = NULL;
	cbd.ctx = ctx;
	
	sql = talloc_asprintf(ctx , "SELECT * FROM %s WHERE %s='%s'", table, colName, key);
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
 */
 
static Bool dbDeleteRow(void *db, TALLOC_CTX *ctx, String id, String table, String colName, String key)
{
	String sql;
	String errorMessage;
	Bool res = PASS;
	
	ASSERT_FAIL(id);
	ASSERT_FAIL(table)
	ASSERT_FAIL(colName)
	ASSERT_FAIL(key)
	
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
*/

void DBClose(void *db)
{
	if(db){
		sqlite3_close((sqlite3 *) db);
	}
}


/*
* Read a value from the nvstate table
* Result must be talloc_free'd when no longer required
*/

const String DBReadNVState(TALLOC_CTX *ctx, void *db, const String key)
{


	String p;
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
		sql = talloc_asprintf(ctx, "INSERT INTO %s (key,value,timestamp) VALUES ('%s','%s','%ld')",
		"nvstate", key, value, (long) now);
	
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
		sql = talloc_asprintf(ctx, "INSERT INTO %s (source,schema,nvpairs,timestamp) VALUES ('%s','%s','%s','%ld'))",
		"triglog", source, schema, nvpairs, (long) now);
	
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
		sql = talloc_asprintf(ctx, "INSERT INTO %s (source,timestamp) VALUES ('%s','%ld')",
		"hbeatlog", source, (long) now);
	
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
		"scripts", script);
	
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

	


