#include <stdio.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "parser.h"



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


	if(argc != 3){
		fatal("Missing input parameters\n");
		exit(1);
	}
	
	/* Allocate top context */
	
	top = talloc_new(NULL);
	ASSERT_FAIL(top)
	
	/* Allocate parser control structure */
		
	parseCtrl = talloc_zero(top, ParseCtrl_t);
	ASSERT_FAIL(top)
	
	
	ParserHashAddKeyValue(&parseCtrl->pcodeHeader->argsHead, parseCtrl,"current","78.0");
	ParserHashAddKeyValue(&parseCtrl->pcodeHeader->argsHead, parseCtrl,"units","Fahrenheit");
	
	debug(DEBUG_EXPECTED,"***** Input hash contents *****");
	ParserHashWalk(parseCtrl->pcodeHeader->argsHead, hashWalkPrint);
	debug(DEBUG_EXPECTED,"***** Input hash contents *****");
			

	
	
	if(ParserHCLScan(parseCtrl, (*argv[1] == 'f'), argv[2])){
		debug(DEBUG_UNEXPECTED, "%s", parseCtrl->failReason);
	}
	if(parseCtrl->failReason)
		debug(DEBUG_UNEXPECTED,"Parse Error: %s", parseCtrl->failReason);	
		
	debug(DEBUG_EXPECTED,"***** Output hash contents *****");
	ParserHashWalk(parseCtrl->pcodeHeader->xplOutHead, hashWalkPrint);
	debug(DEBUG_EXPECTED,"***** Output hash contents *****");
	
	
	
	talloc_report(top, stdout);
	
	talloc_free(top);
	
	return 0;
}

