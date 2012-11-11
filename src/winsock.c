/* ipxwrapper - Winsock functions
 * Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <mswsock.h>
#include <nspapi.h>
#include <wsnwlink.h>

#include "ipxwrapper.h"
#include "common.h"
#include "interface.h"
#include "router.h"
#include "addrcache.h"
#include "addrtable.h"

typedef struct _PROTOCOL_INFO {
	DWORD dwServiceFlags ;
	INT iAddressFamily ;
	INT iMaxSockAddr ;
	INT iMinSockAddr ;
	INT iSocketType ;
	INT iProtocol ;
	DWORD dwMessageSize ;
	void *lpProtocol ;
} PROTOCOL_INFO;

struct sockaddr_ipx_ext {
	short sa_family;
	char sa_netnum[4];
	char sa_nodenum[6];
	unsigned short sa_socket;
	
	unsigned char sa_ptype;
	unsigned char sa_flags;
};

static size_t strsize(void *str, BOOL unicode) {
	return unicode ? 2 + wcslen(str)*2 : 1 + strlen(str);
}

static int do_EnumProtocols(LPINT protocols, LPVOID buf, LPDWORD bsptr, BOOL unicode) {
	int bufsize = *bsptr, rval, i, want_ipx = 0;
	
	PROTOCOL_INFO *pinfo = buf;
	
	if((rval = unicode ? r_EnumProtocolsW(protocols, buf, bsptr) : r_EnumProtocolsA(protocols, buf, bsptr)) == -1) {
		return -1;
	}
	
	if(!protocols) {
		want_ipx = 1;
	}else{
		for(i = 0; protocols[i]; i++) {
			if(protocols[i] == NSPROTO_IPX) {
				want_ipx = 1;
				break;
			}
		}
	}
	
	if(want_ipx) {
		for(i = 0; i < rval; i++) {
			if(pinfo[i].iProtocol == NSPROTO_IPX) {
				return rval;
			}
		}
		
		*bsptr += sizeof(PROTOCOL_INFO) + (unicode ? 8 : 4);
		
		if(*bsptr > bufsize) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return -1;
		}
		
		/* Make sure there is space between the last PROTOCOL_INFO structure
		 * and the protocol names for the extra structure.
		*/
		
		size_t slen = 0, off = 0;
		
		for(i = 0; i < rval; i++) {
			slen += strsize(pinfo[i].lpProtocol, unicode);
		}
		
		char *name_buf = malloc(slen);
		if(!name_buf) {
			SetLastError(ERROR_OUTOFMEMORY);
			return -1;
		}
		
		for(i = 0; i < rval; i++) {
			slen = strsize(pinfo[i].lpProtocol, unicode);
			memcpy(name_buf + off, pinfo[i].lpProtocol, slen);
			
			off += slen;
		}
		
		char *name_dest = ((char*)buf) + sizeof(PROTOCOL_INFO) * (rval + 1);
		
		memcpy(name_dest, name_buf, off);
		free(name_buf);
		
		if(unicode) {
			wcscpy((wchar_t*)(name_dest + off), L"IPX");
		}else{
			strcpy(name_dest + off, "IPX");
		}
		
		for(i = 0, off = 0; i < rval; i++) {
			pinfo[i].lpProtocol = name_dest + off;
			off += strsize(pinfo[i].lpProtocol, unicode);
		}
		
		int ipx_off = rval++;
		
		pinfo[ipx_off].dwServiceFlags = 5641;
		pinfo[ipx_off].iAddressFamily = AF_IPX;
		pinfo[ipx_off].iMaxSockAddr = 16;
		pinfo[ipx_off].iMinSockAddr = 14;
		pinfo[ipx_off].iSocketType = SOCK_DGRAM;
		pinfo[ipx_off].iProtocol = NSPROTO_IPX;
		pinfo[ipx_off].dwMessageSize = 576;
		pinfo[ipx_off].lpProtocol = name_dest + off;
	}
	
	return rval;
}

INT APIENTRY EnumProtocolsA(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	return do_EnumProtocols(protocols, buf, bsptr, FALSE);
}

