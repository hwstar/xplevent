/*
* socket.c
*
* Copyright (C) 2013 Stephen Rodgers
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*
*
* Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
*
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <talloc.h>
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "util.h"
#include "socket.h"

#define SE_MAGIC 0xE20A6491
#define SH_MAGIC 0x04A8C3D6

typedef struct SockAclListEntry_s {
	unsigned magic;
	struct sockaddr_storage mask;
	struct sockaddr_storage check;
	struct SockAclListEntry_s *prev;
	struct SockAclListEntry_s *next;
} SockAclListEntry_t;

typedef SockAclListEntry_t * SockAclListEntryPtr_t;


typedef struct SockAclListPtr_s {
	unsigned magic;
	SockAclListEntryPtr_t allowHead;
	SockAclListEntryPtr_t allowTail;
	SockAclListEntryPtr_t denyHead;
	SockAclListEntryPtr_t denyTail;
} SockAclList_t;

typedef SockAclList_t * SockAclListPtr_t;

/*
 * Are 2 IPV4 addresses the same
 * (From the Samba project)
 *
 * Arguments:
 *
 * 1. First IPV4 address
 * 2. Second IPV4 address
 * 3. IPV4 Mask
 *
 * Return value:
 *
 * Boolean. TRUE if addresses match.
 */

static Bool sameNetV4(struct in_addr ip1, struct in_addr ip2, struct in_addr mask)
{
	uint32_t net1,net2,nmask;

	/* Make local copies of the addresses and mask */
	nmask = ntohl(mask.s_addr);
	net1 = ntohl(ip1.s_addr);
	net2 = ntohl(ip2.s_addr);
            
        /* Mask each address and compare, then return the result */
	return((net1 & nmask) == (net2 & nmask));
}

/*
* Are two IPs on the same subnet?
* (Inspired from the Samba project)
*
* Arguments:
* 
* 1. First V4/V6 IP address.
* 2. Second V4/V6 IP address.
* 3. V4/V6 address mask
* 
* Return value
*
* Boolean. TRUE if addresses match, FALSE otherwise.
*/

static Bool sameNet(const struct sockaddr_storage *ip1, const struct sockaddr_storage *ip2, 
	const struct sockaddr_storage *mask)
{
	if (ip1->ss_family != ip2->ss_family) {
		/* Never on the same net. */
		return FALSE;
	}


	if (ip1->ss_family == AF_INET6) { /* IPV6? */
		/* Make local copies of the IPV6 addresses and the mask */
		struct sockaddr_in6 ip1_6 = *(const struct sockaddr_in6 *)ip1;
		struct sockaddr_in6 ip2_6 = *(const struct sockaddr_in6 *)ip2;
		struct sockaddr_in6 mask_6 = *(const struct sockaddr_in6 *)mask;
		/* Make pointers to the local copies */
		char *p1 = (char *)&ip1_6.sin6_addr;
		char *p2 = (char *)&ip2_6.sin6_addr;
		char *m = (char *)&mask_6.sin6_addr;
		
		int i;
		
		/* Apply the mask to the addresses... */

		for (i = 0; i < sizeof(struct in6_addr); i++) {
			*p1++ &= *m;
			*p2++ &= *m;
			m++;
		}
		/* Return the comparison */
		
		return (memcmp(&ip1_6.sin6_addr,
		&ip2_6.sin6_addr,
		sizeof(struct in6_addr)) == 0);
	}


	if (ip1->ss_family == AF_INET) { /* IPV4?*/
		/* Return the comparison of the IPV4 addresses */
		return sameNetV4(((const struct sockaddr_in *)ip1)->sin_addr,
		((const struct sockaddr_in *)ip2)->sin_addr,
		((const struct sockaddr_in *)mask)->sin_addr);
	}
	/* Not something we understand */
	return FALSE;
}


/*
* Permit or deny a socket connection.
*
*
* Arguments:
*
* 1. A pointer to the access Control list
* 2. A pointer to the incoming socket data
*
* Return value:
*
* Boolean: PASS = accept, FAIL = reject
*/

Bool SocketCheckACL(void *acl, const struct sockaddr_storage *clientAddr)
{
	SockAclListPtr_t al = acl;
	SockAclListEntryPtr_t e;
	Bool allow = FALSE;
	Bool deny = FALSE;
	
	
	ASSERT_FAIL(clientAddr)
	
	if(!al){
		return PASS;
	}
	
	ASSERT_FAIL(SH_MAGIC == al->magic)
	
	/* Check deny */
	for(e = al->denyHead; e && !deny; e = e->next){
		ASSERT_FAIL(SE_MAGIC == e->magic)
		deny = sameNet(clientAddr, &e->check, &e->mask);
	}
	
	/* Check allow */
	
	for(e = al->allowHead; e && !allow; e = e->next){
		ASSERT_FAIL(SE_MAGIC == e->magic)
		allow = sameNet(clientAddr, &e->check, &e->mask);
	}
	
	if(!deny && !allow){
		return PASS;
	}
	
	if(allow){
		return PASS;
	}
		
	return FAIL;
	
}

