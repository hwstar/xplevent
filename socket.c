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
	Bool denyAll;
	SockAclListEntryPtr_t allowHead;
	SockAclListEntryPtr_t allowTail;
	SockAclListEntryPtr_t denyHead;
	SockAclListEntryPtr_t denyTail;
} SockAclList_t;

typedef SockAclList_t * SockAclListPtr_t;


/*
* Initialize a V4 or V6 address mask 
*
* Arguments:
*
* 1. A pointer to the address storage where the mask bits will be saved.
* 2. The address family to use during mask bit generation.
* 3. The number of mask bits to generate starting from the most significant bit.
*
* Return value:
*
* None
*/

static void addrMaskInit(struct sockaddr_storage *mask, sa_family_t family, uint8_t num)
{
	uint8_t i, octet, bit;
	
	/* Initialize mask data */
	*mask = (struct sockaddr_storage){.ss_family = family};
	
	if(AF_INET6 == family){ /* IPV6 */
		struct sockaddr_in6 *m6 = (struct sockaddr_in6 *) mask;
		
		if(num > 128){ /* Clip to 128 */
			num = 128;
		}
		/* Set the required number of mask bits using network byte order */
		for(i = 0; i < num; i++){
			octet =  i >> 3;
			bit = i & 7;
			((uint8_t *) &m6->sin6_addr)[octet] |= (0x80 >> (7 - bit));
		}
		
		
		
	}
	else if (AF_INET == family){ /* IPV4 */
		struct sockaddr_in *m4 = (struct sockaddr_in *) mask;
	
		if(num > 32){
			num = 32; /* Clip to 32 */
		}
		/* Set the required number of mask bits using network byte order */
		for(i = 0; i < num; i++){
			octet =  i >> 3;
			bit = i & 7;
			((uint8_t *) &m4->sin_addr)[octet] |= (0x80 >> (7 - bit));
		}
	}
	

}


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
		uint8_t *p1 = (uint8_t *)&ip1_6.sin6_addr;
		uint8_t *p2 = (uint8_t *)&ip2_6.sin6_addr;
		uint8_t *m = (uint8_t *)&mask_6.sin6_addr;
		
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
* Parse a IPV4 or IPV6 CIDR address and generate a new list entry if the address is valid
*
* Arguments:
*
* 1. A talloc context to use for the result, and for internal transitory data.
* 2. The CIDR string in IPADDRESS/MASKBITS format.
* 3. A pointer to a pointer variable where the resulting holding structure can be saved.
*
*
* Return value:
*
* PASS if parse was successful, otherwise FAIL
*/


static Bool parseCIDR(TALLOC_CTX *ctx, String cidrString, SockAclListEntryPtr_t *e)
{
	String *parts = NULL;
	struct addrinfo *ai = NULL;
	struct addrinfo hints = (struct addrinfo){.ai_family = AF_UNSPEC, .ai_flags = AI_NUMERICHOST};
	SockAclListEntryPtr_t new;
	Bool res = PASS;
	int rv;
	uint8_t masklen;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(e)
	ASSERT_FAIL(cidrString)

	
	/* Split the mask and address portions */
	
	MALLOC_FAIL(parts = UtilSplitString(ctx, cidrString, '/'))
	
	/* Allocate a holding structure */
	
	MALLOC_FAIL(new = talloc_zero(ctx, SockAclListEntry_t))
	new->magic = SE_MAGIC;
	
	/* Parse the address portion */
	
	if((rv = getaddrinfo(parts[0], NULL, &hints, &ai))){
		res = FAIL;
		debug(DEBUG_UNEXPECTED, "%s: Invalid IP address: %s: %s", __func__, parts[0], gai_strerror(rv));
	}
	
	
	/* Copy the binary address info to our holding struct */
	if(res != FAIL){
		new->check = *((struct sockaddr_storage *) (ai[0].ai_addr));
		
		if(!parts[1]){ /* If no mask bits specified */
			masklen = 128; /* set to the max */
		}
		else{
			unsigned ml; 
			/* Get the number of bits to mask from the second substring */
			if(FAIL == UtilStou( parts[1], &ml)){
				debug(DEBUG_UNEXPECTED, "%s: Not a number", __func__);
				res = FAIL;
			}
			else{
				/* Sanity check the mask length */
				if(AF_INET6 == new->check.ss_family){
					if(ml > 128){
						debug(DEBUG_UNEXPECTED, "%s: 128 mask bits max", __func__);
						res = FAIL;
					}
					else{
						masklen = (uint8_t) ml;
					}
				}
				else if(AF_INET == new->check.ss_family){
					if(ml > 32){
						debug(DEBUG_UNEXPECTED, "%s: 32 mask bits max", __func__);
						res = FAIL;
					}
					else{
						masklen = (uint8_t) ml;
					}
				}
				else{ /* Don't know what it is, so fail */
					debug(DEBUG_UNEXPECTED, "%s: Unknown address family", __func__);
					res = FAIL;
				}
			}
		}
	}

	
	if(res != FAIL){
		/* Initialize the mask */
		addrMaskInit(&new->mask, new->check.ss_family, masklen);
	}
	
	/* Free the address structure */
	if(ai){
		freeaddrinfo(ai);	
	}
	

	/* Free the split strings */
	
	talloc_free(parts);

	/* Test for errors, and clean up if so. */
	
	if(FAIL == res){
		talloc_free(new);
		*e = NULL;
	}
	else{
		*e = new;
	}

	
	/* Return PASS/FAIL result */
	
	return res;	
}



