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
int SocketCreateListenList(String bindaddr, String service, int family, int socktype, 
	int (*addsock)(int sock, void *addr, int family, int socktype));
Bool SocketWaitReadReady(int socket, int msTimeout);
Bool SocketWaitWriteReady(int socket, int msTimeout);


#endif