/*
 * Generate an access control list from a comma separated list of of Allow and Deny IP addresses (IPV4 and/or IPV6)
 * 
 *
 * Arguments:
 *
 * 1. The talloc context to hang the access control list off of.
 * 2. A comma delimited set of V4 and/or V6 IP addresses with optional CIDR notation to allow.
 * 3. A comma delimited set of V4 and/or V6 IP addresses with optional CIDR notation to deny, 
 *    or the keyword ALL to deny everything and only accept IP addresses on the allow list.
 *
 * Returns:
 * 
 * ACL list or NULL if error. Use talloc_free to free the list when done
 */ 

void *SocketGenACL(TALLOC_CTX *ctx, String allowList, String denyList)
{
	String s;
	String *addrs;
	unsigned i;
	
	ASSERT_FAIL(ctx)

	if(allowList){
		s = UtilStripWhite(ctx, allowList);
		addrs = UtilSplitString(ctx, s, ',');
		talloc_free(s);
		for(i = 0; addrs[i]; i++){
		}
	}
	
	return NULL;
}


/* 
 * Wait for a read data to become readable.
 *
 * Arguments:
 * 
 * 1. The socket to wait on.
 * 2. The time out value in milliseconds. If set to -1, the time out will be infinite.
 *
 *
 * Return value:
 * 
 * Boolean. Return PASS if sucessful,
 * otherwise FAIL.
 *
 *
 */
 
Bool SocketWaitReadReady(int socket, int msTimeout)
{
	fd_set read_fd_set;
	struct timeval tv;
	struct timeval *tvp;
	
	/* If the timeout is -1, we have no timeout. */
	if(msTimeout == -1) {
		tvp=NULL;
	}
	else {
		tvp=&tv;
	}
	
	/* Wait for data to be readable. */
	FD_ZERO(&read_fd_set);
	FD_SET(socket, &read_fd_set);
	tv.tv_sec=msTimeout / 1000;
	tv.tv_usec=(msTimeout % 1000) * 1000;
	if(select(socket+1, &read_fd_set, NULL, NULL, tvp)) {
		/* We got some data, return ok. */
		return PASS;
	}
	else {
		/* We didn't get any data, this is a fail. */
		return FAIL;
	}
}


/* 
 * Wait for a write output buffer to become ready.
 *
 * Arguments:
 * 
 * 1. The socket to wait on.
 * 2. The time out value in milliseconds. If set to -1, the time out will be infinite.
 *
 *
 * Return value:
 * 
 * Boolean. Return PASS if sucessful,
 * otherwise FAIL.
 *
 *
 */
Bool SocketWaitWriteReady(int socket, int msTimeout)
{
	fd_set write_fd_set;
	struct timeval tv;
	struct timeval *tvp;
	
	/* If the timeout is -1, we have no timeout. */
	if(msTimeout == -1) {
		tvp=NULL;
	}
	else {
		tvp=&tv;
	}
	
	/* Wait for data to be readable. */
	FD_ZERO(&write_fd_set);
	FD_SET(socket, &write_fd_set);
	tv.tv_sec=msTimeout / 1000;
	tv.tv_usec=(msTimeout % 1000) * 1000;
	if(select(socket+1, NULL, &write_fd_set, NULL, tvp)) {
		/* We got some data, return ok. */
		return PASS;
	}
	else {
		/* We didn't see the buffer become ready, this is a fail. */
		return FAIL;
	}
}






/*
* Helper to figure out the offset to the address field depending upon the address family
*
* Parameters:
*
* 1. A void pointer to the socket address structure to check.
*
* Return value:
*
* A void pointer to the address family specific part of the socket address structure passed in.
*/

void *SocketFixAddrPointer(void *p)
{
	struct sockaddr *q = p;

	if (q->sa_family == AF_INET) 
        	return &((( struct sockaddr_in* ) p )->sin_addr);

	return &(((struct sockaddr_in6* ) p )->sin6_addr);

}



