#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <talloc.h>
#include  "defs.h"
#include "types.h"
#include "notify.h"
#include "db.h"

typedef struct callbackData_s {
	TALLOC_CTX *ctx;
	String colName;
	String res;
} callbackData_t;
	
typedef callbackData_t * callbackDataPtr_t;


/*
 * DBReadField callback function
 */

static int dbReadFieldCallback(void *objptr, int argc, String *argv, String *colnames)
{
	callbackDataPtr_t cbd = objptr;
	int i;
	
	ASSERT_FAIL(objptr)
	ASSERT_FAIL(cbd->ctx)
	ASSERT_FAIL(cbd->colName)
	
	/* Find index to column */
	
	for(i = 0; colnames[i]; i++){
		if(!strcmp(colnames[i], cbd->colName)){
			break;
		}
	}
	if(colnames[i] && i < argc){ /* If a result was found */
		cbd->res = talloc_strdup(cbd->ctx, argv[i]);
		ASSERT_FAIL(cbd->res);
	
	}
	
	return 0;
}

/*
 * Return a single result from a select.
 * Result must be talloc_free'd when no longer required
 */

static const String dbReadField(TALLOC_CTX *ctx, void *db, String table, String colName, String key)
{
	String errorMessage;
	String sql;
	callbackData_t cbd;
	
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(db)
	ASSERT_FAIL(table)
	ASSERT_FAIL(colName)
	ASSERT_FAIL(key)
	
	cbd.colName = colName;
	cbd.res = NULL;
	cbd.ctx = ctx;
	
	sql = talloc_asprintf(ctx , "SELECT * FROM %s WHERE %s='%s'", table, colName, key);
	ASSERT_FAIL(sql);
	sqlite3_exec(db, sql, dbReadFieldCallback, (void *) &cbd , &errorMessage);
	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite select error on dbReadField select: %s", errorMessage);
		sqlite3_free(errorMessage);
	}

	talloc_free(sql);
	
	return cbd.res;		
	
}

/*
 * Delete a row from a table
 */
 
static Bool dbDeleteRow(TALLOC_CTX *ctx, void *db, String table, String key)
{
	return PASS;
}

/*
* Read a value from the nvstate table
* Result must be talloc_free'd when no longer required
*/

const String DBReadNVState(TALLOC_CTX *ctx, void *db, const String key)
{

	String errorMessage;
	String p;
		
	/* Transaction start */
	sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errorMessage);
	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite error on DBReadNVState begin tx: %s", errorMessage);
		sqlite3_free(errorMessage);
		return NULL;
	}

	p = dbReadField(ctx, db, "nvstate", "value", key);

	

	/* Transaction commit */	
	sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, &errorMessage);
	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite error on DBReadNVState commit : %s", errorMessage);
		sqlite3_free(errorMessage);
	}
	return p;
}

/*
* Write a value to the nvstate table
*/

Bool DBWriteNVState(TALLOC_CTX *ctx, void *db, const String key, const String value)
{
	Bool res = PASS;
	String errorMessage;
	String p;
		
	/* Transaction start */
	sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errorMessage);
	if(errorMessage){
		debug(DEBUG_UNEXPECTED,"Sqlite error on DBWriteNVState begin tx: %s", errorMessage);
		sqlite3_free(errorMessage);
		return FAIL;
	}

	p = dbReadField(ctx, db, "nvstate", "value", key);
	if(p){
		res = dbDeleteRow(ctx, db, "nvstate", key);
	}
	

	/* Transaction commit */
	if(res == PASS){
		sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, &errorMessage);
		if(errorMessage){
			debug(DEBUG_UNEXPECTED,"Sqlite error on DBWriteNVState commit : %s", errorMessage);
			sqlite3_free(errorMessage);
		}
	}
	return res;
	
}
