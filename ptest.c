/*
 * ptest.c
 * 
 *  Copyright (C) 2013  Stephen Rodgers
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * 
 * Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 */


#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "parser.h"

#define SHORT_OPTIONS "af:mpt"

/* Commandline options. */

static struct option longOptions[] = {
	{"all", 0, 0, 'a'},
	{"file", 1, 0, 'f'},
	{"memory", 0, 0,'m'},
	{"pcode", 0, 0, 'p'},
	{"trace", 0, 0, 't'},
	{0, 0, 0, 0}
};


/*
 * Parser test harness
 */
 
/* Program name */
char *progName;

/* Debug level. */
int debugLvl = 4;

Bool fullPcodeDump = FALSE;
Bool execPcodeTrace = FALSE;
Bool memoryUsage = FALSE;

/*
 * Print the contents of a hash entry
 */
 
void hashWalkPrint(const String key, const String value)
{
		debug(DEBUG_EXPECTED,"Key: %s, Value: %s", key, value);
}



int main(int argc, char *argv[])
{
	int longindex;
	int optchar;
	TALLOC_CTX *top;
	ParseCtrlPtr_t parseCtrl;
	pcodeHeaderPtr_t ph;
	String fileName;
	int res = 0;
	
	/* Set the program name */
	progName=argv[0];


	
	/* Allocate top context */
	
	top = talloc_new(NULL);
	ASSERT_FAIL(top)

		/* Parse the arguments. */
	while((optchar=getopt_long(argc, argv, SHORT_OPTIONS, longOptions, &longindex)) != EOF) {
		
		/* Handle each argument. */
		switch(optchar) {
			
			/* If it was an error, exit right here. */
			case '?':
				fatal("Optarg error");
				exit(1);
				
			case 'a':
				memoryUsage = execPcodeTrace = fullPcodeDump = TRUE;
				break;
		
				
			case 'm':
				memoryUsage = TRUE;
				break;
				
			case 't': /* Pcode Trace */
				execPcodeTrace = TRUE;
				break;
				
			case 'p': /* Pcode Dump */
				fullPcodeDump = TRUE;
				break;
			
			case 'f':
				ASSERT_FAIL(fileName = talloc_strdup(top, optarg))
				break;
				
			/* It was something weird.. */
			default:
				fatal("Unhandled getopt return value %d", optchar);
		}

	}
	


	
	
	/* Allocate pcode header */
	ph = talloc_zero(top, pcodeHeader_t);
	ASSERT_FAIL(ph)
	

	
	/* Allocate parser control structure */
		
	parseCtrl = talloc_zero(top, ParseCtrl_t);
	ASSERT_FAIL(parseCtrl)

	/* Add pointer to pcode header in parse control block */
	parseCtrl->pcodeHeader = ph;
	
	/* Initialize the xplin and xplnvin hashes with some test data */
	
	ParserHashAddKeyValue(ph, ph,"xplin", "source", "hwstar-test.0");
	ParserHashAddKeyValue(ph, ph, "xplin", "classtype", "x10.advanced");

	ParserHashAddKeyValue(ph, ph,"xplnvin", "current", "78.0");
	ParserHashAddKeyValue(ph, ph, "xplnvin", "units", "Fahrenheit");
	ParserHashAddKeyValue(ph, ph, "xplnvin", "device", "0");


	ParserParseHCL(parseCtrl, TRUE, fileName);

	
	if(parseCtrl->failReason){
		debug(DEBUG_UNEXPECTED,"Parse Error: %s", parseCtrl->failReason);	
		res = -1;
	}

	if((!res) && (fullPcodeDump == TRUE)){
		debug(DEBUG_EXPECTED,"***** Start P-code dump before execution *****");
		ParserPcodeDumpList(ph);
		debug(DEBUG_EXPECTED,"***** End P-code dump before execution *****");
	}


	
	if(!res){
		ph->tracePcode = execPcodeTrace;
		if(ParserExecPcode(ph)){
			ASSERT_FAIL(ph->failReason);
			debug(DEBUG_UNEXPECTED,"Pcode error: %s", ph->failReason);
		}
	}

	if(memoryUsage == TRUE){
		talloc_report(top, stdout);
	}


	talloc_free(top);
	
	exit(res);
}

