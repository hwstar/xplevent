/*
 * Socket handling headers.
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <sys/socket.h>

typedef struct SockAclListEntry_s {
	unsigned magic;
	struct SockAclListEntry_s prev;
	struct SockAclListEntry_s next;
} SockAclListEntry_t;

typedef SockAclListEntry_t * SoclAclListEntryPtr_t;


typedef struct SockAclListPtr_s {
	SockAclListEntryPtr_t head;
	SockAckListEntryPtr_t tail;
} SockAclList_t;

typedef SockAclList_t * SockAclListPtr_t;

	

/* Prototypes. */

int SocketConnectIP(const String host, const String service, int family, int socktype);
String SocketReadLine(TALLOC_CTX *ctx, int socket, unsigned *length);
int SocketPrintf(TALLOC_CTX *ctx, int socket, const String format, ...);
void *SocketFixAddrPointer(void *p);
Bool SocketCreateListenList(String bindaddr, String service, int family, int socktype, 
	int (*addsock)(int sock, void *addr, int family, int socktype));
Bool SocketWaitReadReady(int socket, int msTimeout);
Bool SocketWaitWriteReady(int socket, int msTimeout);
Bool SocketCheckACL(TALLOC_CTX *ctx, SockAclListPtr_t acl, 
struct sockaddr_storage *clientAddr, socklen_t clientAddrSize)


#endif