INT APIENTRY EnumProtocolsW(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	return do_EnumProtocols(protocols, buf, bsptr, TRUE);
}

INT WINAPI WSHEnumProtocols(LPINT protocols, LPWSTR ign, LPVOID buf, LPDWORD bsptr) {
	return do_EnumProtocols(protocols, buf, bsptr, FALSE);
}

SOCKET WSAAPI socket(int af, int type, int protocol)
{
	log_printf(LOG_DEBUG, "socket(%d, %d, %d)", af, type, protocol);
	
	if(af == AF_IPX)
	{
		ipx_socket *nsock = malloc(sizeof(ipx_socket));
		if(!nsock)
		{
			WSASetLastError(ERROR_OUTOFMEMORY);
			return -1;
		}
		
		if((nsock->fd = r_socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		{
			log_printf(LOG_ERROR, "Cannot create UDP socket: %s", w32_error(WSAGetLastError()));
			
			free(nsock);
			return -1;
		}
		
		nsock->flags = IPX_SEND | IPX_RECV | IPX_RECV_BCAST;
		nsock->s_ptype = (protocol ? NSPROTO_IPX - protocol : 0);
		
		log_printf(LOG_INFO, "IPX socket created (fd = %d)", nsock->fd);
		
		lock_sockets();
		HASH_ADD_INT(sockets, fd, nsock);
		unlock_sockets();
		
		return nsock->fd;
	}
	else{
		return r_socket(af, type, protocol);
	}
}

int WSAAPI closesocket(SOCKET sockfd)
{
	int ret = r_closesocket(sockfd);
	
	ipx_socket *sock = get_socket(sockfd);
	if(!sock)
	{
		/* Not an IPX socket */
		return ret;
	}
	
	if(ret == SOCKET_ERROR)
	{
		log_printf(LOG_ERROR, "closesocket(%d): %s", sockfd, w32_error(WSAGetLastError()));
		RETURN(SOCKET_ERROR);
	}
	
	log_printf(LOG_INFO, "IPX socket closed (fd = %d)", sockfd);
	
	if(sock->flags & IPX_BOUND)
	{
		addr_table_remove(sock->port);
	}
	
	HASH_DEL(sockets, sock);
	free(sock);
	
	unlock_sockets();
	
	return 0;
}

static bool _complete_bind_address(struct sockaddr_ipx *addr)
{
	/* Network number 00:00:00:00 is specified as the "current" network, this code
	 * treats it as a wildcard when used for the network OR node numbers.
	 *
	 * According to MSDN 6, IPX socket numbers are unique to systems rather than
	 * interfaces and as such, the same socket number cannot be bound to more than
	 * one interface.
	 *
	 * If you know the above information about IPX socket numbers to be incorrect,
	 * PLEASE email me with corrections!
	*/
	
	/* Iterate over the interfaces list, stop at the first match. */
	
	struct ipx_interface *ifaces = get_ipx_interfaces(), *iface;
	
	addr32_t netnum  = addr32_in(addr->sa_netnum);
	addr48_t nodenum = addr48_in(addr->sa_nodenum);
	
	for(iface = ifaces; iface; iface = iface->next)
	{
		if(
			(netnum == iface->ipx_net || netnum == 0)
			&& (nodenum == iface->ipx_node || nodenum == 0)
		) {
			break;
		}
	}
	
	if(!iface)
	{
		log_printf(LOG_ERROR, "bind failed: no such address");
		
		free_ipx_interface_list(&ifaces);
		
		WSASetLastError(WSAEADDRNOTAVAIL);
		return false;
	}
	
	addr32_out(addr->sa_netnum, iface->ipx_net);
	addr48_out(addr->sa_nodenum, iface->ipx_node);
	
	free_ipx_interface_list(&ifaces);
	
	/* Socket zero signifies automatic allocation. */
	
	if(addr->sa_socket == 0 && (addr->sa_socket = addr_table_auto_socket()) == 0)
	{
		/* Hmmm. We appear to have ran out of sockets?! */
		
		log_printf(LOG_ERROR, "bind failed: out of sockets?!");
		
		WSASetLastError(WSAEADDRNOTAVAIL);
		return false;
	}
	
	return true;
}

int WSAAPI bind(SOCKET fd, const struct sockaddr *addr, int addrlen)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		struct sockaddr_ipx ipxaddr;
		
		if(addrlen < sizeof(ipxaddr) || addr->sa_family != AF_IPX)
		{
			RETURN_WSA(WSAEFAULT, -1);
		}
		
		memcpy(&ipxaddr, addr, sizeof(ipxaddr));
		
		IPX_STRING_ADDR(req_addr_s, addr32_in(ipxaddr.sa_netnum), addr48_in(ipxaddr.sa_nodenum), ipxaddr.sa_socket);
		
		log_printf(LOG_INFO, "bind(%d, %s)", fd, req_addr_s);
		
		if(sock->flags & IPX_BOUND)
		{
			log_printf(LOG_ERROR, "bind failed: socket already bound");
			
			unlock_sockets();
			
			WSASetLastError(WSAEINVAL);
			return -1;
		}
		
		addr_table_lock();
		
		/* Resolve any wildcards in the requested address. */
		
		if(!_complete_bind_address(&ipxaddr))
		{
			addr_table_unlock();
			unlock_sockets();
			
			return -1;
		}
		
		IPX_STRING_ADDR(got_addr_s, addr32_in(ipxaddr.sa_netnum), addr48_in(ipxaddr.sa_nodenum), ipxaddr.sa_socket);
		
		log_printf(LOG_INFO, "bind address: %s", got_addr_s);
		
		/* Check that the address is free. */
		
		if(!addr_table_check(&ipxaddr, !!(sock->flags & IPX_REUSE)))
		{
			/* Address has already been bound. */
			
			addr_table_unlock();
			unlock_sockets();
			
			WSASetLastError(WSAEADDRINUSE);
			return -1;
		}
		
		/* Bind the fake (UDP) socket. */
		
		struct sockaddr_in bind_addr;
		
		bind_addr.sin_family      = AF_INET;
		bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		bind_addr.sin_port        = 0;
		
		if(r_bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1)
		{
			log_printf(
				LOG_ERROR,
				"Binding local UDP socket failed: %s",
				w32_error(WSAGetLastError())
			);
			
			addr_table_unlock();
			unlock_sockets();
			
			return -1;
		}
		
		/* Find out what port we got allocated. */
		
		int al = sizeof(bind_addr);
		
		if(r_getsockname(fd, (struct sockaddr*)&bind_addr, &al) == -1)
		{
			/* Socket state is now inconsistent as the underlying
			 * UDP socket has been bound, but the IPX socket failed
			 * to bind.
			 * 
			 * We also don't know what port number the socket is
			 * bound to and can't unbind, so future bind attempts
			 * will fail.
			*/
			
			log_printf(LOG_ERROR, "getsockname: %s", w32_error(WSAGetLastError()));
			log_printf(LOG_WARNING, "SOCKET STATE IS NOW INCONSISTENT!");
			
			addr_table_unlock();
			unlock_sockets();
			
			return -1;
		}
		
		sock->port = bind_addr.sin_port;
		
		log_printf(LOG_DEBUG, "Bound to local UDP port %hu", ntohs(sock->port));
		
		/* Add to the address table. */
		
		addr_table_add(&ipxaddr, sock->port, !!(sock->flags & IPX_REUSE));
		
		/* Mark the IPX socket as bound. */
		
		memcpy(&(sock->addr), &ipxaddr, sizeof(ipxaddr));
		sock->flags |= IPX_BOUND;
		
		addr_table_unlock();
		unlock_sockets();
		
		return 0;
	}else{
		return r_bind(fd, addr, addrlen);
	}
}