/* 
*
* Helper to create a listening socket list.
* Supports both IPV4 and IPV6 sockets.
* 
* 
* Parameters:
*
* 1. Bind address string. If NULL is passed in, all interfaces will be bound 
* 2. Service name string. Can be either a service name or a port number.
* 3. Address family. Usually set to AF_UNSPEC.
* 4. Socket type. Set to SOCK_STREAM for a TCP connection.
* 5. Callback function (see below)
*
* Return value:
*
* PASS indicates success. FAIL indicates failure.
*
*************** Callback Function ***************
*
* Parameters:
*
* 1. Socket FD
* 2. Socket address as a void pointer
* 3. Socket family
* 4. Socket type.
*
* Return Value:
*
* PASS if list creation is to continue, FAIL if list creation is to be aborted.
*
*/
	
Bool SocketCreateListenList(String bindaddr, String service, int family, int socktype, 
	int (*addsock)(int sock, void *addr, int family, int socktype))
{
	struct addrinfo hints, *list, *p;
	int sock = -1, res;
	int sockcount = 0;
	static String id = "SocketCreateListenList";

	
	ASSERT_FAIL(service)
	ASSERT_FAIL(addsock);


	memset(&hints, 0, sizeof hints);

	/* Init the hints struct for getaddrinfo */

	hints.ai_family = family;
	hints.ai_socktype = socktype;
	if(bindaddr == NULL)	
		hints.ai_flags = AI_PASSIVE;

	/* Get the address list */
	if((res = getaddrinfo(bindaddr, service, &hints, &list)) == -1){
		debug(DEBUG_EXPECTED, "%s: getaddrinfo failed: %s", id, gai_strerror(res));
		return FAIL;
	}

	for(p = list; p != NULL; p = p->ai_next){ // Traverse the list
		int sockopt = 1;
	
		if((sock = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, p->ai_protocol)) == -1){
			debug(DEBUG_EXPECTED,"%s: Call to socket failed with %s, continuing...",id, strerror(errno));
			continue;
		}		

		/* If IPV6 socket, set IPV6 only option so port space does not clash with an IPV4 socket */
		/* This is necessary in order to prevent the ipv6 bind from failing when an IPV4 socket was previously bound. */

		if(p->ai_family == PF_INET6){
			setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &sockopt, sizeof(sockopt ));
			debug(DEBUG_EXPECTED,"%s: Setting IPV6_V6ONLY socket option", id);
		}
			
		/* Set to reuse socket address when program exits */

		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));


		if(bind(sock, p->ai_addr, p->ai_addrlen) == -1){
			debug(DEBUG_EXPECTED,"%s: Bind failed with %s, continuing...", id, strerror(errno));
			close(sock);
			continue;
		}

		if(listen(sock, SOMAXCONN) == -1){
			debug(DEBUG_EXPECTED, "%s: Listen failed with %s, continuing...", id, strerror(errno));
			close(sock);
			continue;
			}

		/* Callback to have caller do something with the socket */

		sockcount++;

		if((*addsock)(sock, p->ai_addr, p->ai_family, p->ai_socktype) == FALSE)
			break;
	}

	freeaddrinfo(list);

	if(!sockcount){
		debug(DEBUG_EXPECTED, "%s: could not create, bind or listen on a socket. Bindaddr: %s service: %s", id, bindaddr, service);
		return FAIL;
	}

	return PASS;
}





/*
 * Connect to a socket on another host. 
 * Used by clients.
 *
 * Arguments:
 *
 * 1. String. Host name or IP address to connect to.
 * 2. String. Service name or port number.
 * 3. Address family. (Usually AF_UNSPEC).
 * 4. Socket type. Use SOCK_STREAM for TCP connections.
 *
 *
 * Return value:
 *
 * The FD of the socket or -1 if error
 */
 
int SocketConnectIP(const String host, const String service, int family, int socktype)
{

	struct addrinfo hints, *list = NULL, *p = NULL, *ipv6 = NULL, *ipv4 = NULL;
	int sock, res;
	static String id = "SocketConnectIP";

	ASSERT_FAIL(host)
	ASSERT_FAIL(service)

  	memset(&hints, 0, sizeof hints);
	
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	
	/* Get the address list */
	if((res = getaddrinfo(host, service, &hints, &list)) == -1){
		debug(DEBUG_ACTION, "%s: getaddrinfo failed: %s", id, gai_strerror(res));
		return -1;
	}
	for(p = list; p ; p = p->ai_next){
		if((!ipv6) && (p->ai_family == PF_INET6))
			ipv6 = p;
		if((!ipv4) && (p->ai_family == PF_INET))
			ipv4 = p;
	}

	if(!ipv4 && !ipv6){
		debug(DEBUG_ACTION,"%s: Could not find a suitable IP address to connect to", id);
		return -1;
	}
	
	p = (ipv6) ? ipv6 : ipv4; /* Prefer IPV6 over IPV4 */

	/* Create a socket for talking to the daemon program. */

	sock = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol );
	if(sock == -1) {
		freeaddrinfo(list);
		debug(DEBUG_ACTION, "%s: Could not create ip socket: %s", id, strerror(errno));
		return -1;
	}
	
	/* Connect the socket */

	if(connect(sock, (struct sockaddr *) p->ai_addr, p->ai_addrlen)) {
		freeaddrinfo(list);
		debug(DEBUG_ACTION, "%s: Could not connect to inet host:port '%s:%s'.", id, host, service);
		return -1;
	}
	
	freeaddrinfo(list);
	

	/* Return this socket. */
	return(sock);
}

