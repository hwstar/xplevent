#ifndef PARSER_H
#define PARSER_H

enum {OPRD_STRINGLIT=0, OPRD_INTLIT, OPRD_FLOATLIT, OPRD_HASHKV, OPRD_HASHREF};
enum {OPRT_NUMEQUALITY=0, OPRT_NUMINEQUALITY, OPRT_NUMGTRTHAN, OPRT_NUMLESSTHAN,
OPRT_NUMGTREQTHAN, OPRT_NUMLESSEQTHAN, OPRT_STREQUALITY};
enum {OPRB_BEGIN=0, OPRB_END=1};
enum {EXS_NORMAL = 0, EXS_IF_BLOCK = 1, EXS_ELSE_BLOCK = 2, EXS_BLOCK_SKIP = 3};

typedef enum {OP_NOP =0, OP_PUSH, OP_ASSIGN, OP_FUNC, OP_BLOCK, OP_IF, OP_TEST2, OP_EXISTS } opType_t;


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
} Pcode_t;

typedef Pcode_t * PcodePtr_t;
typedef PcodePtr_t * PcodePtrPtr_t;

/* pcode header */

typedef struct pcheader_s {
	PcodePtr_t head;
	PcodePtr_t tail;
	ParseHashSTEPtr_t steHead;
	PcodePtr_t firstPush;
	String failReason;
	int ctrlStructRefCount;
	int seq;
	int pushCount;
	Bool tracePcode;
	Bool ignoreAssignErrors;
	void *xplServicePtr;
	void *DB;
	
} PcodeHeader_t;

typedef PcodeHeader_t * PcodeHeaderPtr_t;
typedef PcodeHeaderPtr_t * PcodeHeaderPtrPtr_t;
	

/* Parse Control Block */

typedef struct ParseCtrl_s {
	PcodeHeaderPtr_t pcodeHeader;
	String failReason;
	short lineNo;
}ParseCtrl_t;	
	

typedef ParseCtrl_t * ParseCtrlPtr_t;


Bool ParserSplitXPLTag(TALLOC_CTX *ctx, const String tag, String *vendor, String *device, String *instance);
void ParserHashWalk(PcodeHeaderPtr_t ph, const String name, void (*parseHashWalkCallback)(const String key, const String value));
Bool ParserHashAddKeyValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, const String hashName, const String key, const String value);
const String ParserHashGetValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, const String hashName, const String key);
void ParserExecFunction(PcodeHeaderPtr_t ph, PcodePtr_t pi);
void ParserPcodeEmit(ParseCtrlPtr_t pc, opType_t op, int operand, String data1, String data2);
void ParserPcodeDumpList(PcodeHeaderPtr_t ph);
void ParserSetJumps(ParseCtrlPtr_t this, int tokenID);
Bool ParserPcodeGetValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, PcodePtr_t instr, String *pValue);
Bool ParserPcodePutValue(TALLOC_CTX *ctx, PcodeHeaderPtr_t ph, PcodePtr_t instr, String value);
Bool ParserExecPcode(PcodeHeaderPtr_t ph);
Bool ParserParseHCL(ParseCtrlPtr_t this, Bool fileMode, const String str);
String ParserCheckSyntax(TALLOC_CTX *ctx, String file);


#endif