/*
 * Convert the address passed in to a printable string, return a talloc'd string.
 * 
 * Arguments:
 * 
 * 1. A talloc context to hang the string off of.
 * 2. A pointer to the address to convert.
 * 
 * Return value:
 * 
 * A string with the printable address. This must be freed with talloc_free to
 * avoid memory leaks.
 */
 
String SocketPrintableAddress(TALLOC_CTX *ctx, struct sockaddr_storage *theAddr)
{
	String s;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(theAddr)
	
	
	MALLOC_FAIL(s = talloc_array(ctx, char, INET6_ADDRSTRLEN))
	
	s[0] = 0;
	
	
	switch(theAddr->ss_family) {
        case AF_INET:
            inet_ntop(AF_INET, &(((struct sockaddr_in *)theAddr)->sin_addr),
                    s, INET6_ADDRSTRLEN);
            break;

        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)theAddr)->sin6_addr),
                    s, INET6_ADDRSTRLEN);
            break;

        default:
             snprintf(s, INET6_ADDRSTRLEN, "Unknown Address Family: %d", theAddr->ss_family);
            break;
	}
	return s;
		
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
		deny |= sameNet(clientAddr, &e->check, &e->mask);
	}
	
	/* Check allow */
	
	for(e = al->allowHead; e && !allow; e = e->next){
		ASSERT_FAIL(SE_MAGIC == e->magic)
		allow |= sameNet(clientAddr, &e->check, &e->mask);
	}
	
	if(!deny && !al->denyAll && !allow){ /* Nothing specified, so accept it all */
		return PASS;
	}
	
	if(allow){ /* If something was allowed, then accept it */
		return PASS;
	}
	
	/*  Denied, so fail */
		
	return FAIL;
	
}

/*
 * Generate an access control list from a comma separated list of of Allow and Deny IP addresses (IPV4 and/or IPV6)
 * 
 *
 * Arguments:
 *
 * 1. The talloc context to hang the access control list off of.
 * 2. A pointer to a place to store the ACL list object 
 * 3. A comma delimited set of V4 and/or V6 IP addresses with optional CIDR notation to allow, or NULL
 * 4. A comma delimited set of V4 and/or V6 IP addresses with optional CIDR notation to deny, 
 *    or the keyword ALL to deny everything and only accept IP addresses on the allow list, or NULL
 *
 * Returns:
 * 
 * Boolean: PASS indicates no parse errors were detected. FAIL indicates a parse error.
 */ 

