/*
 * Code to handle the daemon and client's socket needs.
 *
 
 *
 * $Id$
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
 * If it didn't become readable in the ammount of time given, return PASS,
 * otherwise FAIL.
 *
 * The timeout given is in milliseconds.  If it is -1, it is infinite.
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
* Figure out the offset to the address field and return a pointer to it.
*/

void *SocketFixAddrPointer(void *p)
{
	struct sockaddr *q = p;

	if (q->sa_family == AF_INET) 
        	return &((( struct sockaddr_in* ) p )->sin_addr);

	return &(((struct sockaddr_in6* ) p )->sin6_addr);

}



/* Create a listening socket list. */
	
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

	// Init the hints struct for getaddrinfo

	hints.ai_family = family;
	hints.ai_socktype = socktype;
	if(bindaddr == NULL)	
		hints.ai_flags = AI_PASSIVE;

	// Get the address list
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

		// If IPV6 socket, set IPV6 only option so port space does not clash with an IPV4 socket
		// This is necessary in order to prevent the ipv6 bind from failing when an IPV4 socket was previously bound.

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

	
