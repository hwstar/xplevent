#ifndef PARSER_H
#define PARSER_H

enum {OPRD_STRINGLIT=0, OPRD_INTLIT=1, OPRD_HASHKV=2, OPRD_HASHREF=3};
enum {OPRT_NUMEQUALITY=0, OPRT_NUMINEQUALITY};
enum {OPRB_BEGIN=0, OPRB_END=1};
enum {EXS_NORMAL = 0, EXS_IF_BLOCK = 1, EXS_ELSE_BLOCK = 2, EXS_BLOCK_SKIP = 3};

typedef enum {OP_NOP =0, OP_PUSH, OP_ASSIGN, OP_FUNC, OP_BLOCK, OP_IF, OP_TEST } opType_t;


typedef enum {ATYPE_STRING = 0, ATYPE_HASH = 1} argType_t;


/* Token control block */

typedef struct token {
	int lineNo;
	int tokenID;
	int operand;
	String anno;
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


/* Hash symbol table entry */

typedef struct ParseHashSTE_s {
	unsigned magic;
	unsigned hash;
	Bool writable;
	String name;
	TALLOC_CTX *context;
	ParseHashKVPtr_t head;
	struct ParseHashSTE_s *next;
} ParseHashSTE_t;

typedef ParseHashSTE_t * ParseHashSTEPtr_t;
typedef ParseHashSTEPtr_t * ParseHashSTEPtrPtr_t;

/* 
 * Pcode control block
 */
 
typedef struct pcode_s {
	unsigned magic;
	int lineNo;
	int seq;
	int operand;
	int datatype;
	int ctrlStructRefCount;
	opType_t opcode;
	void *data1;
	void *data2;
	struct pcode_s *skip;
	struct pcode_s *prev;
	struct pcode_s *next;
} pcode_t;

typedef pcode_t * pcodePtr_t;
typedef pcodePtr_t * pcodePtrPtr_t;

/* pcode header */

typedef struct pcheader_s {
	pcodePtr_t head;
	pcodePtr_t tail;
	ParseHashSTEPtr_t head;
	ParseHashKVPtr_t argsHead; // To be removed
	ParseHashKVPtr_t xplOutHead; // To be removed
	void *xplOutContext; // To be removed
	argListEntryPtr_t argListHead; // To be removed
	pcodePtr_t firstPush;
	String failReason;
	int ctrlStructRefCount;
	int seq;
	short pushCount;
	short numFuncArgs;
	Bool dumpPcode;
	void *argListContext; // To be removed
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
void ParserSetJumps(ParseCtrlPtr_t this, int tokenID);
Bool ParserPcodeGetValue(pcodeHeaderPtr_t ph, pcodePtr_t instr, String *pValue);
Bool ParserPcodePutValue(pcodeHeaderPtr_t ph, pcodePtr_t instr, String value);
int ParserExecPcode(pcodeHeaderPtr_t ph);
int ParserParseHCL(ParseCtrlPtr_t this, int fileMode, const String str);
String ParserMoveString(TALLOC_CTX *newCtx, String oldStr, int offset);


#endif
