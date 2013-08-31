
#ifndef PARSE_H
#define PARSE_H

void *ParseAlloc(void *(*mallocProc)(size_t));
void ParseFree(void *p, void (*freeProc)(void*));
void Parse(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  tokenPtr_t yyminor,          /* The value for the token */
  void *context				   /* Memory context */
);

#endif