int WSAAPI getsockname(SOCKET fd, struct sockaddr *addr, int *addrlen) {
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(ptr->flags & IPX_BOUND) {
			if(*addrlen < sizeof(struct sockaddr_ipx)) {
				*addrlen = sizeof(struct sockaddr_ipx);
				RETURN_WSA(WSAEFAULT, -1);
			}
			
			memcpy(addr, &(ptr->addr), sizeof(ptr->addr));
			*addrlen = sizeof(struct sockaddr_ipx);
			
			RETURN(0);
		}else{
			RETURN_WSA(WSAEINVAL, -1);
		}
	}else{
		return r_getsockname(fd, addr, addrlen);
	}
}

/* Recieve a packet from an IPX socket
 * addr must be NULL or a region of memory big enough for a sockaddr_ipx
 *
 * The mutex should be locked before calling and will be released before returning
 * The size of the packet will be returned on success, even if it was truncated
*/
static int recv_packet(ipx_socket *sockptr, char *buf, int bufsize, int flags, struct sockaddr_ipx_ext *addr, int addrlen) {
	SOCKET fd = sockptr->fd;
	int is_bound = sockptr->flags & IPX_BOUND;
	int extended_addr = sockptr->flags & IPX_EXT_ADDR;
	
	unlock_sockets();
	
	if(!is_bound) {
		WSASetLastError(WSAEINVAL);
		return -1;
	}
	
	char *recvbuf = malloc(MAX_PKT_SIZE);
	if(!recvbuf) {
		WSASetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	
	struct ipx_packet *packet = (struct ipx_packet*)(recvbuf);
	
	int rval = r_recv(fd, recvbuf, MAX_PKT_SIZE, flags);
	if(rval == -1) {
		free(recvbuf);
		return -1;
	}
	
	if(rval < sizeof(ipx_packet) - 1 || rval != packet->size + sizeof(ipx_packet) - 1)
	{
		log_printf(LOG_ERROR, "Invalid packet received on loopback port!");
		
		free(recvbuf);
		WSASetLastError(WSAEWOULDBLOCK);
		return -1;
	}
	
	if(min_log_level <= LOG_DEBUG)
	{
		IPX_STRING_ADDR(addr_s, addr32_in(packet->src_net), addr48_in(packet->src_node), packet->src_socket);
		
		log_printf(LOG_DEBUG, "Received packet from %s", addr_s);
	}
	
	if(addr) {
		addr->sa_family = AF_IPX;
		memcpy(addr->sa_netnum, packet->src_net, 4);
		memcpy(addr->sa_nodenum, packet->src_node, 6);
		addr->sa_socket = packet->src_socket;
		
		if(extended_addr) {
			if(addrlen >= sizeof(struct sockaddr_ipx_ext)) {
				addr->sa_ptype = packet->ptype;
				addr->sa_flags = 0;
				
				const unsigned char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
				
				if(memcmp(packet->dest_node, f6, 6) == 0) {
					addr->sa_flags |= 0x01;
				}
				
				/* Attempt to get an IPX interface using the
				 * source address to test if the packet claims
				 * to be from one of our interfaces.
				*/
				
				ipx_interface_t *src_iface = ipx_interface_by_addr(
					addr32_in(packet->src_net),
					addr48_in(packet->src_node)
				);
				
				if(src_iface)
				{
					free_ipx_interface(src_iface);
					addr->sa_flags |= 0x02;
				}
			}else{
				log_printf(LOG_ERROR, "IPX_EXTENDED_ADDRESS enabled, but recvfrom called with addrlen %d", addrlen);
			}
		}
	}
	
	memcpy(buf, packet->data, packet->size <= bufsize ? packet->size : bufsize);
	rval = packet->size;
	free(recvbuf);
	
	return rval;
}

int WSAAPI recvfrom(SOCKET fd, char *buf, int len, int flags, struct sockaddr *addr, int *addrlen) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(addr && addrlen && *addrlen < sizeof(struct sockaddr_ipx)) {
			unlock_sockets();
			
			WSASetLastError(WSAEFAULT);
			return -1;
		}
		
		int extended_addr = sockptr->flags & IPX_EXT_ADDR;
		
		int rval = recv_packet(sockptr, buf, len, flags, (struct sockaddr_ipx_ext*)addr, *addrlen);
		
		/* The value pointed to by addrlen is only set if the recv call was
		 * successful, may not be correct.
		*/
		if(rval >= 0 && addr && addrlen) {
			*addrlen = (*addrlen >= sizeof(struct sockaddr_ipx_ext) && extended_addr ? sizeof(struct sockaddr_ipx_ext) : sizeof(struct sockaddr_ipx));
		}
		
		if(rval > len) {
			WSASetLastError(WSAEMSGSIZE);
			return -1;
		}
		
		return rval;
	}else{
		return r_recvfrom(fd, buf, len, flags, addr, addrlen);
	}
}

