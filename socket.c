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
#include "defs.h"
#include "types.h"
#include "notify.h"
#include "socket.h"

#define TRUE 1
#define FALSE 0
#define ERROR -1;

/*
 * Connect to the daemon socket.
 *
 * Returns the fd of the socket or -1 if error
 */
 
int SocketConnectIP(const String host, const String service, int family, int socktype) {

	struct addrinfo hints, *list = NULL, *p = NULL, *ipv6 = NULL, *ipv4 = NULL;
	int sock, res;

	ASSERT_FAIL(host)
	ASSERT_FAIL(service)

  	memset(&hints, 0, sizeof hints);
	
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	
	// Get the address list
	if((res = getaddrinfo(host, service, &hints, &list)) == -1){
		debug(DEBUG_ACTION, "socket_connect_ip(): getaddrinfo failed: %s", gai_strerror(res));
		return -1;
	}
	for(p = list; p ; p = p->ai_next){
		if((!ipv6) && (p->ai_family == PF_INET6))
			ipv6 = p;
		if((!ipv4) && (p->ai_family == PF_INET))
			ipv4 = p;
	}

	if(!ipv4 && !ipv6){
		debug(DEBUG_ACTION,"socket_connect_ip(): Could not find a suitable IP address to connect to");
		return -1;
	}
	
	p = (ipv6) ? ipv6 : ipv4; // Prefer IPV6 over IPV4

	/* Create a socket for talking to the daemon program. */

	sock = socket(p->ai_family, p->ai_socktype,p->ai_protocol );
	if(sock == -1) {
		freeaddrinfo(list);
		debug(DEBUG_ACTION, "socket_connect_ip(): Could not create ip socket: %s", strerror(errno));
		return -1;
	}
	
	/* Connect the socket */

	if(connect(sock, (struct sockaddr *) p->ai_addr, p->ai_addrlen)) {
		freeaddrinfo(list);
		debug(DEBUG_ACTION, "socket_connect_ip(): Could not connect to inet host:port '%s:%s'.", host, service);
		return -1;
	}
	
	freeaddrinfo(list);
	
	/* We don't want to block reads. */
	if(fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		debug(DEBUG_UNEXPECTED,"Could not set socket to nonblocking");
		close(sock);
		return FALSE;
	}

	/* Return this socket. */
	return(sock);
}

/*
 * Read a line of text from a socket.
 *
 */
 
int SocketReadLineNonBlocking(int socket, Bool *rcvdFlag, unsigned *pos, String line, int maxline) {
	char c;
	int res;
	
	ASSERT_FAIL(pos)
	ASSERT_FAIL(line)
	ASSERT_FAIL(rcvdFlag)
	
	*rcvdFlag = FALSE;
	
	for(;;){
		res = read(socket, &c, 1);	

		if(res < 0){
			if(errno == EINTR){
				continue;
			}
			if(errno != EWOULDBLOCK){
				debug(DEBUG_UNEXPECTED, "Read error on fd %d: %s", socket, strerror(errno));
				*pos = 0;
				break;
			}
			return PASS;
			
		}
		else if(res == 1){
			if(c == '\r'){ /* Ignore return */
				continue;
			}
			if(c != '\n'){
				if(*pos < (maxline - 1)){
					line[*pos] = c;
					(*pos)++;
				}
				else{
					debug(DEBUG_UNEXPECTED,"End of line buffer reached!");
				}

			}
			else{
				line[*pos] = 0;
				*pos = 0;
				*rcvdFlag = TRUE;
				return PASS;
			}
		}
		else{ /* EOF */
			*pos = 0;
			line[0] = 0;
			*rcvdFlag = TRUE;
			return TRUE;
		}
	}

	return FAIL;
	
}

/* 
 * Print to a socket
 */
 
int SocketPrintf(int socket, const String format, ...){
	va_list ap;
	int res = 0;
	int len;
	char string[48];
	char *sp = string;
	
	ASSERT_FAIL(format)
    
	va_start(ap, format);

	if(socket >= 0){
		vsnprintf(string, 48, format, ap);
		
		for(;;){
			len = strlen(sp);
			res = write(socket, sp, len);
			if(res < 0){
				if(errno == EINTR){
					continue;
				}
				if(errno != EWOULDBLOCK)
					break;
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

	va_end(ap);

	return res;	
}

	