Bool SocketGenACL(TALLOC_CTX *ctx, void **acl, String allowList, String denyList)
{
	String s;
	String *addrs;
	Bool res = PASS;
	unsigned i;
	SockAclListPtr_t al;
	SockAclListEntryPtr_t e;
	

	ASSERT_FAIL(acl)
	ASSERT_FAIL(ctx)
	
	
	if(allowList || denyList){
		MALLOC_FAIL(al = talloc_zero(ctx, SockAclList_t))
		al->magic = SH_MAGIC;
	}
	else{
		al = NULL;
	}
	
	/* Parse the allow list */
	if(allowList){
		/* Get rid of white space */
		s = UtilStripWhite(al, allowList);
		/* Split the strings into substrings, each containing an address and optional CIDR mask */
		addrs = UtilSplitString(al, s, ',');
		/* Free stripped string */
		talloc_free(s);
		/* Process each address */
		for(i = 0; addrs[i]; i++){
			if(FAIL == parseCIDR(al, addrs[i], &e)){
				debug(DEBUG_UNEXPECTED, "Allow address parse error");
				res = FAIL;
				break;
			}
			/* Insert into list */
			if(!al->allowHead){
				al->allowHead = al->allowTail = e;
			}
			else{
				e->prev = al->allowTail;
				e->prev->next = e;
				al->allowTail = e;
			}
		}
		talloc_free(addrs);
	}
	
	/* If no parse errors in the allow list, parse the deny list if it exists */
	if((res == PASS) && denyList){
		if(!strcmp("ALL", denyList)){
			al->denyAll = TRUE;
		}
		else{
			/* Get rid of white space */
			s = UtilStripWhite(al, denyList);
			/* Split the strings into substrings, each containing an address and optional CIDR mask */
			addrs = UtilSplitString(al, s, ',');
			/* Free stripped string */
			talloc_free(s);
			/* Process each address */
			for(i = 0; addrs[i]; i++){
				if(FAIL == parseCIDR(al, addrs[i], &e)){
					debug(DEBUG_UNEXPECTED, "Deny address parse error");
					res = FAIL;
					break;
				}
				/* Insert into list */
				if(!al->denyHead){
					al->denyHead = al->denyTail = e;
				}
				else{
					e->prev = al->denyTail;
					e->prev->next = e;
					al->denyTail = e;
				}
			}
			talloc_free(addrs);
		}
	}
	
	/* Check for errors */
	if(res == FAIL){
		/* Clean up the mess */
		*acl = NULL;
		if(al){
			talloc_free(al);
		}
	}
	else{
		/* Give the caller the opaque ACL object */
		*acl = al;
	}
		
	return res;
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
 * Boolean. Return PASS if successful,
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
* Create socket(s) for a single bind address.
* Supports both IPV4 and IPV6 sockets.
* 
* 
* Parameters:
*
* 1. Bind address string. If NULL is passed in, or it matches the keyword ALL, all interfaces will be bound 
*    Multiple interfaces may be specified with a comma.
* 2. Service name string. Can be either a service name or a port number.
* 3. Address family. Usually set to AF_UNSPEC.
* 4. Socket type. Set to SOCK_STREAM for a TCP connection.
* 5. User Object
* 6. Callback function (see below)
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
* 5. User Object
*
* Return Value:
*
* PASS if list creation is to continue, FAIL if list creation is to be aborted.
*
*/
	

Bool SocketCreate(String bindaddr, String service, int family, int socktype, void *userObj,
	int (*addsock)(int sock, void *addr, int addrlen, int family, int socktype, void *userObj))
{
	struct addrinfo hints = (struct addrinfo){0};
	struct addrinfo *list, *p;
	int sock = -1, res;
	int sockcount = 0;

	
	ASSERT_FAIL(service)
	ASSERT_FAIL(addsock);	


	/* Init the hints struct for getaddrinfo */

	hints.ai_family = family;
	hints.ai_socktype = socktype;
	if(bindaddr == NULL){
		hints.ai_flags = AI_PASSIVE;
	}

	/* Get the address list */
	if((res = getaddrinfo(bindaddr, service, &hints, &list)) == -1){
		debug(DEBUG_EXPECTED, "%s: getaddrinfo failed: %s", __func__, gai_strerror(res));
		return FAIL;
	}

	for(p = list; p != NULL; p = p->ai_next){ // Traverse the list
	
	
		if((sock = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, p->ai_protocol)) == -1){
			debug(DEBUG_EXPECTED,"%s: Call to socket failed with %s, continuing...", __func__, strerror(errno));
			continue;
		}		

		/* Callback to have caller do something with the socket */

		sockcount++;

		if((*addsock)(sock, p->ai_addr, p->ai_addrlen, p->ai_family, p->ai_socktype, userObj) == FALSE){
			break;
		}
	}

	freeaddrinfo(list);

	if(!sockcount){
		debug(DEBUG_EXPECTED, "%s: could not create, bind or listen on a socket. Bindaddr: %s service: %s",
		__func__, bindaddr, service);
		return FAIL;
	}

	return PASS;
}


/* 
*
* Helper to create a listening socket list.
* Supports both IPV4 and IPV6 sockets.
* 
* 
* Parameters:
*
* 1. Talloc context for transitory data
* 2. Bind address string. If NULL is passed in, or it matches the keyword ALL, all interfaces will be bound 
*    Multiple interfaces may be specified by using a comma to delimit each one.
* 3. Service name string. Can be either a service name or a port number.
* 4. Address family. Usually set to AF_UNSPEC.
* 5. Socket type. Set to SOCK_STREAM for a TCP connection.
* 6. User Object
* 7. Callback function (see below)
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
* 5. User Object
*
* Return Value:
*
* PASS if list creation is to continue, FAIL if list creation is to be aborted.
*
*/
	
