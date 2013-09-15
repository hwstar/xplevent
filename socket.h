/*
 * Socket handling headers.
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <sys/socket.h>

/* Prototypes. */

int SocketConnectIP(const String host, const String service, int family, int socktype);
int SocketReadLine(int socket, Bool *rcvdFlag, unsigned *pos, String line, int maxline);
int SocketPrintf(int socket, const String format, ...);
void *SocketFixAddrPointer(void *p);
int SocketCreateListenList(String bindaddr, String service, int family, int socktype, 
	int (*addsock)(int sock, void *addr, int family, int socktype));


#endif
