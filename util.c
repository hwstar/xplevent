

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
  String str;
  
  ASSERT_FAIL(filename);
  
  if((fd = open(filename, O_RDONLY)) < 0){
    return NULL;
  }
  
  fstat(fd, &s);
  
  if((buf = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == (void *) -1){
    return NULL;
  }
   
  str = talloc_array(ctx, char, s.st_size + 1);
  memcpy(s, buf, s.st_size);
  str[s.st_size] = 0;
  
  munmap(buf);
  
  if(close(fd) < 0){
    return NULL;
  }
  
  return str;
  
}