int WSAAPI recv(SOCKET fd, char *buf, int len, int flags) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		int rval = recv_packet(sockptr, buf, len, flags, NULL, 0);
		
		if(rval > len) {
			WSASetLastError(WSAEMSGSIZE);
			return -1;
		}
		
		return rval;
	}else{
		return r_recv(fd, buf, len, flags);
	}
}

int PASCAL WSARecvEx(SOCKET fd, char *buf, int len, int *flags) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		int rval = recv_packet(sockptr, buf, len, 0, NULL, 0);
		
		if(rval > len) {
			*flags = MSG_PARTIAL;
			
			/* Wording of MSDN is unclear on what should be returned when
			 * an incomplete message is read, I think it should return the
			 * amount of data copied to the buffer.
			*/
			rval = len;
		}else if(rval != -1) {
			*flags = 0;
		}
		
		return rval;
	}else{
		return r_WSARecvEx(fd, buf, len, flags);
	}
}

#define CHECK_OPTLEN(size) \
	if(*optlen < size) {\
		*optlen = size;\
		RETURN_WSA(WSAEFAULT, -1);\
	}\
	*optlen = size;

int WSAAPI getsockopt(SOCKET fd, int level, int optname, char FAR *optval, int FAR *optlen) {
	int* intval = (int*)optval;
	BOOL *bval = (BOOL*)optval;
	
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(level == NSPROTO_IPX) {
			if(optname == IPX_PTYPE) {
				CHECK_OPTLEN(sizeof(int));
				*intval = ptr->s_ptype;
				
				RETURN(0);
			}
			
			if(optname == IPX_FILTERPTYPE) {
				CHECK_OPTLEN(sizeof(int));
				*intval = ptr->f_ptype;
				
				RETURN(0);
			}
			
			if(optname == IPX_MAXSIZE) {
				CHECK_OPTLEN(sizeof(int));
				*intval = MAX_DATA_SIZE;
				
				RETURN(0);
			}
			
			if(optname == IPX_ADDRESS) {
				CHECK_OPTLEN(sizeof(IPX_ADDRESS_DATA));
				
				IPX_ADDRESS_DATA *ipxdata = (IPX_ADDRESS_DATA*)optval;
				
				struct ipx_interface *nic = ipx_interface_by_index(ipxdata->adapternum);
				
				if(!nic) {
					RETURN_WSA(ERROR_NO_DATA, -1);
				}
				
				addr32_out(ipxdata->netnum, nic->ipx_net);
				addr48_out(ipxdata->nodenum, nic->ipx_node);
				
				/* TODO: LAN/WAN detection, link speed detection */
				ipxdata->wan = FALSE;
				ipxdata->status = FALSE;
				ipxdata->maxpkt = MAX_DATA_SIZE;
				ipxdata->linkspeed = 100000; /* 10MBps */
				
				free_ipx_interface(nic);
				
				RETURN(0);
			}
			
			/* NOTE: IPX_MAX_ADAPTER_NUM implies it may be the maximum index
			 * for referencing an IPX interface. This behaviour makes no sense
			 * and a code example in MSDN implies it should be the number of
			 * IPX interfaces, this code follows the latter.
			*/
			if(optname == IPX_MAX_ADAPTER_NUM) {
				CHECK_OPTLEN(sizeof(int));
				
				*intval = ipx_interface_count();
				
				RETURN(0);
			}
			
			if(optname == IPX_EXTENDED_ADDRESS) {
				CHECK_OPTLEN(sizeof(BOOL));
				
				*bval = (ptr->flags & IPX_EXT_ADDR ? TRUE : FALSE);
				
				RETURN(0);
			}
			
			log_printf(LOG_ERROR, "Unknown NSPROTO_IPX socket option passed to getsockopt: %d", optname);
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
		
		if(level == SOL_SOCKET) {
			if(optname == SO_BROADCAST) {
				CHECK_OPTLEN(sizeof(BOOL));
				
				*bval = ptr->flags & IPX_BROADCAST ? TRUE : FALSE;
				RETURN(0);
			}
			
			if(optname == SO_REUSEADDR) {
				CHECK_OPTLEN(sizeof(BOOL));
				
				*bval = ptr->flags & IPX_REUSE ? TRUE : FALSE;
				RETURN(0);
			}
		}
		
		unlock_sockets();
	}
	
	return r_getsockopt(fd, level, optname, optval, optlen);
}

