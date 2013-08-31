
/*
 * confread.h
 * 
 *  Copyright (C) 2012  Stephen Rodgers
 * 
 */

#ifndef CONFSCAN_H
#define CONFSCAN_H


/* Enums */

enum {CRE_SYNTAX, CRE_IO, CRE_FOPEN};

/* Typedefs */

typedef struct keyent KeyEntry_t;
typedef KeyEntry_t * KeyEntryPtr_t;
typedef struct sectionent SectionEntry_t;
typedef SectionEntry_t * SectionEntryPtr_t;
typedef struct configent ConfigEntry_t;
typedef ConfigEntry_t * ConfigEntryPtr_t;

/* Entry for a key table entry */

struct	keyent{
	unsigned magic;
	unsigned hash;
	unsigned linenum;
	char *key;
	char *value;
	KeyEntryPtr_t prev;
	KeyEntryPtr_t next;	
};

/* Entry for a section block table */

struct sectionent{
	unsigned magic;
	unsigned hash;
	unsigned linenum;
	unsigned entry_count;
	char *section;
	KeyEntryPtr_t key_head;
	KeyEntryPtr_t key_tail;
	SectionEntryPtr_t prev;
	SectionEntryPtr_t next;	
};


struct configent{
	unsigned magic;
	char *line;
	char *work_string;
	SectionEntryPtr_t head;
	SectionEntryPtr_t tail;
};


/* 
*Function prototypes 
*/

/* Config functions */
ConfigEntryPtr_t ConfReadScan(void *ctx, const char *thePath, void (*error_callback)(int type, int linenum, const char *info));
void ConfReadFree(ConfigEntry_t *theConfig);

/* Section functions */
SectionEntryPtr_t ConfReadFindSection(ConfigEntryPtr_t ce, const char *section);
const char *ConfReadGetSection(SectionEntryPtr_t se);
SectionEntryPtr_t ConfReadGetFirstSection(ConfigEntryPtr_t ce);
SectionEntryPtr_t ConfReadGetNextSection(SectionEntryPtr_t se);
unsigned ConfReadSectionLineNum(SectionEntryPtr_t se);

/* Key Functions */
KeyEntryPtr_t ConfReadKeyEntryBySectKey(ConfigEntryPtr_t ce, const char *section, const char *key);
KeyEntryPtr_t ConfReadFindKey(SectionEntryPtr_t se, const char *key);
const char *ConfReadGetKey(KeyEntryPtr_t ke);
KeyEntryPtr_t ConfReadGetFirstKey(SectionEntryPtr_t se);
KeyEntryPtr_t ConfReadGetFirstKeyBySection(ConfigEntryPtr_t ce, const char * section);
KeyEntryPtr_t ConfReadGetNextKey(KeyEntryPtr_t ke);
unsigned ConfReadKeyLineNum(KeyEntryPtr_t ke);
unsigned ConfReadGetNumEntriesInSect(ConfigEntryPtr_t ce, const char * section);



/* Value functions */
const char * ConfReadGetValue(KeyEntryPtr_t ke);
const char * ConfReadValueBySectKey(ConfigEntryPtr_t ce, const char *section, const char *key);
int ConfReadValueBySectKeyAsUnsigned(ConfigEntryPtr_t ce, const char *section, const char *key, unsigned *res);
const char * ConfReadValueBySectEntKey(SectionEntryPtr_t se, const char *key);

/*Default error handler*/
void ConfReadDefErrorHandler( int etype, int linenum, const char *info);

/* Utility Functions */

char * ConfReadStringCopy(char *dest, const char *src, int charsToCopy);
unsigned ConfReadHash(const char *key);

/* Debugging functions */
void ConfReadDebugDump(ConfigEntryPtr_t ce);


#endif	
	
	

