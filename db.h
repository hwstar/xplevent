#ifndef DB_H
#define DB_H

void *DBOpen(String file);
void DBClose(void *db);
const String DBReadNVState(TALLOC_CTX *ctx, void *db, const String key);
Bool DBWriteNVState(TALLOC_CTX *ctx, void *db, const String key, const String value);
const String DBFetchScript(TALLOC_CTX *ctx, void *db, const String scriptName);
const String DBFetchScriptByTag(TALLOC_CTX *ctx, void *db, const String tagSubAddr);
Bool DBUpdateTrigLog(TALLOC_CTX *ctx, void *db, const String source, const String schema, const String nvpairs);
Bool DBUpdateHeartbeatLog(TALLOC_CTX *ctx, void *db, const String source);
Bool DBIRScript(TALLOC_CTX *ctx, void *db, const String name, const String script);

#endif
