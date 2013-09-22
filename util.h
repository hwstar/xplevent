#ifndef UTIL_H
#define UTIL_H

String UtilFileReadString(TALLOC_CTX *ctx, const String filename);
Bool UtilFileWriteString(const String filename, const String str);
uint32_t UtilHash(const String key);
String UtilMoveString(TALLOC_CTX *newCtx, String oldStr, int offset);
String UtilStringCopy(String dest, const String src, int charsToCopy);
pid_t UtilPIDRead(const String filename);
Bool UtilPIDWrite(const String filename, pid_t pid);
Bool UtilSpawn(const String command, pid_t *pid);
String *UtilSplitString(TALLOC_CTX *ctx, String input, char sep);
String *UtilSplitWhite(TALLOC_CTX *ctx, String input);


#endif
