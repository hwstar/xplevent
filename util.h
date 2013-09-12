#ifndef UTIL_H
#define UTIL_H

String *UtilFileReadString(TALLOC_CTX *ctx, String filename);
Bool UtilFileWriteString(String filename, String str)
static uint32_t UtilHash(const String key);
String UtilMoveString(TALLOC_CTX *newCtx, String oldStr, int offset);
String UtilStringCopy(String dest, const String src, int charsToCopy);
pid_t UtilPIDRead(char *filename);
int UtilPIDWrite(String filename, pid_t pid);


#endif
