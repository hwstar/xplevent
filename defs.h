
#ifndef DEFS_H
#define DEFS_H


#ifndef ASSERT_FAIL
#define ASSERT_FAIL(C) if(!(C)) fatal("Assertion failure detected in file %s, line %d",__FILE__,__LINE__);
#endif
	

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define PASS 0
#define FAIL 1

#endif