Bool SocketCreateMultiple(TALLOC_CTX *ctx, String bindaddr, String service, int family, int socktype, void *userObj,
	int (*addsock)(int sock, void *addr, int addrlen, int family, int socktype, void *userObj))
{
	int i;
	int res;
	String ba = NULL;
	String *bindList = NULL;
	
	ASSERT_FAIL(ctx)
	ASSERT_FAIL(service)
	ASSERT_FAIL(addsock)

	/* IF the string bindaddr is NULL, or contains the word A::, then we want to create a passive socket */
	
	ba = (bindaddr && strcmp(bindaddr,"ALL")) ? bindaddr : NULL;

	if(ba){
		/* Make the bind list from the comma delimited string */
		String strippedBA;
		strippedBA = UtilStripWhite(ctx, ba);
		bindList = UtilSplitString(ctx, strippedBA, ',');
		talloc_free(strippedBA);
		/* Iterate the list creating all the necessary listening sockets */
		for(i = 0; bindList[i]; i++){
			res = SocketCreate(bindList[i], service, family, socktype, addsock, userObj);
			if(FAIL == res){
				debug(DEBUG_UNEXPECTED, "%s: Failure binding: %s", __func__, bindList[i]);
				talloc_free(bindList);
				return FAIL;
			}
		}
		/* Free the list */
		talloc_free(bindList);
		return PASS;
	}
	else{
		/* Create a passive socket */
		return SocketCreate(ba, service, family, socktype, NULL, addsock);
	}

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

	struct addrinfo hints = (struct addrinfo) {.ai_family = family, .ai_socktype = socktype};
	struct addrinfo *list = NULL, *p = NULL, *ipv6 = NULL, *ipv4 = NULL;
	int sock, res;

	ASSERT_FAIL(host)
	ASSERT_FAIL(service)

	
	/* Get the address list */
	if((res = getaddrinfo(host, service, &hints, &list)) == -1){
		debug(DEBUG_ACTION, "%s: getaddrinfo failed: %s", __func__, gai_strerror(res));
		return -1;
	}
	for(p = list; p ; p = p->ai_next){
		if((!ipv6) && (p->ai_family == PF_INET6))
			ipv6 = p;
		if((!ipv4) && (p->ai_family == PF_INET))
			ipv4 = p;
	}

	if(!ipv4 && !ipv6){
		debug(DEBUG_ACTION,"%s: Could not find a suitable IP address to connect to", __func__);
		return -1;
	}
	
	p = (ipv6) ? ipv6 : ipv4; /* Prefer IPV6 over IPV4 */

	/* Create a socket for talking to the daemon program. */

	sock = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol );
	if(sock == -1) {
		freeaddrinfo(list);
		debug(DEBUG_ACTION, "%s: Could not create ip socket: %s", __func__, strerror(errno));
		return -1;
	}
	
	/* Connect the socket */

	if(connect(sock, (struct sockaddr *) p->ai_addr, p->ai_addrlen)) {
		freeaddrinfo(list);
		debug(DEBUG_ACTION, "%s: Could not connect to inet host:port '%s:%s'.", __func__, host, service);
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
	
	ASSERT_FAIL(length)
	ASSERT_FAIL(ctx)
	
	MALLOC_FAIL(line = talloc_zero_array(ctx, char, lsize))
	
	*length = 0;
	
	for(;;){
		if(*length >= (lsize - 2)){
			/* Double the buffer size and re-allocate */
			lsize <<= 1;
			debug(DEBUG_ACTION, "%s: Doubling the line buffer to %d", __func__, lsize);
			MALLOC_FAIL(line = talloc_realloc(ctx, line, char, lsize))
		}
		
		res = read(socket, &c, 1);
			

		if(res < 0){
			if(errno == EINTR){
				continue;
			}
			if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
				if(SocketWaitReadReady(socket, 5000) == FAIL){
					debug(DEBUG_UNEXPECTED, "%s: Time out on socket", __func__);
					talloc_free(line);
					return NULL;
				}
				continue;
			}
			debug(DEBUG_UNEXPECTED, "%s: Read error on fd %d: %s", __func__, socket, strerror(errno));
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
			debug(DEBUG_ACTION,"%s: EOF on line", __func__);
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
					debug(DEBUG_UNEXPECTED, "%s: Time out on socket", __func__);
				}
				debug(DEBUG_UNEXPECTED, "%s: Socket write error: %s", __func__, strerror(errno));
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

/*
 * Compare two socket addresses verbatim
 */
 
Bool SocketCompareAddrVerbatim(const struct sockaddr_storage *ip1, const struct sockaddr_storage *ip2)
{
	struct sockaddr_storage mask;
	
	if (ip1->ss_family != ip2->ss_family) {
		/* Never on the same net. */
		return FALSE;
	}
	/* Enable every last mask bit */
	addrMaskInit(&mask, ip1->ss_family, 128);
	
	/* Compare and return */
	return sameNet(ip1, ip2, &mask);	
}

	
