#ifndef UTIL_H
#define UTIL_H

String *UtilFileReadString(TALLOC_CTX *ctx, String filename);
int UtilFileReadString(String filename, String str);

#endif