#define SET_FLAG(flag, state) \
	if(state) { \
		sockptr->flags |= (flag); \
	}else{ \
		sockptr->flags &= ~(flag); \
	}

int WSAAPI setsockopt(SOCKET fd, int level, int optname, const char FAR *optval, int optlen) {
	int *intval = (int*)optval;
	BOOL *bval = (BOOL*)optval;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(min_log_level <= LOG_DEBUG) {
			char opt_s[24] = "";
			
			int i;
			for(i = 0; i < optlen && i < 8 && optval; i++) {
				if(i) {
					strcat(opt_s, " ");
				}
				
				sprintf(opt_s + i * 3, "%02X", (unsigned int)(unsigned char)optval[i]);
			}
			
			if(optval) {
				log_printf(LOG_DEBUG, "setsockopt(%d, %d, %d, {%s}, %d)", fd, level, optname, opt_s, optlen);
			}else{
				log_printf(LOG_DEBUG, "setsockopt(%d, %d, %d, NULL, %d)", fd, level, optname, optlen);
			}
		}
		
		if(level == NSPROTO_IPX) {
			if(optname == IPX_PTYPE) {
				sockptr->s_ptype = *intval;
				RETURN(0);
			}
			
			if(optname == IPX_FILTERPTYPE) {
				sockptr->f_ptype = *intval;
				sockptr->flags |= IPX_FILTER;
				
				RETURN(0);
			}
			
			if(optname == IPX_STOPFILTERPTYPE) {
				sockptr->flags &= ~IPX_FILTER;
				
				RETURN(0);
			}
			
			if(optname == IPX_RECEIVE_BROADCAST) {
				SET_FLAG(IPX_RECV_BCAST, *bval);
				
				RETURN(0);
			}
			
			if(optname == IPX_EXTENDED_ADDRESS) {
				SET_FLAG(IPX_EXT_ADDR, *bval);
				RETURN(0);
			}
			
			log_printf(LOG_ERROR, "Unknown NSPROTO_IPX socket option passed to setsockopt: %d", optname);
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
		
		if(level == SOL_SOCKET) {
			if(optname == SO_BROADCAST) {
				SET_FLAG(IPX_BROADCAST, *bval);
				
				RETURN(0);
			}
			
			if(optname == SO_REUSEADDR) {
				SET_FLAG(IPX_REUSE, *bval);
				
				RETURN(0);
			}
		}
		
		unlock_sockets();
	}
	
	return r_setsockopt(fd, level, optname, optval, optlen);
}

