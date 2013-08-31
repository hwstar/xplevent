#ifndef LEX_H
#define LEX_H
void yyInit(void * context);
void yyScanString(const char *s);
void yyDeleteBuffer(void);
int yyLex(void);
void yySetInputFile(FILE *f);
const tokenPtr_t yyTokenVal(void *context);
#endif

