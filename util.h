#ifndef UTIL_H
#define UTIL_H

String *UtilFileReadString(TALLOC_CTX *ctx, String filename);
int UtilFileWriteString(String filename, String str);
static uint32_t UtilHash(const String key);
String UtilMoveString(TALLOC_CTX *newCtx, String oldStr, int offset);

#endif