/*
 * Read a line of text from a socket.
 * This function is used to get a line of text from the other host.
 * There is a 5 second time out on waiting for data to arrive.
 *
 * Parameters:
 *
 * 1. The talloc context to hang the result off of.
 * 2. The socket to read from.
 * 3. Unsigned pointer to the length of the line received. 
 *
 * Return Value:
 *
 * Success: A string containing the line of text NUL terminated. This must be freed when no longer required.
 * If the length of the string returned is 0, then EOF has been detected.
 * Failure: NULL
 *
 */
String SocketReadLine(TALLOC_CTX *ctx, int socket, unsigned *length)
{
	int lsize = 80;
	int res;
	char c;
	String line;
	static String id = "SocketReadLine";
	
	ASSERT_FAIL(length)
	ASSERT_FAIL(ctx)
	
	MALLOC_FAIL(line = talloc_zero_array(ctx, char, lsize))
	
	*length = 0;
	
	for(;;){
		if(*length >= (lsize - 2)){
			/* Double the buffer size and re-allocate */
			lsize <<= 1;
			debug(DEBUG_ACTION, "%s: Doubling the line buffer to %d", id, lsize);
			MALLOC_FAIL(line = talloc_realloc(ctx, line, char, lsize))
		}
		
		res = read(socket, &c, 1);
			

		if(res < 0){
			if(errno == EINTR){
				continue;
			}
			if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
				if(SocketWaitReadReady(socket, 5000) == FAIL){
					debug(DEBUG_UNEXPECTED, "%s: Time out on socket", id);
					talloc_free(line);
					return NULL;
				}
				continue;
			}
			debug(DEBUG_UNEXPECTED, "%s: Read error on fd %d: %s", id, socket, strerror(errno));
			talloc_free(line);
			return NULL;
			
		}
		else if(res == 1){
			if(c == '\r'){ /* Ignore return */
				continue;
			}
			if(c != '\n'){
				line[*length] = c;
				(*length)++;
			}
			else{
				line[*length] = 0;
				return line;
			}
		}
		else{ /* EOF */
			debug(DEBUG_ACTION,"%s: EOF on line", id);
			*length = 0;
			line[0] = 0;
			return line;
		}
	}	
}

/* 
 * Print to a socket.
 * This function has a printf-like interface, and allows text to be formatted and
 * written to a socket.
 *
 * Arguments:
 *
 * 1. Talloc context for internal string generation. All internal strings will be
 *    freed upon return from this function.
 *
 * 2. Printf-like format string
 * 3. Variadic argments.
 *
 * Return value:
 *
 * 0 if successful, -1 if error.
 * 
 */
 
Bool SocketPrintf(TALLOC_CTX *ctx, int socket, const String format, ...)
{
	va_list ap;
	int res = PASS;
	int len;
	String string, sp;
	static String id = "SocketPrintf";

	ASSERT_FAIL(format)
	ASSERT_FAIL(ctx)

	va_start(ap, format);

	if(socket >= 0){
		MALLOC_FAIL(sp = string = talloc_vasprintf(ctx, format, ap))
		for(;;){
			len = strlen(sp);
			res = write(socket, sp, len);
			if(res < 0){
				if(errno == EINTR){
					continue;
				}
				if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
					if(SocketWaitWriteReady(socket, 5000) == PASS){
						continue;
					}
					debug(DEBUG_UNEXPECTED, "%s: Time out on socket", id);
				}
				debug(DEBUG_UNEXPECTED, "%s: Socket write error: %s", id, strerror(errno));
				res = FAIL;
				break;
			}
			else{
				if(res == len){
					res = PASS;
					break;
				}
				sp += len;
				len -= res;
			}
		}
		talloc_free(string);
	}
	


	va_end(ap);

	return (Bool) res;	
}

	
