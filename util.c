#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
* Read a file into a string
*/

String *UtilFileReadString(TALLOC_CTX *ctx, String filename)
{
  int fd; 
  struct stat s;
  void *buf;
  String str = NULL;
  
  ASSERT_FAIL(filename);
  
  if((fd = open(filename, O_RDONLY)) < 0){
    debug(DEBUG_UNEXPECTED,"File open error on %s: %s", filename, strerror(errno))
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
  
  MALLOC_FAIL(str);
  
  memcpy(s, buf, s.st_size);
  str[s.st_size] = 0;
  
  munmap(buf);
  
  close(fd);
  
  return str;
  
}

/*
* Write a string to a file
*/

int UtilFileWriteString(String filename, String str)
{
  int len, bw;
  
  ASSERT_FAIL(filename)
  ASSERT_FAIL(str)
  
  len = strlen(str);
  
  if((fd = open(filename, O_WRONLY | O_TRUNC)) < 0){
    debug(DEBUG_UNEXPECTED,"File open error on %s: %s", filename, strerror(errno));
    return FAIL;
  }
  
  for(;;){
    if((bw = write(fd, str, len)) < 0){
      if(bw == EINTR){
        continue;
      }
      debug(DEBUG_UNEXPECTED,"File write error on %s: %s", filename, strerror(errno));
      return FAIL;
    }
    if(bw != len){
      debug(DEBUG_UNEXPECTED, "File could not be written to disk");
      return FAIL;
    }
    break;
  }
  return PASS;
}


/*
* Hash a string
*/

static uint32_t UtilHash(const String key)
{
  int len;
  register uint32_t hash, i;

  ASSERT_FAIL(key)

  len = strlen(key);

  if(!key){
    return 0;
  }

  for(hash = i = 0; i < len; ++i){
    hash += key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}


/*
* Move string from one context to another
*/

String UtilMoveString(TALLOC_CTX *newCtx, String oldStr, int offset)
{
  String newStr;

  ASSERT_FAIL(newCtx)
  ASSERT_FAIL(oldStr)

  ASSERT_FAIL(strlen(oldStr) > offset)

  if((newStr = talloc_strdup(newCtx, oldStr + offset))){
    talloc_free(oldStr);
  }

  return newStr;
}


/*
* Safer string copy
*/

String UtilStringCopy(String dest, const String src, int charsToCopy)
{
  ASSERT_FAIL(dest)
  ASSERT_FAIL(src)
 

  strncpy(dest, src, charsToCopy);
  dest[charsToCopy - 1] = 0;
  return dest;
  
}


/*
* Get the pid from a pidfile. Returns the pid or -1 if it couldn't get the
* pid (either not there, stale, or not accesible).
*/

pid_t UtilPIDRead(String filename) {
  FILE *file;
  pid_t pid;

  /* Get the pid from the file. */
  file=fopen(filename, "r");
  if(!file) {
    return -1;
  }
  if(fscanf(file, "%d", &pid) != 1) {
    fclose(file);
    return -1;
  }
  if(fclose(file) != 0) {
    return -1;
  }

  /* Check that a process is running on this pid. */
  if(kill(pid, 0) != 0) {

    /* It might just be bad permissions, check to be sure. */
    if(errno == ESRCH) {
    return -1 ;
    }
  }

  /* Return this pid. */
  return(pid);
}

/*
* Write the pid into a pid file. Returns zero if it worked, non-zero
* otherwise.
*/

int UtilPidWrite(String filename, pid_t pid) {
  FILE *file;

  /* Create the file. */
  file=fopen(filename, "w");
  if(!file) {
  return -1;
  }

  /* Write the pid into the file. */
  (void) fprintf(file, "%d\n", pid);
  if(ferror(file) != 0) {
    (void) fclose(file);
    return -1;
  }

  /* Close the file. */
  if(fclose(file) != 0) {
    return -1;
  }

  /* We finished ok. */
  return 0;
}




