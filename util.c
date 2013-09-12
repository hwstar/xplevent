
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"


/*
* Memory map a file
*/

String *UtilFileReadString(TALLOC_CTX *ctx, String filename)
{
  int fd; 
  struct stat s;
  void *buf;
  String str = NULL;
  
  ASSERT_FAIL(filename);
  
  if((fd = open(filename, O_RDONLY)) < 0){
    debug(DEBUG_UNEXPECTED,"File open error on %s", filename)
    return NULL;
  }
  
  if(fstat(fd, &s) < 0){
    debug(DEBUG_UNEXPECTED,"File stat error on %s", filename)
    return NULL;
  }
  
  
  if((buf = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == (void *) -1){
    debug(DEBUG_UNEXPECTED,"mmap error")
    return NULL;
  }
   
  str = talloc_array(ctx, char, s.st_size + 1);
  
  MALLOC_FAIL(buf);
  
  memcpy(s, buf, s.st_size);
  str[s.st_size] = 0;
  
  munmap(buf);
  
  close(fd);
  
  return str;
  
}