/* Send an IPX packet to the specified address.
 * Returns true on success, false on failure.
*/
static int send_packet(const ipx_packet *packet, int len, struct sockaddr *addr, int addrlen)
{
	if(min_log_level <= LOG_DEBUG && addr->sa_family == AF_INET)
	{
		struct sockaddr_in *v4 = (struct sockaddr_in*)(addr);
		
		IPX_STRING_ADDR(
			addr_s,
			addr32_in(packet->dest_net),
			addr48_in(packet->dest_node),
			packet->dest_socket
		);
		
		log_printf(LOG_DEBUG, "Sending packet to %s (%s:%hu)", addr_s, inet_ntoa(v4->sin_addr), ntohs(v4->sin_port));
	}
	
	return (r_sendto(private_socket, (char*)packet, len, 0, addr, addrlen) == len);
}

int WSAAPI sendto(SOCKET fd, const char *buf, int len, int flags, const struct sockaddr *addr, int addrlen) {
	struct sockaddr_ipx_ext *ipxaddr = (struct sockaddr_ipx_ext*)addr;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(!addr || addrlen < sizeof(struct sockaddr_ipx)) {
			RETURN_WSA(WSAEDESTADDRREQ, -1);
		}
		
		if(!(sockptr->flags & IPX_SEND)) {
			RETURN_WSA(WSAESHUTDOWN, -1);
		}
		
		if(!(sockptr->flags & IPX_BOUND)) {
			log_printf(LOG_WARNING, "sendto() on unbound socket, attempting implicit bind");
			
			struct sockaddr_ipx bind_addr;
			
			bind_addr.sa_family = AF_IPX;
			memcpy(bind_addr.sa_netnum, ipxaddr->sa_netnum, 4);
			memset(bind_addr.sa_nodenum, 0, 6);
			bind_addr.sa_socket = 0;
			
			if(bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
				RETURN(-1);
			}
		}
		
		if(len > MAX_DATA_SIZE) {
			RETURN_WSA(WSAEMSGSIZE, -1);
		}
		
		int psize = sizeof(ipx_packet)+len-1;
		
		ipx_packet *packet = malloc(psize);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		packet->ptype = sockptr->s_ptype;
		
		if(sockptr->flags & IPX_EXT_ADDR) {
			if(addrlen >= 15) {
				packet->ptype = ipxaddr->sa_ptype;
			}else{
				log_printf(LOG_ERROR, "IPX_EXTENDED_ADDRESS enabled, but sendto() called with addrlen %d", addrlen);
			}
		}
		
		memcpy(packet->dest_net, ipxaddr->sa_netnum, 4);
		memcpy(packet->dest_node, ipxaddr->sa_nodenum, 6);
		packet->dest_socket = ipxaddr->sa_socket;
		
		unsigned char z6[] = {0,0,0,0,0,0};
		
		if(memcmp(packet->dest_net, z6, 4) == 0) {
			memcpy(packet->dest_net, sockptr->addr.sa_netnum, 4);
		}
		
		memcpy(packet->src_net, sockptr->addr.sa_netnum, 4);
		memcpy(packet->src_node, sockptr->addr.sa_nodenum, 6);
		packet->src_socket = sockptr->addr.sa_socket;
		
		packet->size = htons(len);
		memcpy(packet->data, buf, len);
		
		/* Search the address cache for a real address */
		
		SOCKADDR_STORAGE send_addr;
		size_t addrlen;
		
		int success = 0;
		
		if(addr_cache_get(&send_addr, &addrlen, addr32_in(packet->dest_net), addr48_in(packet->dest_node), packet->dest_socket))
		{
			/* Address is cached. We can send to the real host. */
			
			success = send_packet(
				packet,
				psize,
				(struct sockaddr*)(&send_addr),
				addrlen
			);
		}
		else{
			/* No cached address. Send using broadcast. */
			
			ipx_interface_t *iface = ipx_interface_by_addr(
				addr32_in(packet->src_net),
				addr48_in(packet->src_node)
			);
			
			if(iface && iface->ipaddr)
			{
				/* Iterate over all the IPs associated
				 * with this interface and return
				 * success if the packet makes it out
				 * through any of them.
				*/
				
				ipx_interface_ip_t* ip;
				
				DL_FOREACH(iface->ipaddr, ip)
				{
					struct sockaddr_in bcast;
					
					bcast.sin_family      = AF_INET;
					bcast.sin_port        = htons(main_config.udp_port);
					bcast.sin_addr.s_addr = ip->bcast;
					
					success |= send_packet(
						packet,
						psize,
						(struct sockaddr*)(&bcast),
						sizeof(bcast)
					);
				}
			}
			else{
				/* No IP addresses. */
				
				WSASetLastError(WSAENETDOWN);
				success = 0;
			}
			
			free_ipx_interface(iface);
		}
		
		free(packet);
		
		RETURN(success ? len : -1);
	}else{
		return r_sendto(fd, buf, len, flags, addr, addrlen);
	}
}

