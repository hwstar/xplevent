/*
* socket.c
*
* Copyright (C) 22013 Stephen Rodgers
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
#include "socket.h"


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
	
int SocketCreateListenList(String bindaddr, String service, int family, int socktype, 
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
	
		if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
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
 * Connect to the daemon socket.
 *
 * Returns the fd of the socket or -1 if error
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
	
	// Get the address list
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
	
	p = (ipv6) ? ipv6 : ipv4; // Prefer IPV6 over IPV4

	/* Create a socket for talking to the daemon program. */

	sock = socket(p->ai_family, p->ai_socktype,p->ai_protocol );
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
	
	/* We don't want to block reads. */
	if(fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		debug(DEBUG_UNEXPECTED,"%s: Could not set socket to nonblocking", id);
		close(sock);
		return -1;
	}

	/* Return this socket. */
	return(sock);
}

/*
 * Read a line of text from a socket.
 *
 */
String SocketReadLine(TALLOC_CTX *ctx, int socket, Bool *rcvdFlag, unsigned *length)
{
	int lsize = 80;
	int res;
	char c;
	String line;
	static String id = "SocketReadLine";
	
	ASSERT_FAIL(length)
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(rcvdFlag)

	
	MALLOC_FAIL(line = talloc_zero_array(ctx, char, lsize))
	
	
	*rcvdFlag = FALSE;
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
			if(errno == EAGAIN){
				if(SocketWaitReadReady(socket, 5000) == FAIL){
					debug(DEBUG_UNEXPECTED, "%s: Read error time out on socket", id);
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
				*rcvdFlag = TRUE;
				return line;
			}
		}
		else{ /* EOF */
			debug(DEBUG_ACTION,"%s: EOF on line", id);
			*length = 0;
			line[0] = 0;
			*rcvdFlag = TRUE;
			return line;
		}
	}	
}

/* 
 * Print to a socket
 */
 
int SocketPrintf(TALLOC_CTX *ctx, int socket, const String format, ...)
{
	va_list ap;
	int res = 0;
	int len;
	String string, sp;

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
				if(errno != EWOULDBLOCK){
					break;
				}
			}
			else{
				if(res == len){
					res = 0;
					break;
				}
				sp += len;
				len -= res;
			}
		}
	}
	
	talloc_free(string);

	va_end(ap);

	return res;	
}

	
