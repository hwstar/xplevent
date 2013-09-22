#ifndef DB_H
#define DB_H

typedef int (* DBRecordCallBack_t)(void *data, int argc, String *argv, String *colnames);


void *DBOpen(String file);
void DBClose(void *db);
const String DBReadNVState(TALLOC_CTX *ctx, void *db, const String key);
Bool DBWriteNVState(TALLOC_CTX *ctx, void *db, const String key, const String value);
const String DBFetchScript(TALLOC_CTX *ctx, void *db, const String scriptName);
const String DBFetchScriptByTag(TALLOC_CTX *ctx, void *db, const String tagSubAddr);
Bool DBUpdateTrigLog(TALLOC_CTX *ctx, void *db, const String source, const String schema, const String nvpairs);
Bool DBUpdateHeartbeatLog(TALLOC_CTX *ctx, void *db, const String source);
Bool DBIRScript(TALLOC_CTX *ctx, void *db, const String name, const String script);
Bool DBReadRecords(TALLOC_CTX *ctx, void *db,  void *data, String table, unsigned limit, DBRecordCallBack_t callback);

#endif