int PASCAL shutdown(SOCKET fd, int cmd) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(cmd == SD_RECEIVE || cmd == SD_BOTH) {
			sockptr->flags &= ~IPX_RECV;
		}
		
		if(cmd == SD_SEND || cmd == SD_BOTH) {
			sockptr->flags &= ~IPX_SEND;
		}
		
		RETURN(0);
	}else{
		return r_shutdown(fd, cmd);
	}
}

int PASCAL ioctlsocket(SOCKET fd, long cmd, u_long *argp) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr && cmd == FIONREAD) {
		fd_set fdset;
		struct timeval tv = {0,0};
		
		FD_ZERO(&fdset);
		FD_SET(sockptr->fd, &fdset);
		
		int r = select(1, &fdset, NULL, NULL, &tv);
		if(r == -1) {
			RETURN(-1);
		}else if(r == 0) {
			*(unsigned long*)argp = 0;
			RETURN(0);
		}
		
		char tmp_buf;
		
		if((r = recv_packet(sockptr, &tmp_buf, 1, MSG_PEEK, NULL, 0)) == -1) {
			return -1;
		}
		
		*(unsigned long*)argp = r;
		return 0;
	}
	
	if(sockptr) {
		log_printf(LOG_DEBUG, "ioctlsocket(%d, %d)", fd, cmd);
		unlock_sockets();
	}
	
	return r_ioctlsocket(fd, cmd, argp);
}

