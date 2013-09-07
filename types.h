
#ifndef TYPES_H
#define TYPES_H

/* Pull in types by size */
#include <inttypes.h>

/* Define common types */
#ifndef COMMON_TYPES
typedef int Bool;
typedef char * String;
#define COMMON_TYPES
#endif

#endif
