


#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "defs.h"
#include "types.h"
#include "notify.h"


/*
* Memory map a file
*/

String *UtilMMFile(String filename)
{
  int fd; 
  struct stat s;
  void *buf;
  
  ASSERT_FAIL(filename);
  
  if((fd = open(filename, O_RDONLY)) < 0)
    return NULL;
  
  fstat(fd, &s);
  
  if((buf = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == (void *) -1)
    return NULL;
  
  
  if(close(fd) < 0){
    munmap(buf);
    return NULL;
  }
  
  return String buf;
  
}
