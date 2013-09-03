#include <stdio.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "parser.h"

/*
 * Parser test harness
 */
 
/* Program name */
char *progName;

/* Debug level. */
int debugLvl = 4;

/*
 * Print the contents of a hash entry
 */
 
void hashWalkPrint(const String key, const String value)
{
		debug(DEBUG_EXPECTED,"Key: %s, Value: %s", key, value);
}



int main(int argc, char *argv[])
{
	progName = argv[0];
	TALLOC_CTX *top;
	ParseCtrlPtr_t parseCtrl;
	pcodeHeaderPtr_t ph;
	int res = 0;


	if(argc != 3){
		fatal("Missing input parameters\n");
		exit(-1);
	}
	
	/* Allocate top context */
	
	top = talloc_new(NULL);
	ASSERT_FAIL(top)
	
	/* Allocate pcode header */
	ph = talloc_zero(top, pcodeHeader_t);
	ASSERT_FAIL(ph)
	
	
	/* Allocate parser control structure */
		
	parseCtrl = talloc_zero(top, ParseCtrl_t);
	ASSERT_FAIL(top)
	
	/* Add pointer to pcode header in parse control block */
	parseCtrl->pcodeHeader = ph;
	
	/* Initialize the args hash with some test data */
	
	ParserHashAddKeyValue(&ph->argsHead, parseCtrl,"current","78.0");
	ParserHashAddKeyValue(&ph->argsHead, parseCtrl,"units","Fahrenheit");
	
	debug(DEBUG_EXPECTED,"***** Input hash contents *****");
	ParserHashWalk(ph->argsHead, hashWalkPrint);
	debug(DEBUG_EXPECTED,"***** Input hash contents *****");
			

	
	
	ParserParseHCL(parseCtrl, (*argv[1] == 'f'), argv[2]);
	
	if(parseCtrl->failReason){
		debug(DEBUG_UNEXPECTED,"Parse Error: %s", parseCtrl->failReason);	
		res = -1;
	}
	
	debug(DEBUG_EXPECTED,"***** P-code dump *****");
	ParserPcodeDumpList(ph);
	debug(DEBUG_EXPECTED,"***** P-code dump *****");
	
	if(!res){
		if(ParserExecPcode(ph)){
			ASSERT_FAIL(ph->failReason);
			debug(DEBUG_UNEXPECTED,"Pcode error: %s", ph->failReason);
		}
	}

	
	talloc_report(top, stdout);
	
	talloc_free(top);
	
	exit(res);
}

