#ifndef DB_H
#define DB_H

typedef int (* DBRecordCallBack_t)(void *data, int argc, String *argv, String *colnames);


void *DBOpen(TALLOC_CTX *ctx, String file);
void DBClose(void *dbObjPtr);
const String DBReadNVState(TALLOC_CTX *ctx, void *dbObjPtr, const String key);
Bool DBWriteNVState(TALLOC_CTX *ctx, void *dbObjPtr, const String key, const String value);
const String DBFetchScript(TALLOC_CTX *ctx, void *dbObjPtr, const String scriptName);
const String DBFetchScriptByTag(TALLOC_CTX *ctx, void *dbObjPtr, const String tagSubAddr);
Bool DBUpdateTrigLog(TALLOC_CTX *ctx, void *dbObjPtr, const String source, const String schema, const String nvpairs);
Bool DBUpdateHeartbeatLog(TALLOC_CTX *ctx, void *dbObjPtr, const String source);
Bool DBIRScript(TALLOC_CTX *ctx, void *dbObjPtr, const String name, const String script);
Bool DBReadRecords(TALLOC_CTX *ctx, void *dbObjPtr,  void *data, String table, unsigned limit, DBRecordCallBack_t callback);
void DBGenFile(TALLOC_CTX *ctx, String theFile, Bool forceFlag);
const String DBGetFieldByName(const String *argv, const String *colnames, const String colname);

#endif
