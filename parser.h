#ifndef PARSER_H
#define PARSER_H


typedef enum {ATYPE_STRING = 0, ATYPE_HASH = 1} argType_t;


/* Token control block */

typedef struct token {
	int lineNo;
	int tokenID;
	char *stringVal;
} token_t;

typedef token_t * tokenPtr_t;


/* Parse Control Block */

typedef struct ParseCtrl_s {
	void *xplServicePtr;
	void *argsHead;
	void *xplOutHead;
	void *xplOutContext;
	void *argListHead;
	void *argListContext;
	char *failReason;
	short lineNo;
	short numFuncArgs;
}ParseCtrl_t;	
	

typedef ParseCtrl_t * ParseCtrlPtr_t;


/* Linked list entry for arguments */

typedef struct argListEntry_s {
	unsigned magic;
	argType_t type;
	char *arg;
	struct argListEntry_s *next;
} argListEntry_t;

typedef argListEntry_t * argListEntryPtr_t;


/* Hash linked list entry */

typedef struct ParseHashKV_s {
	unsigned magic;
	unsigned hash;
	char *key;
	char *value;
	struct ParseHashKV_s *next;
} ParseHashKV_t;

typedef ParseHashKV_t * ParseHashKVPtr_t;
typedef ParseHashKVPtr_t * ParseHashKVPtrPtr_t;



int ParserSplitXPLTag(TALLOC_CTX *ctx, const char *tag, char **vendor, char **device, char **instance);
int ParserHCLScan(ParseCtrlPtr_t parseCtrl, int fileMode, const char *str);
void ParserHashWalk(void *pHead, void (*parseHashWalkCallback)(const char *key, const char *value));
void ParserHashAddKeyValue(void **ppHead, void *tallocContext, const char *key, const char *value);
const char *ParserHashGetValue(void *pHead, const char *key);
void ParserAddFunctionArg(struct ParseCtrl_s *this, void *arg, argType_t type);
void ParserExecFunction(struct ParseCtrl_s *this, int tokenID);
void ParserPostFunctionCleanup (struct ParseCtrl_s *this);
const char *ParserFunctionArg(argListEntryPtr_t ale, int argNum);



#endif
