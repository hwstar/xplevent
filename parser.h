#ifndef PARSER_H
#define PARSER_H


typedef enum {ATYPE_STRING = 0, ATYPE_HASH = 1} argType_t;


/* Token control block */

typedef struct token {
	int lineNo;
	int tokenID;
	String stringVal;
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
	String failReason;
	short lineNo;
	short numFuncArgs;
}ParseCtrl_t;	
	

typedef ParseCtrl_t * ParseCtrlPtr_t;


/* Linked list entry for arguments */

typedef struct argListEntry_s {
	unsigned magic;
	argType_t type;
	String arg;
	struct argListEntry_s *next;
} argListEntry_t;

typedef argListEntry_t * argListEntryPtr_t;


/* Hash linked list entry */

typedef struct ParseHashKV_s {
	unsigned magic;
	unsigned hash;
	String key;
	String value;
	struct ParseHashKV_s *next;
} ParseHashKV_t;

typedef ParseHashKV_t * ParseHashKVPtr_t;
typedef ParseHashKVPtr_t * ParseHashKVPtrPtr_t;



int ParserSplitXPLTag(TALLOC_CTX *ctx, const String tag, String *vendor, String *device, String *instance);
int ParserHCLScan(ParseCtrlPtr_t parseCtrl, int fileMode, const String str);
void ParserHashWalk(void *pHead, void (*parseHashWalkCallback)(const String key, const String value));
void ParserHashAddKeyValue(void **ppHead, void *tallocContext, const String key, const String value);
const String ParserHashGetValue(void *pHead, const String key);
void ParserAddFunctionArg(struct ParseCtrl_s *this, void *arg, argType_t type);
void ParserExecFunction(struct ParseCtrl_s *this, int tokenID);
void ParserPostFunctionCleanup (struct ParseCtrl_s *this);
const String ParserFunctionArg(argListEntryPtr_t ale, int argNum);



#endif
