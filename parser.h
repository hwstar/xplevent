#ifndef PARSER_H
#define PARSER_H

enum {OPRD_STRINGLIT=0, OPRD_INTLIT=1, OPRD_HASHKV=2, OPRD_HASHREF=3};
enum {OPRT_NUMEQUALITY=0};
enum {OPRB_BEGIN=0, OPRB_END=1};
enum {EXS_NORMAL = 0, EXS_IF_BLOCK = 1, EXS_ELSE_BLOCK = 2, EXS_BLOCK_SKIP = 3};

typedef enum {OP_NOP =0, OP_PUSH, OP_ASSIGN, OP_FUNC, OP_BLOCK, OP_IF, OP_TEST } opType_t;


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
	int lineNo;
	opType_t opcode;
	int operand;
	int datatype;
	void *data1;
	void *data2;
	struct pcode_s *prev;
	struct pcode_s *next;
} pcode_t;

typedef pcode_t * pcodePtr_t;
typedef pcodePtr_t * pcodePtrPtr_t;

/* pcode header */

typedef struct pcheader_s {
	pcodePtr_t head;
	pcodePtr_t tail;
	ParseHashKVPtr_t argsHead;
	ParseHashKVPtr_t xplOutHead;
	void *xplOutContext;
	argListEntryPtr_t argListHead;
	pcodePtr_t firstPush;
	String failReason;
	short lastIfOperand;
	short execState;
	short pushCount;
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

void ParserHashWalk(ParseHashKVPtr_t pHead, void (*parseHashWalkCallback)(const String key, const String value));
void ParserHashAddKeyValue(ParseHashKVPtrPtr_t ppHead, void *tallocContext, const String key, const String value);
const String ParserHashGetValue(ParseHashKVPtr_t pHead, const String key);
void ParserExecFunction(struct ParseCtrl_s *this, int tokenID);
void ParserPcodeEmit(ParseCtrlPtr_t pc, opType_t op, int operand, String data1, String data2);
void ParserPcodeDumpList(pcodeHeaderPtr_t ph);
void ParserUpdateIf(ParseCtrlPtr_t this, tokenPtr_t t);
Bool ParserPcodeGetValue(pcodeHeaderPtr_t ph, pcodePtr_t instr, String *pValue);
Bool ParserPcodePutValue(pcodeHeaderPtr_t ph, pcodePtr_t instr, String value);
int ParserExecPcode(pcodeHeaderPtr_t ph);
int ParserParseHCL(ParseCtrlPtr_t this, int fileMode, const String str);





#endif