int PASCAL connect(SOCKET fd, const struct sockaddr *addr, int addrlen) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(addrlen < sizeof(struct sockaddr_ipx)) {
			RETURN_WSA(WSAEFAULT, -1);
		}
		
		struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
		
		const unsigned char z6[] = {0,0,0,0,0,0};
		
		if(ipxaddr->sa_family == AF_UNSPEC || (ipxaddr->sa_family == AF_IPX && memcmp(ipxaddr->sa_nodenum, z6, 6) == 0)) {
			if(!(sockptr->flags & IPX_CONNECTED)) {
				RETURN(0);
			}
			
			struct sockaddr_ipx dc_addr;
			dc_addr.sa_family = AF_UNSPEC;
			
			sockptr->flags &= ~IPX_CONNECTED;
			
			RETURN(0);
		}
		
		if(ipxaddr->sa_family != AF_IPX) {
			RETURN_WSA(WSAEAFNOSUPPORT, -1);
		}
		
		if(!(sockptr->flags & IPX_BOUND)) {
			log_printf(LOG_WARNING, "connect() on unbound socket, attempting implicit bind");
			
			struct sockaddr_ipx bind_addr;
			
			bind_addr.sa_family = AF_IPX;
			memcpy(bind_addr.sa_netnum, ipxaddr->sa_netnum, 4);
			memset(bind_addr.sa_nodenum, 0, 6);
			bind_addr.sa_socket = 0;
			
			if(bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
				RETURN(-1);
			}
		}
		
		memcpy(&(sockptr->remote_addr), addr, sizeof(*ipxaddr));
		sockptr->flags |= IPX_CONNECTED;
		
		RETURN(0);
	}else{
		return r_connect(fd, addr, addrlen);
	}
}

int PASCAL send(SOCKET fd, const char *buf, int len, int flags) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(!(sockptr->flags & IPX_CONNECTED)) {
			RETURN_WSA(WSAENOTCONN, -1);
		}
		
		int ret = sendto(fd, buf, len, 0, (struct sockaddr*)&(sockptr->remote_addr), sizeof(struct sockaddr_ipx));
		RETURN(ret);
	}else{
		return r_send(fd, buf, len, flags);
	}
}

int PASCAL getpeername(SOCKET fd, struct sockaddr *addr, int *addrlen) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(!(sockptr->flags & IPX_CONNECTED)) {
			RETURN_WSA(WSAENOTCONN, -1);
		}
		
		if(*addrlen < sizeof(struct sockaddr_ipx)) {
			RETURN_WSA(WSAEFAULT, -1);
		}
		
		memcpy(addr, &(sockptr->remote_addr), sizeof(struct sockaddr_ipx));
		*addrlen = sizeof(struct sockaddr_ipx);
		
		RETURN(0);
	}else{
		return r_getpeername(fd, addr, addrlen);
	}
}
