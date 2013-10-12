/*
 * Socket handling headers.
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <sys/socket.h>



/* Prototypes. */

int SocketConnectIP(const String host, const String service, int family, int socktype);
String SocketReadLine(TALLOC_CTX *ctx, int socket, unsigned *length);
int SocketPrintf(TALLOC_CTX *ctx, int socket, const String format, ...);
void *SocketFixAddrPointer(void *p);
Bool SocketCreate(String bindaddr, String service, int family, int socktype, void *userObj, 
	Bool (*addsock)(int sock, void *addr, int addrlen, int family, int socktype, void *userObj));
Bool SocketCreateMultiple(TALLOC_CTX *ctx, String bindaddr, String service, int family, int socktype, void *userObj,
	Bool (*addsock)(int sock, void *addr, int addrlen, int family, int socktype, void *userObj));
Bool SocketWaitReadReady(int socket, int msTimeout);
Bool SocketWaitWriteReady(int socket, int msTimeout);
Bool SocketCheckACL(void *acl, const struct sockaddr_storage *clientAddr);
Bool SocketGenACL(TALLOC_CTX *ctx, void **acl, String allowList, String denyList);
String SocketPrintableAddress(TALLOC_CTX *ctx, struct sockaddr_storage *theAddr);
Bool SocketCompareAddrVerbatim(const struct sockaddr_storage *ip1, const struct sockaddr_storage *ip2);


#endif
