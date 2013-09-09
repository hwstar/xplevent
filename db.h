#ifndef DB_H
#define DB_H

void *DBOpen(String sqlitefile);
void DBClose(void *db);
const String DBReadNVState(TALLOC_CTX *ctx, void *db, const String key);
Bool DBWriteNVState(TALLOC_CTX *ctx, void *db, const String key, const String value);

#endif
