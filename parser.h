#ifndef PARSER_H
#define PARSER_H

typedef enum {OP_NOP =0, OP_PUSH, OP_POP, OP_ASSIGN, OP_FUNC } opType_t;


typedef enum {ATYPE_STRING = 0, ATYPE_HASH = 1} argType_t;


/* Token control block */

typedef struct token {
	int lineNo;
	int tokenID;
	String stringVal;
} token_t;

typedef token_t * tokenPtr_t;

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


/* 
 * Pcode control block
 */
 
typedef struct pcode_s {
	unsigned magic;
	opType_t opcode;
	int operand;
	int datatype;
	void *data1;
	void *data2;
	struct pcode_s *prev;
	struct pcode_s *next;
} pcode_t;

typedef pcode_t * pcodePtr_t;


/* pcode header */

typedef struct pcheader_s {
	pcodePtr_t head;
	pcodePtr_t tail;
	void *pcodeCTX;
	ParseHashKVPtr_t argsHead;
	ParseHashKVPtr_t xplOutHead;
	void *xplOutContext;
	argListEntryPtr_t argListHead;
	short numFuncArgs;
	void *argListContext;
	void *xplServicePtr;
	
} pcodeHeader_t;

typedef pcodeHeader_t * pcodeHeaderPtr_t;
	

/* Parse Control Block */

typedef struct ParseCtrl_s {
	pcodeHeaderPtr_t pcodeHeader;
	String failReason;
	short lineNo;
}ParseCtrl_t;	
	

typedef ParseCtrl_t * ParseCtrlPtr_t;


int ParserSplitXPLTag(TALLOC_CTX *ctx, const String tag, String *vendor, String *device, String *instance);
int ParserHCLScan(ParseCtrlPtr_t this, int fileMode, const String str);
void ParserHashWalk(ParseHashKVPtr_t pHead, void (*parseHashWalkCallback)(const String key, const String value));
void ParserHashAddKeyValue(ParseHashKVPtrPtr_t ppHead, void *tallocContext, const String key, const String value);
const String ParserHashGetValue(ParseHashKVPtr_t pHead, const String key);
void ParserAddFunctionArg(ParseCtrlPtr_t this, void *arg, argType_t type);
void ParserExecFunction(struct ParseCtrl_s *this, int tokenID);
void ParserPostFunctionCleanup (ParseCtrlPtr_t this);
const String ParserFunctionArg(argListEntryPtr_t ale, int argNum);
void ParserPcodeEmit(ParseCtrlPtr_t pc, opType_t op, int operand, String data1, String data2);
void ParserDumpPcodeList(pcodeHeaderPtr_t ph);



#endif
