/* ipxwrapper - Winsock functions
 * Copyright (C) 2008-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define WINSOCK_API_LINKAGE

#include <winsock2.h>
#include <windows.h>
#include <wsipx.h>
#include <mswsock.h>
#include <nspapi.h>
#include <wsnwlink.h>

#include "ipxwrapper.h"
#include "common.h"
#include "interface.h"
#include "router.h"
#include "addrcache.h"
#include "ethernet.h"

struct sockaddr_ipx_ext {
	short sa_family;
	char sa_netnum[4];
	char sa_nodenum[6];
	unsigned short sa_socket;
	
	unsigned char sa_ptype;
	unsigned char sa_flags;
};

static size_t strsize(void *str, bool unicode)
{
	return unicode
		? (wcslen(str) * 2) + 2
		: strlen(str) + 1;
}

static int _max_ipx_payload(void)
{
	if(ipx_encap_type == ENCAP_TYPE_PCAP)
	{
		/* TODO: Use real interface MTU */
		
		switch(main_config.frame_type)
		{
			case FRAME_TYPE_ETH_II:
			case FRAME_TYPE_NOVELL:
				return 1500 - sizeof(novell_ipx_packet);
				
			case FRAME_TYPE_LLC:
				return 1500 - (3 + sizeof(novell_ipx_packet));
		}
		
		abort();
	}
	else if(ipx_encap_type == ENCAP_TYPE_DOSBOX)
	{
		/* include/ipx.h in DOSBox:
		 * #define IPXBUFFERSIZE 1424
		*/
		
		return 1424;
	}
	else{
		return MAX_DATA_SIZE;
	}
}

#define PUSH_NAME(name) \
{ \
	int i = 0; \
	do { \
		if(unicode) \
		{ \
			*(wchar_t*)(name_base) = name[i]; \
			name_base += 2; \
		} \
		else{ \
			*name_base = name[i]; \
			name_base += 1; \
		} \
	} while(name[i++] != '\0'); \
}

static int do_EnumProtocols(LPINT protocols, LPVOID buf, LPDWORD bsptr, bool unicode)
{
	/* Determine which IPX protocols should be added to the list. */
	
	bool want_ipx   = !protocols;
	bool want_spx   = !protocols;
	bool want_spxii = !protocols;
	
	for(int i = 0; protocols && protocols[i]; ++i)
	{
		if(protocols[i] == NSPROTO_IPX)
		{
			want_ipx = true;
		}
		else if(protocols[i] == NSPROTO_SPX)
		{
			want_spx = true;
		}
		else if(protocols[i] == NSPROTO_SPXII)
		{
			want_spxii = true;
		}
	}
	
	/* Stash the true buffer size and call EnumProtocols to get any
	 * protocols provided by the OS.
	*/
	
	DWORD bufsize = *bsptr;
	
	int rval = unicode
		? r_EnumProtocolsW(protocols, buf, bsptr)
		: r_EnumProtocolsA(protocols, buf, bsptr);
	
	if(rval == -1 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
	{
		return -1;
	}
	
	/* Determine how much additional buffer space is needed and check that
	 * the originally provided size is enough.
	*/
	
	if(want_ipx)
	{
		*bsptr += sizeof(PROTOCOL_INFO) + (strlen("IPX") + 1) * (!!unicode + 1);
	}
	
	if(want_spx)
	{
		*bsptr += sizeof(PROTOCOL_INFO) + (strlen("SPX") + 1) * (!!unicode + 1);
	}
	
	if(want_spxii)
	{
		*bsptr += sizeof(PROTOCOL_INFO) + (strlen("SPX II") + 1) * (!!unicode + 1);
	}
	
	if(*bsptr > bufsize)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return -1;
	}
	
	if(rval == -1)
	{
		return -1;
	}
	
	/* Remove any IPX/SPX protocols from the list the native EnumProtocols
	 * function returned; this is to force the data for the IPX types to be
	 * the same under all Windows versions.
	*/
	
	PROTOCOL_INFO *pinfo = buf;
	
	for(int i = 0; i < rval;)
	{
		if(pinfo[i].iAddressFamily == AF_IPX)
		{
			pinfo[i] = pinfo[--rval];
		}
		else{
			++i;
		}
	}
	
	/* The names pointed to by lpProtocol may be stored in the buffer, past
	 * the PROTOCOL_INFO structures.
	 * 
	 * We want to overwrite that block, so move any such names out.
	*/
	
	size_t name_buf_size = 0;
	
	for(int i = 0; i < rval; ++i)
	{
		if(pinfo[i].lpProtocol >= (char*)(buf) && pinfo[i].lpProtocol < (char*)(buf) + bufsize)
		{
			name_buf_size += strsize(pinfo[i].lpProtocol, unicode);
		}
	}
	
	char *name_buf = malloc(name_buf_size);
	if(!name_buf)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	
	for(int i = 0, off = 0; i < rval; ++i)
	{
		if(pinfo[i].lpProtocol >= (char*)(buf) && pinfo[i].lpProtocol < (char*)(buf) + bufsize)
		{
			int len = strsize(pinfo[i].lpProtocol, unicode);
			
			pinfo[i].lpProtocol = memcpy(name_buf + off, pinfo[i].lpProtocol, len);
			off += len;
		}
	}
	
	/* Calculate buffer offset so start adding names at. */
	
	char *name_base = (char*)(buf) + sizeof(PROTOCOL_INFO) * (rval + !!want_ipx + !!want_spx + !!want_spxii);
	
	/* Append additional PROTOCOL_INFO structures and name strings. */
	
	if(want_ipx)
	{
		pinfo[rval].dwServiceFlags = XP_CONNECTIONLESS | XP_MESSAGE_ORIENTED | XP_SUPPORTS_BROADCAST | XP_SUPPORTS_MULTICAST | XP_FRAGMENTATION;
		pinfo[rval].iAddressFamily = AF_IPX;
		pinfo[rval].iMaxSockAddr   = 16;
		pinfo[rval].iMinSockAddr   = 14;
		pinfo[rval].iSocketType    = SOCK_DGRAM;
		pinfo[rval].iProtocol      = NSPROTO_IPX;
		pinfo[rval].dwMessageSize  = 576;
		pinfo[rval].lpProtocol     = name_base;
		
		PUSH_NAME("IPX");
		
		++rval;
	}
	
	if(want_spx)
	{
		pinfo[rval].dwServiceFlags = XP_GUARANTEED_DELIVERY | XP_GUARANTEED_ORDER | XP_PSEUDO_STREAM | XP_FRAGMENTATION;
		pinfo[rval].iAddressFamily = AF_IPX;
		pinfo[rval].iMaxSockAddr   = 16;
		pinfo[rval].iMinSockAddr   = 14;
		pinfo[rval].iSocketType    = SOCK_STREAM;
		pinfo[rval].iProtocol      = NSPROTO_SPX;
		pinfo[rval].dwMessageSize  = 0xFFFFFFFF;
		pinfo[rval].lpProtocol     = name_base;
		
		PUSH_NAME("SPX");
		
		++rval;
	}
	
	if(want_spxii)
	{
		pinfo[rval].dwServiceFlags = XP_GUARANTEED_DELIVERY | XP_GUARANTEED_ORDER | XP_PSEUDO_STREAM | XP_FRAGMENTATION;
		pinfo[rval].iAddressFamily = AF_IPX;
		pinfo[rval].iMaxSockAddr   = 16;
		pinfo[rval].iMinSockAddr   = 14;
		pinfo[rval].iSocketType    = SOCK_STREAM;
		pinfo[rval].iProtocol      = NSPROTO_SPXII;
		pinfo[rval].dwMessageSize  = 0xFFFFFFFF;
		pinfo[rval].lpProtocol     = name_base;
		
		PUSH_NAME("SPX II");
		
		++rval;
	}
	
	/* Replace the names we pulled out of the buffer earlier. */
	
	for(int i = 0; i < rval; ++i)
	{
		if(pinfo[i].lpProtocol >= name_buf && pinfo[i].lpProtocol < name_buf + name_buf_size)
		{
			int size = strsize(pinfo[i].lpProtocol, unicode);
			
			pinfo[i].lpProtocol = memcpy(name_base, pinfo[i].lpProtocol, size);
			name_base += size;
		}
	}
	
	free(name_buf);
	
	return rval;
}

INT APIENTRY EnumProtocolsA(LPINT protocols, LPVOID buf, LPDWORD bsptr)
{
	return do_EnumProtocols(protocols, buf, bsptr, false);
}

INT APIENTRY EnumProtocolsW(LPINT protocols, LPVOID buf, LPDWORD bsptr)
{
	return do_EnumProtocols(protocols, buf, bsptr, true);
}

INT WINAPI WSHEnumProtocols(LPINT protocols, LPWSTR ign, LPVOID buf, LPDWORD bsptr)
{
	return do_EnumProtocols(protocols, buf, bsptr, false);
}

static int recv_queue_adjust_refcount(ipx_recv_queue *recv_queue, int adj)
{
	EnterCriticalSection(&(recv_queue->refcount_lock));
	int new_refcount = (recv_queue->refcount += adj);
	LeaveCriticalSection(&(recv_queue->refcount_lock));
	
	return new_refcount;
}

static void release_recv_queue(ipx_recv_queue *recv_queue)
{
	int new_refcount = recv_queue_adjust_refcount(recv_queue, -1);
	
	if(new_refcount == 0)
	{
		DeleteCriticalSection(&(recv_queue->refcount_lock));
		free(recv_queue);
	}
}

SOCKET WSAAPI socket(int af, int type, int protocol)
{
	log_printf(LOG_DEBUG, "socket(%d, %d, %d)", af, type, protocol);
	
	if(af == AF_IPX)
	{
		if(type == SOCK_DGRAM)
		{
			ipx_socket *nsock = malloc(sizeof(ipx_socket));
			if(!nsock)
			{
				WSASetLastError(ERROR_OUTOFMEMORY);
				return -1;
			}
			
			ipx_recv_queue *recv_queue = malloc(sizeof(ipx_recv_queue));
			if(recv_queue == NULL)
			{
				free(nsock);
				
				WSASetLastError(ERROR_OUTOFMEMORY);
				return -1;
			}
			
			if(!InitializeCriticalSectionAndSpinCount(&(recv_queue->refcount_lock), 0x80000000))
			{
				log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
				WSASetLastError(GetLastError());
				
				free(recv_queue);
				free(nsock);
				return -1;
			}
			
			recv_queue->refcount = 1;
			recv_queue->n_ready = 0;
			
			for(int i = 0; i < RECV_QUEUE_MAX_PACKETS; ++i)
			{
				recv_queue->sizes[i] = IPX_RECV_QUEUE_FREE;
			}
			
			if((nsock->fd = r_socket(AF_INET, SOCK_DGRAM, 0)) == -1)
			{
				log_printf(LOG_ERROR, "Cannot create UDP socket: %s", w32_error(WSAGetLastError()));
				
				release_recv_queue(recv_queue);
				free(nsock);
				return -1;
			}
			
			nsock->flags = IPX_SEND | IPX_RECV | IPX_RECV_BCAST;
			nsock->s_ptype = (protocol ? protocol - NSPROTO_IPX : 0);
			
			nsock->recv_queue = recv_queue;
			
			log_printf(LOG_INFO, "IPX socket created (fd = %d)", nsock->fd);
			
			lock_sockets();
			HASH_ADD_INT(sockets, fd, nsock);
			unlock_sockets();
			
			return nsock->fd;
		}
		else if(type == SOCK_STREAM)
		{
			if(ipx_encap_type == ENCAP_TYPE_PCAP)
			{
				log_printf(LOG_WARNING, "Application attempted to create an SPX socket, this isn't supported when using Ethernet encapsulation");
				
				WSASetLastError(WSAEPROTONOSUPPORT);
				return -1;
			}
			else if(ipx_encap_type == ENCAP_TYPE_DOSBOX)
			{
				log_printf(LOG_WARNING, "Application attempted to create an SPX socket, this isn't supported when using DOSBox encapsulation");
				
				WSASetLastError(WSAEPROTONOSUPPORT);
				return -1;
			}
			
			if(protocol != 0 && protocol != NSPROTO_SPX && protocol != NSPROTO_SPXII)
			{
				log_printf(LOG_DEBUG, "Unknown protocol (%d) for AF_INET/SOCK_STREAM", protocol);
				
				WSASetLastError(WSAEPROTONOSUPPORT);
				return -1;
			}
			
			ipx_socket *nsock = malloc(sizeof(ipx_socket));
			if(!nsock)
			{
				WSASetLastError(ERROR_OUTOFMEMORY);
				return -1;
			}
			
			if((nsock->fd = r_socket(AF_INET, SOCK_STREAM, 0)) == -1)
			{
				log_printf(LOG_ERROR, "Cannot create TCP socket: %s", w32_error(WSAGetLastError()));
				free(nsock);
				
				return -1;
			}
			
			nsock->flags = IPX_IS_SPX;
			
			if(protocol == NSPROTO_SPXII)
			{
				nsock->flags |= IPX_IS_SPXII;
			}
			
			nsock->recv_queue = NULL;
			
			log_printf(LOG_INFO, "SPX socket created (fd = %d)", nsock->fd);
			
			lock_sockets();
			HASH_ADD_INT(sockets, fd, nsock);
			unlock_sockets();
			
			return nsock->fd;
		}
		else{
			log_printf(LOG_DEBUG, "Unknown type (%d) for family AF_IPX", type);
			
			WSASetLastError(WSAEINVAL);
			return -1;
		}
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
		
		unlock_sockets();
		return -1;
	}
	
	log_printf(LOG_INFO, "Socket %d (%s) closed", sockfd, (sock->flags & IPX_IS_SPX ? "SPX" : "IPX"));
	
	if(sock->recv_queue != NULL)
	{
		release_recv_queue(sock->recv_queue);
	}
	
	if(sock->flags & IPX_BOUND)
	{
		CloseHandle(sock->sock_mut);
	}
	
	HASH_DEL(sockets, sock);
	free(sock);
	
	unlock_sockets();
	
	return 0;
}

static HANDLE _open_socket_mutex(uint16_t socket, bool exclusive)
{
	char mutex_name[256];
	snprintf(mutex_name, sizeof(mutex_name), "ipxwrapper_socket_%hu", socket);
	
	HANDLE mutex = CreateMutex(NULL, FALSE, mutex_name);
	if(!mutex)
	{
		log_printf(LOG_ERROR, "Error when creating mutex %s: %s",
			mutex_name, w32_error(GetLastError()));
	}
	
	if(GetLastError() == ERROR_ALREADY_EXISTS && exclusive)
	{
		CloseHandle(mutex);
		return NULL;
	}
	
	return mutex;
}

bool _complete_bind(ipx_socket *sock)
{
	if(ntohs(sock->addr.sa_socket) == 0)
	{
		uint16_t socknum = 1024;
		
		do {
			HANDLE mutex = _open_socket_mutex(socknum, true);
			if(mutex)
			{
				sock->addr.sa_socket = htons(socknum);
				sock->sock_mut       = mutex;
				sock->flags         |= IPX_BOUND;
				
				return true;
			}
		} while(socknum++ != 65535);
	}
	else{
		if((sock->sock_mut = _open_socket_mutex(
			ntohs(sock->addr.sa_socket), !(sock->flags & IPX_REUSE))))
		{
			sock->flags |= IPX_BOUND;
			
			return true;
		}
	}
	
	return false;
}

static bool _resolve_bind_address(ipx_socket *sock, const struct sockaddr_ipx *addr)
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
		return false;
	}
	
	addr32_out(sock->addr.sa_netnum,  iface->ipx_net);
	addr48_out(sock->addr.sa_nodenum, iface->ipx_node);
	sock->addr.sa_socket = addr->sa_socket;
	
	free_ipx_interface_list(&ifaces);
	return true;
}

int WSAAPI bind(SOCKET fd, const struct sockaddr *addr, int addrlen)
{
	ipx_socket *sock = get_socket_wait_for_ready(fd, IPX_READY_TIMEOUT);
	
	if(sock)
	{
		struct sockaddr_ipx ipxaddr;
		
		if(addrlen < sizeof(ipxaddr) || addr->sa_family != AF_IPX)
		{
			WSASetLastError(WSAEFAULT);
			
			unlock_sockets();
			return -1;
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
		
		sock->addr.sa_family = AF_IPX;
		
		/* Resolve any wildcards in the requested address. */
		
		if(!_resolve_bind_address(sock, &ipxaddr))
		{
			unlock_sockets();
			
			WSASetLastError(WSAEADDRNOTAVAIL);
			return -1;
		}
		
		/* Check that the address is free. */
		
		if(!_complete_bind(sock))
		{
			unlock_sockets();
			
			WSASetLastError(WSAEADDRINUSE);
			return -1;
		}
		
		IPX_STRING_ADDR(got_addr_s, addr32_in(sock->addr.sa_netnum), addr48_in(sock->addr.sa_nodenum), sock->addr.sa_socket);
		
		log_printf(LOG_INFO, "bind address: %s", got_addr_s);
		
		/* Bind the underlying socket. */
		
		struct sockaddr_in bind_addr;
		
		bind_addr.sin_family      = AF_INET;
		bind_addr.sin_addr.s_addr = htonl(sock->flags & IPX_IS_SPX ? INADDR_ANY : INADDR_LOOPBACK);
		bind_addr.sin_port        = 0;
		
		if(r_bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1)
		{
			log_printf(LOG_ERROR, "Binding local socket failed: %s", w32_error(WSAGetLastError()));
			
			CloseHandle(sock->sock_mut);
			sock->flags &= ~IPX_BOUND;
			
			unlock_sockets();
			
			return -1;
		}
		
		/* Find out what port we got allocated. */
		
		int al = sizeof(bind_addr);
		
		if(r_getsockname(fd, (struct sockaddr*)&bind_addr, &al) == -1)
		{
			/* Socket state is now inconsistent because the
			 * underlying socket has been bound, but we don't know
			 * the port number and can't finish binding the IPX one
			 * as a result.
			 * 
			 * In short, the socket is unusable now.
			*/
			
			log_printf(LOG_ERROR, "Cannot get local port of socket: %s", w32_error(WSAGetLastError()));
			log_printf(LOG_WARNING, "Socket %d is NOW INCONSISTENT!", fd);
			
			CloseHandle(sock->sock_mut);
			sock->flags &= ~IPX_BOUND;
			
			unlock_sockets();
			
			return -1;
		}
		
		sock->port = bind_addr.sin_port;
		log_printf(LOG_DEBUG, "Bound to local port %hu", ntohs(sock->port));
		
		unlock_sockets();
		
		return 0;
	}
	else{
		return r_bind(fd, addr, addrlen);
	}
}

int WSAAPI getsockname(SOCKET fd, struct sockaddr *addr, int *addrlen)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(sock->flags & IPX_BOUND)
		{
			if(*addrlen < sizeof(struct sockaddr_ipx))
			{
				*addrlen = sizeof(struct sockaddr_ipx);
				
				WSASetLastError(WSAEFAULT);
				
				unlock_sockets();
				return -1;
			}
			
			memcpy(addr, &(sock->addr), sizeof(sock->addr));
			*addrlen = sizeof(struct sockaddr_ipx);
			
			unlock_sockets();
			return 0;
		}
		else{
			WSASetLastError(WSAEINVAL);
			
			unlock_sockets();
			return -1;
		}
	}
	else{
		return r_getsockname(fd, addr, addrlen);
	}
}

static BOOL reclaim_socket(ipx_socket *sockptr, int lookup_fd)
{
	/* Reclaim the lock, ensure the socket hasn't been
	 * closed by the application (naughty!) while we were
	 * waiting.
	*/
	
	ipx_socket *reclaim_sock = get_socket(lookup_fd);
	if(sockptr != reclaim_sock)
	{
		log_printf(LOG_DEBUG, "Application closed socket while inside a WinSock call!");
		
		if(reclaim_sock)
		{
			unlock_sockets();
		}
		
		return FALSE;
	}
	
	return TRUE;
}

static int recv_pump(ipx_socket *sockptr, BOOL block)
{
	int fd = sockptr->fd;
	
	if(!block)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		
		FD_SET(fd, &read_fds);
		
		struct timeval timeout = { 0, 0 };
		
		int r = r_select(-1, &read_fds, NULL, NULL, &timeout);
		if(r == -1)
		{
			unlock_sockets();
			return -1;
		}
		else if(r == 0)
		{
			/* No packet waiting in underlying recv buffer. */
			return 0;
		}
	}
	
	ipx_recv_queue *queue = sockptr->recv_queue;
	
	int recv_slot = -1;
	
	for(int i = 0; i < RECV_QUEUE_MAX_PACKETS; ++i)
	{
		if(queue->sizes[i] == IPX_RECV_QUEUE_FREE)
		{
			queue->sizes[i] = IPX_RECV_QUEUE_LOCKED;
			recv_queue_adjust_refcount(queue, 1);
			
			recv_slot = i;
			break;
		}
	}
	
	if(recv_slot < 0)
	{
		/* No free recv_queue slots. */
		return 0;
	}
	
	unlock_sockets();
	
	int r = r_recv(fd, (char*)(queue->data[recv_slot]), MAX_PKT_SIZE, 0);
	
	if(!reclaim_socket(sockptr, fd))
	{
		/* The application closed the socket while we were in the recv() call.
		 * Just discard our handle, let the queue be destroyed.
		*/
		
		release_recv_queue(queue);
		WSASetLastError(WSAENOTSOCK);
		return -1;
	}
	
	if(r == -1)
	{
		queue->sizes[recv_slot] = IPX_RECV_QUEUE_FREE;
		release_recv_queue(queue);
		unlock_sockets();
		return -1;
	}
	
	struct ipx_packet *packet = (struct ipx_packet*)(queue->data[recv_slot]);
	
	if(r < sizeof(ipx_packet) - 1 || r != packet->size + sizeof(ipx_packet) - 1)
	{
		log_printf(LOG_ERROR, "Invalid packet received on loopback port!");
		
		queue->sizes[recv_slot] = IPX_RECV_QUEUE_FREE;
		release_recv_queue(queue);
		
		WSASetLastError(WSAEWOULDBLOCK);
		unlock_sockets();
		return -1;
	}
	
	queue->sizes[recv_slot] = r;
	queue->ready[queue->n_ready] = recv_slot;
	++(queue->n_ready);
	
	return 1;
}

/* Recieve a packet from an IPX socket
 * addr must be NULL or a region of memory big enough for a sockaddr_ipx
 *
 * The mutex should be locked before calling and will be released before returning
 * The size of the packet will be returned on success, even if it was truncated
*/
static int recv_packet(ipx_socket *sockptr, char *buf, int bufsize, int flags, struct sockaddr_ipx_ext *addr, int addrlen) {
	if(!(sockptr->flags & IPX_BOUND))
	{
		unlock_sockets();
		
		WSASetLastError(WSAEINVAL);
		return -1;
	}
	
	/* Loop here in case some crazy application does concurrent recv() calls
	 * and they race between putting packets on the queue and handling them.
	*/
	while(sockptr->recv_queue->n_ready < 1)
	{
		if(recv_pump(sockptr, TRUE) < 0)
		{
			/* Socket closed or recv() error. */
			return -1;
		}
	}
	
	int slot = sockptr->recv_queue->ready[0];
	
	struct ipx_packet *packet = (struct ipx_packet*)(sockptr->recv_queue->data[slot]);
	assert(sockptr->recv_queue->sizes[slot] >= 0);
	
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
		
		if(sockptr->flags & IPX_EXT_ADDR) {
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
	int rval = packet->size;
	
	if((flags & MSG_PEEK) == 0)
	{
		sockptr->recv_queue->sizes[slot] = IPX_RECV_QUEUE_FREE;
		
		--(sockptr->recv_queue->n_ready);
		memmove(&(sockptr->recv_queue->ready[0]), &(sockptr->recv_queue->ready[1]), (sockptr->recv_queue->n_ready * sizeof(int)));
	}
	
	unlock_sockets();
	
	return rval;
}

int WSAAPI recvfrom(SOCKET fd, char *buf, int len, int flags, struct sockaddr *addr, int *addrlen)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			/* Quoth the MSDN:
			 * 
			 * For stream-oriented sockets such as those of type
			 * SOCK_STREAM, a call to recvfrom returns as much
			 * information as is currently available-up to the size
			 * of the buffer specified.
			 * 
			 * The from and fromlen parameters are ignored for
			 * connection-oriented sockets.
			*/
			
			unlock_sockets();
			
			return r_recv(fd, buf, len, flags);
		}
		else{
			if(addr && addrlen && *addrlen < sizeof(struct sockaddr_ipx))
			{
				unlock_sockets();
				
				WSASetLastError(WSAEFAULT);
				return -1;
			}
			
			int extended_addr = sock->flags & IPX_EXT_ADDR;
			
			int rval = recv_packet(sock, buf, len, flags, (struct sockaddr_ipx_ext*)(addr), (addrlen ? *addrlen : 0));
			
			/* The value pointed to by addrlen is only set if the
			 * recv call was successful, may not be correct.
			*/
			
			if(rval >= 0 && addr && addrlen)
			{
				*addrlen = (*addrlen >= sizeof(struct sockaddr_ipx_ext) && extended_addr ? sizeof(struct sockaddr_ipx_ext) : sizeof(struct sockaddr_ipx));
			}
			
			if(rval > len)
			{
				WSASetLastError(WSAEMSGSIZE);
				return -1;
			}
			
			return rval;
		}
	}
	else{
		return r_recvfrom(fd, buf, len, flags, addr, addrlen);
	}
}

int WSAAPI recv(SOCKET fd, char *buf, int len, int flags)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			unlock_sockets();
			
			return r_recv(fd, buf, len, flags);
		}
		else{
			int rval = recv_packet(sock, buf, len, flags, NULL, 0);
			
			if(rval > len)
			{
				WSASetLastError(WSAEMSGSIZE);
				return -1;
			}
			
			return rval;
		}
	}
	else{
		return r_recv(fd, buf, len, flags);
	}
}

int PASCAL WSARecvEx(SOCKET fd, char *buf, int len, int *flags)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			unlock_sockets();
			
			return r_WSARecvEx(fd, buf, len, flags);
		}
		else{
			int rval = recv_packet(sock, buf, len, 0, NULL, 0);
			
			if(rval > len)
			{
				*flags = MSG_PARTIAL;
				
				/* Wording of MSDN is unclear on what should be
				 * returned when a partial packet is read.
				 * 
				 * I _THINK_ it should return the amount of data
				 * actually copied to the buffer.
				 * 
				 * Windows 95/98: Returns -1
				 * Windows 2000/XP: Returns len
				*/
				
				rval = len;
			}
			else if(rval != -1)
			{
				*flags = 0;
			}
			
			return rval;
		}
	}
	else{
		return r_WSARecvEx(fd, buf, len, flags);
	}
}

#define GETSOCKOPT_OPTLEN(size) \
	if(*optlen < size) \
	{\
		*optlen = size;\
		WSASetLastError(WSAEFAULT); \
		unlock_sockets(); \
		return -1; \
	}\
	*optlen = size;

#define RETURN_INT_OPT(val) \
	GETSOCKOPT_OPTLEN(sizeof(int)); \
	*((int*)(optval)) = (val); \
	unlock_sockets(); \
	return 0;

#define RETURN_BOOL_OPT(val) \
	GETSOCKOPT_OPTLEN(sizeof(BOOL)); \
	*((BOOL*)(optval)) = (val) ? TRUE : FALSE; \
	unlock_sockets(); \
	return 0;

int WSAAPI getsockopt(SOCKET fd, int level, int optname, char FAR *optval, int FAR *optlen)
{
	ipx_socket *sock = get_socket_wait_for_ready(fd, IPX_READY_TIMEOUT);
	
	if(sock)
	{
		if(level == NSPROTO_IPX)
		{
			if(optname == IPX_PTYPE)
			{
				/* NOTE: Windows 95/98 only write to the first
				 * byte of the buffer, leaving the rest
				 * uninitialised. Windows 2000/XP write all 4
				 * bytes.
				 * 
				 * Both require optlen to be at least 4.
				*/
				
				RETURN_INT_OPT(sock->s_ptype);
			}
			else if(optname == IPX_FILTERPTYPE)
			{
				RETURN_INT_OPT(sock->f_ptype);
			}
			else if(optname == IPX_MAXSIZE)
			{
				RETURN_INT_OPT(_max_ipx_payload());
			}
			else if(optname == IPX_ADDRESS)
			{
				GETSOCKOPT_OPTLEN(sizeof(IPX_ADDRESS_DATA));
				
				IPX_ADDRESS_DATA *ipxdata = (IPX_ADDRESS_DATA*)(optval);
				
				struct ipx_interface *nic = ipx_interface_by_index(ipxdata->adapternum);
				
				if(!nic)
				{
					WSASetLastError(ERROR_NO_DATA);
					
					unlock_sockets();
					return -1;
				}
				
				addr32_out(ipxdata->netnum, nic->ipx_net);
				addr48_out(ipxdata->nodenum, nic->ipx_node);
				
				ipxdata->wan       = FALSE;
				ipxdata->status    = FALSE;
				ipxdata->maxpkt    = _max_ipx_payload();
				ipxdata->linkspeed = 100000; /* 10MBps */
				
				free_ipx_interface(nic);
				
				unlock_sockets();
				return 0;
			}
			else if(optname == IPX_MAX_ADAPTER_NUM)
			{
				/* NOTE: IPX_MAX_ADAPTER_NUM implies it may be
				 * the maximum index for referencing an IPX
				 * interface. This behaviour makes no sense and
				 * a code example in MSDN implies it should be
				 * the number of IPX interfaces, this code
				 * follows the latter behaviour.
				*/
				
				RETURN_INT_OPT(ipx_interface_count());
			}
			else if(optname == IPX_EXTENDED_ADDRESS)
			{
				RETURN_BOOL_OPT(sock->flags & IPX_EXT_ADDR);
			}
			else{
				log_printf(LOG_ERROR, "Unknown NSPROTO_IPX socket option passed to getsockopt: %d", optname);
				
				WSASetLastError(WSAENOPROTOOPT);
				
				unlock_sockets();
				return -1;
			}
		}
		else if(level == SOL_SOCKET)
		{
			if(optname == SO_BROADCAST)
			{
				RETURN_BOOL_OPT(sock->flags & IPX_BROADCAST);
			}
			else if(optname == SO_REUSEADDR)
			{
				RETURN_BOOL_OPT(sock->flags & IPX_REUSE);
			}
		}
		
		unlock_sockets();
	}
	
	return r_getsockopt(fd, level, optname, optval, optlen);
}

#define SETSOCKOPT_OPTLEN(s) \
	if(optlen < s) \
	{ \
		WSASetLastError(WSAEFAULT); \
		unlock_sockets(); \
		return -1; \
	}

#define SET_FLAG(flag) \
	SETSOCKOPT_OPTLEN(sizeof(BOOL)); \
	if(*((BOOL*)(optval))) \
	{ \
		sock->flags |= (flag); \
	} \
	else{ \
		sock->flags &= ~(flag); \
	} \
	unlock_sockets(); \
	return 0;

int WSAAPI setsockopt(SOCKET fd, int level, int optname, const char FAR *optval, int optlen)
{
	{
		char opt_s[24] = "";
		
		for(int i = 0; i < optlen && i < 8 && optval; i++)
		{
			if(i)
			{
				strcat(opt_s, " ");
			}
			
			sprintf(opt_s + i * 3, "%02X", (unsigned int)(unsigned char)optval[i]);
		}
		
		if(optval)
		{
			log_printf(LOG_CALL, "setsockopt(%d, %d, %d, {%s}, %d)", fd, level, optname, opt_s, optlen);
		}
		else{
			log_printf(LOG_CALL, "setsockopt(%d, %d, %d, NULL, %d)", fd, level, optname, optlen);
		}
	}
	
	int *intval = (int*)(optval);
	
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(level == NSPROTO_IPX)
		{
			if(optname == IPX_PTYPE)
			{
				SETSOCKOPT_OPTLEN(sizeof(int));
				
				sock->s_ptype = *intval;
				
				unlock_sockets();
				return 0;
			}
			else if(optname == IPX_FILTERPTYPE)
			{
				SETSOCKOPT_OPTLEN(sizeof(int));
				
				sock->f_ptype = *intval;
				sock->flags |= IPX_FILTER;
				
				unlock_sockets();
				return 0;
			}
			else if(optname == IPX_STOPFILTERPTYPE)
			{
				sock->flags &= ~IPX_FILTER;
				
				unlock_sockets();
				return 0;
			}
			else if(optname == IPX_RECEIVE_BROADCAST)
			{
				SET_FLAG(IPX_RECV_BCAST);
			}
			else if(optname == IPX_EXTENDED_ADDRESS)
			{
				SET_FLAG(IPX_EXT_ADDR);
			}
			else{
				log_printf(LOG_ERROR, "Unknown NSPROTO_IPX socket option passed to setsockopt: %d", optname);
				
				WSASetLastError(WSAENOPROTOOPT);
				
				unlock_sockets();
				return -1;
			}
		}
		else if(level == SOL_SOCKET)
		{
			if(optname == SO_BROADCAST)
			{
				SET_FLAG(IPX_BROADCAST);
			}
			else if(optname == SO_REUSEADDR)
			{
				SET_FLAG(IPX_REUSE);
			}
			else if(optname == SO_LINGER && !(sock->flags & IPX_IS_SPX))
			{
				/* Setting SO_LINGER only has an effect on
				 * stream sockets and fails on datagrams, but
				 * Jane's Combat Simulations: WWWII Fighters
				 * depends on the call succeeding.
				*/
				
				log_printf(LOG_DEBUG, "Ignoring SO_LINGER on IPX socket %d", sock->fd);
				unlock_sockets();
				
				return 0;
			}
			else if(optname == 16399)
			{
				/* As far as I can tell, this socket option
				 * isn't defined anywhere and no tested version
				 * of Windows accepts it on an IPX socket, but
				 * Jane's Combat Simulations: WWWII Fighters
				 * uses it and won't work if the call fails.
				*/
				
				log_printf(LOG_DEBUG, "Ignoring unknown SOL_SOCKET option 16399 on socket %d", sock->fd);
				unlock_sockets();
				
				return 0;
			}
		}
		
		unlock_sockets();
	}
	
	int r = r_setsockopt(fd, level, optname, optval, optlen);
	log_printf(LOG_CALL, "r_setsockopt = %d, WSAGetLastError = %d", r, (int)(WSAGetLastError()));
	
	return r;
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
			src_addr,
			addr32_in(packet->src_net),
			addr48_in(packet->src_node),
			packet->src_socket
		);
		
		IPX_STRING_ADDR(
			dest_addr,
			addr32_in(packet->dest_net),
			addr48_in(packet->dest_node),
			packet->dest_socket
		);
		
		log_printf(LOG_DEBUG, "Sending packet from %s to %s (%s:%hu)", src_addr, dest_addr, inet_ntoa(v4->sin_addr), ntohs(v4->sin_port));
	}
	
	return (r_sendto(private_socket, (char*)packet, len, 0, addr, addrlen) == len);
}

static DWORD ipx_send_packet(
	uint8_t type,
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const void *data,
	size_t data_size)
{
	{
		IPX_STRING_ADDR(src_addr, src_net, src_node, src_socket);
		IPX_STRING_ADDR(dest_addr, dest_net, dest_node, dest_socket);
		
		log_printf(LOG_DEBUG, "Sending %u byte payload from %s to %s",
			(unsigned int)(data_size), src_addr, dest_addr);
	}
	
	if(ipx_encap_type == ENCAP_TYPE_PCAP)
	{
		ipx_interface_t *iface = ipx_interface_by_addr(src_net, src_node);
		if(iface)
		{
			/* Calculate the frame size and check we can actually
			 * fit this much data in it.
			*/
			
			size_t frame_size;
			
			switch(main_config.frame_type)
			{
				case FRAME_TYPE_ETH_II:
					frame_size = ethII_frame_size(data_size);
					break;
					
				case FRAME_TYPE_NOVELL:
					frame_size = novell_frame_size(data_size);
					break;
					
				case FRAME_TYPE_LLC:
					frame_size = llc_frame_size(data_size);
					break;
			}
			
			/* TODO: Check frame_size against interface MTU */
			
			if(frame_size == 0)
			{
				log_printf(LOG_ERROR,
					"Tried sending a %u byte packet, too large for the selected frame type",
					(unsigned int)(data_size));
				
				return WSAEMSGSIZE;
			}
			
			log_printf(LOG_DEBUG, "...frame size = %u", (unsigned int)(frame_size));
			
			/* Serialise the frame. */
			
			void *frame = malloc(frame_size);
			if(!frame)
			{
				return ERROR_OUTOFMEMORY;
			}
			
			switch(main_config.frame_type)
			{
				case FRAME_TYPE_ETH_II:
					ethII_frame_pack(frame,
						type,
						src_net,  src_node,  src_socket,
						dest_net, dest_node, dest_socket,
						data, data_size);
					break;
					
				case FRAME_TYPE_NOVELL:
					novell_frame_pack(frame,
						type,
						src_net,  src_node,  src_socket,
						dest_net, dest_node, dest_socket,
						data, data_size);
					break;
					
				case FRAME_TYPE_LLC:
					llc_frame_pack(frame,
						type,
						src_net,  src_node,  src_socket,
						dest_net, dest_node, dest_socket,
						data, data_size);
					break;
			}
			
			/* Transmit the frame. */
			
			if(pcap_sendpacket(iface->pcap, (void*)(frame), frame_size) == 0)
			{
				free(frame);
				return ERROR_SUCCESS;
			}
			else{
				log_printf(LOG_ERROR, "Could not transmit Ethernet frame");
				
				free(frame);
				return WSAENETDOWN;
			}
		}
		else{
			/* It's a bug if we actually hit this. */
			return WSAENETDOWN;
		}
	}
	else if(ipx_encap_type == ENCAP_TYPE_DOSBOX)
	{
		if(dosbox_state != DOSBOX_CONNECTED)
		{
			return WSAENETDOWN;
		}
		else if(src_net != dosbox_local_netnum || src_node != dosbox_local_nodenum)
		{
			return WSAENETDOWN;
		}
		else{
			size_t packet_size = sizeof(novell_ipx_packet) + data_size;
			
			novell_ipx_packet *packet = malloc(packet_size);
			if(packet == NULL)
			{
				return ERROR_OUTOFMEMORY;
			}
			
			packet->checksum = 0xFFFF;
			packet->length = htons(sizeof(novell_ipx_packet) + data_size);
			packet->hops = 0;
			packet->type = type;
			
			addr32_out(packet->dest_net, dest_net);
			addr48_out(packet->dest_node, dest_node);
			packet->dest_socket = dest_socket;
			
			addr32_out(packet->src_net, src_net);
			addr48_out(packet->src_node, src_node);
			packet->src_socket = src_socket;
			
			memcpy(packet->data, data, data_size);
			
			DWORD error = ERROR_SUCCESS;
			
			if(r_sendto(private_socket, (const void*)(packet), packet_size, 0, (struct sockaddr*)(&dosbox_server_addr), sizeof(dosbox_server_addr)) < 0)
			{
				error = WSAGetLastError();
				log_printf(LOG_ERROR, "Error sending DOSBox IPX packet: %s", w32_error(error));
			}
			
			free(packet);
			
			return error;
		}
	}
	else{
		int packet_size = sizeof(ipx_packet) - 1 + data_size;
		
		ipx_packet *packet = malloc(packet_size);
		if(!packet)
		{
			return ERROR_OUTOFMEMORY;
		}
		
		packet->ptype = type;
		
		addr32_out(packet->src_net, src_net);
		addr48_out(packet->src_node, src_node);
		packet->src_socket = src_socket;
		
		addr32_out(packet->dest_net, dest_net);
		addr48_out(packet->dest_node, dest_node);
		packet->dest_socket = dest_socket;
		
		packet->size = htons(data_size);
		memcpy(packet->data, data, data_size);
		
		/* Search the address cache for an IP address */
		
		SOCKADDR_STORAGE send_addr;
		size_t addrlen;
		
		DWORD send_error = ERROR_SUCCESS;
		BOOL send_ok     = FALSE;
		
		if(addr_cache_get(&send_addr, &addrlen, dest_net, dest_node, dest_socket))
		{
			/* IP address is cached. We can send directly to the
			 * host.
			*/
			
			if(send_packet(
				packet,
				packet_size,
				(struct sockaddr*)(&send_addr),
				addrlen))
			{
				send_ok = TRUE;
			}
			else{
				send_error = WSAGetLastError();
			}
		}
		else{
			/* No cached address. Send using broadcast. */
			
			ipx_interface_t *iface = ipx_interface_by_addr(src_net, src_node);
			
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
					
					if(send_packet(
						packet,
						packet_size,
						(struct sockaddr*)(&bcast),
						sizeof(bcast)))
					{
						send_ok = TRUE;
					}
					else{
						send_error = WSAGetLastError();
					}
				}
			}
			else{
				/* No IP addresses; can't transmit */
				
				free_ipx_interface(iface);
				free(packet);
				
				return WSAENETUNREACH;
			}
			
			free_ipx_interface(iface);
		}
		
		free(packet);
		
		return send_ok
			? ERROR_SUCCESS
			: send_error;
	}
}

int WSAAPI sendto(SOCKET fd, const char *buf, int len, int flags, const struct sockaddr *addr, int addrlen)
{
	struct sockaddr_ipx_ext *ipxaddr = (struct sockaddr_ipx_ext*)addr;
	
	ipx_socket *sock = get_socket_wait_for_ready(fd, IPX_READY_TIMEOUT);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			unlock_sockets();
			
			return r_send(sock->fd, buf, len, flags);
		}
		
		if(!addr)
		{
			/* Destination address required. */
			
			WSASetLastError(WSAEDESTADDRREQ);
			
			unlock_sockets();
			return -1;
		}
		
		if(addrlen < sizeof(struct sockaddr_ipx))
		{
			/* Destination address too small. */
			
			WSASetLastError(WSAEFAULT);
			
			unlock_sockets();
			return -1;
		}
		
		if(!(sock->flags & IPX_SEND))
		{
			/* Socket has been shut down for sending. */
			
			WSASetLastError(WSAESHUTDOWN);
			
			unlock_sockets();
			return -1;
		}
		
		if(!(sock->flags & IPX_BOUND))
		{
			log_printf(LOG_WARNING, "sendto() on unbound socket, attempting implicit bind");
			
			struct sockaddr_ipx bind_addr;
			
			bind_addr.sa_family = AF_IPX;
			memcpy(bind_addr.sa_netnum, ipxaddr->sa_netnum, 4);
			memset(bind_addr.sa_nodenum, 0, 6);
			bind_addr.sa_socket = 0;
			
			if(bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1)
			{
				unlock_sockets();
				return -1;
			}
		}
		
		if(len > _max_ipx_payload())
		{
			WSASetLastError(WSAEMSGSIZE);
			
			unlock_sockets();
			return -1;
		}
		
		uint8_t type = sock->s_ptype;
		
		if(sock->flags & IPX_EXT_ADDR)
		{
			if(addrlen >= 15)
			{
				type = ipxaddr->sa_ptype;
			}
			else{
				log_printf(LOG_DEBUG, "IPX_EXTENDED_ADDRESS enabled, sendto called with addrlen %d", addrlen);
			}
		}
		
		addr32_t src_net    = addr32_in(sock->addr.sa_netnum);
		addr48_t src_node   = addr48_in(sock->addr.sa_nodenum);
		uint16_t src_socket = sock->addr.sa_socket;
		
		addr32_t dest_net    = addr32_in(ipxaddr->sa_netnum);
		addr48_t dest_node   = addr48_in(ipxaddr->sa_nodenum);
		uint16_t dest_socket = ipxaddr->sa_socket;
		
		if(dest_net == addr32_in((unsigned char[]){0x00,0x00,0x00,0x00}))
		{
			dest_net = src_net;
		}
		
		DWORD error = ipx_send_packet(type, src_net, src_node, src_socket, dest_net, dest_node, dest_socket, buf, len);
		
		unlock_sockets();
		
		if(error == ERROR_SUCCESS)
		{
			return len;
		}
		else{
			WSASetLastError(error);
			return -1;
		}
	}
	else{
		return r_sendto(fd, buf, len, flags, addr, addrlen);
	}
}

int PASCAL shutdown(SOCKET fd, int cmd)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			unlock_sockets();
			return r_shutdown(fd, cmd);
		}
		else{
			if(cmd == SD_RECEIVE || cmd == SD_BOTH)
			{
				sock->flags &= ~IPX_RECV;
			}
			
			if(cmd == SD_SEND || cmd == SD_BOTH)
			{
				sock->flags &= ~IPX_SEND;
			}
			
			unlock_sockets();
			return 0;
		}
	}
	else{
		return r_shutdown(fd, cmd);
	}
}

int PASCAL ioctlsocket(SOCKET fd, long cmd, u_long *argp)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		log_printf(LOG_DEBUG, "ioctlsocket(%d, %d)", fd, cmd);
		
		if(cmd == FIONREAD && !(sock->flags & IPX_IS_SPX))
		{
			while(1)
			{
				int r = recv_pump(sock, FALSE);
				if(r < 0)
				{
					/* Error in recv_pump() */
					return -1;
				}
				
				if(r == 0)
				{
					/* No more packets ready to read from underlying socket. */
					break;
				}
			}
			
			unsigned long accumulated_packet_data = 0;
			
			for(int i = 0; i < sock->recv_queue->n_ready; ++i)
			{
				const ipx_packet *packet = (const ipx_packet*)(sock->recv_queue->data[ sock->recv_queue->ready[i] ]);
				accumulated_packet_data += packet->size;
			}
			
			unlock_sockets();
			
			*(unsigned long*)(argp) = accumulated_packet_data;
			return 0;
		}
		
		unlock_sockets();
	}
	
	return r_ioctlsocket(fd, cmd, argp);
}

#define MAX_CONNECT_BCAST_ADDRS 64

static void _connect_bcast_push(uint32_t *bcast_addrs, int *bcast_count, ipx_interface_ip_t *ips)
{
	ipx_interface_ip_t *ip;
	DL_FOREACH(ips, ip)
	{
		for(int i = 0; i < *bcast_count; ++i)
		{
			if(bcast_addrs[i] == ip->bcast)
			{
				goto NEXT;
			}
		}
		
		if(*bcast_count < MAX_CONNECT_BCAST_ADDRS)
		{
			bcast_addrs[(*bcast_count)++] = ip->bcast;
		}
		
		NEXT:;
	}
}

static int _connect_spx(ipx_socket *sock, struct sockaddr_ipx *ipxaddr)
{
	if(ipxaddr->sa_family != AF_IPX)
	{
		unlock_sockets();
		
		WSASetLastError(WSAEAFNOSUPPORT);
		return -1;
	}
	
	/* SPX is implemented here as a very thin layer over the top of TCP, so
	 * we need to ask all the hosts on the network if they have an
	 * IPXWrapper SPX socket listening on the requested address.
	 * 
	 * We begin by determining which IP broadcast addresses to send the
	 * lookup requests to.
	 * 
	 * If the socket is already bound, we broadcast to all of the IP subnets
	 * on that interface.
	 * 
	 * If the socket is unbound, we broadcast to all IPX interfaces, this is
	 * the best we can do since every interface has the same network number
	 * by default.
	*/
	
	uint32_t bcast_addrs[MAX_CONNECT_BCAST_ADDRS];
	int bcast_count = 0;
	
	if(sock->flags & IPX_BOUND)
	{
		ipx_interface_t *iface = ipx_interface_by_addr(
			addr32_in(sock->addr.sa_netnum),
			addr48_in(sock->addr.sa_nodenum));
		
		if(iface)
		{
			_connect_bcast_push(bcast_addrs, &bcast_count, iface->ipaddr);
		}
		
		free_ipx_interface(iface);
	}
	else{
		ipx_interface_t *interfaces = get_ipx_interfaces();
		
		ipx_interface_t *iface;
		DL_FOREACH(interfaces, iface)
		{
			_connect_bcast_push(bcast_addrs, &bcast_count, iface->ipaddr);
		}
		
		free_ipx_interface_list(&interfaces);
	}
	
	if(bcast_count == 0)
	{
		/* There isn't anywhere for us to probe. */
		
		unlock_sockets();
		
		WSASetLastError(WSAENETUNREACH);
		return -1;
	}
	
	{
		IPX_STRING_ADDR(
			addr_s,
			addr32_in(ipxaddr->sa_netnum),
			addr48_in(ipxaddr->sa_nodenum),
			ipxaddr->sa_socket
		);
		
		log_printf(LOG_DEBUG, "Trying to connect SPX socket %d to %s", sock->fd, addr_s);
	}
	
	/* Construct the request packet. */
	
	spxlookup_req_t req;
	memset(&req, 0, sizeof(req));
	
	memcpy(req.net, ipxaddr->sa_netnum, 4);
	memcpy(req.node, ipxaddr->sa_nodenum, 6);
	req.socket = ipxaddr->sa_socket;
	
	size_t packet_len  = sizeof(ipx_packet) - 1 + sizeof(req);
	ipx_packet *packet = malloc(packet_len);
	if(!packet)
	{
		unlock_sockets();
		
		WSASetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	
	memset(packet, 0, sizeof(ipx_packet));
	
	packet->ptype = IPX_MAGIC_SPXLOOKUP;
	
	packet->size = htons(sizeof(req));
	memcpy(packet->data, &req, sizeof(req));
	
	/* Set up a UDP socket for sending the spxlookup_req_t packets and
	 * receiving the spxlookup_reply_t packets.
	 * 
	 * A dedicated socket is used so connect() can block without having to
	 * worry about interaction with the router thread.
	*/
	
	int lookup_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(lookup_fd == -1)
	{
		log_printf(LOG_ERROR, "Cannot create UDP socket: %s", w32_error(WSAGetLastError()));
		
		free(packet);
		unlock_sockets();
		
		return -1;
	}
	
	unsigned long argp = 1;
	ioctlsocket(lookup_fd, FIONBIO, &argp);
	
	BOOL bcast = TRUE;
	setsockopt(lookup_fd, SOL_SOCKET, SO_BROADCAST, (char*)(&bcast), sizeof(bcast));
	
	struct sockaddr_in in_addr;
	in_addr.sin_family      = AF_INET;
	in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	in_addr.sin_port        = htons(0);
	
	if(bind(lookup_fd, (struct sockaddr*)(&in_addr), sizeof(in_addr)) == -1)
	{
		log_printf(LOG_ERROR, "Cannot bind UDP socket for SPX address lookup: %s", w32_error(WSAGetLastError()));
		
		closesocket(lookup_fd);
		free(packet);
		unlock_sockets();
		
		return -1;
	}
	
	/* Try to find a host listening on the named SPX address. */
	
	bool got_reply = false;
	
	for(int i = 0; i < IPX_CONNECT_TRIES && !got_reply; ++i)
	{
		/* Send a batch of requests to the previously determined
		 * broadcast addresses.
		*/
		
		bool sent_req = false;
		
		for(int n = 0; n < bcast_count; ++n)
		{
			in_addr.sin_addr.s_addr = bcast_addrs[n];
			in_addr.sin_port        = htons(main_config.udp_port);
			
			log_printf(LOG_DEBUG, "Sending IPX_MAGIC_SPXLOOKUP packet to %s:%hu", inet_ntoa(in_addr.sin_addr), main_config.udp_port);
			
			if(sendto(lookup_fd, (char*)(packet), packet_len, 0, (struct sockaddr*)(&in_addr), sizeof(in_addr)) == -1)
			{
				log_printf(LOG_ERROR, "Cannot send IPX_MAGIC_SPXLOOKUP packet: %s", w32_error(WSAGetLastError()));
			}
			else{
				sent_req = true;
			}
		}
		
		if(!sent_req)
		{
			/* Give up if none of them could be sent. */
			
			closesocket(lookup_fd);
			free(packet);
			unlock_sockets();
			
			WSASetLastError(WSAENETUNREACH);
			return -1;
		}
		
		/* Wait for any replies to the batch.
		 * 
		 * BUG: Batch may time out or wait (effectively) forever if the
		 * batch is sent just before the system tick count rolls over.
		*/
		
		uint64_t wait_until = get_ticks() + (IPX_CONNECT_TIMEOUT / IPX_CONNECT_TRIES) * 1000;
		
		for(uint64_t now; (now = get_ticks()) < wait_until;)
		{
			/* Release the socket table in case the remote address
			 * in question is in the same process and we block the
			 * router from replying.
			*/
			
			int reclaim_fd = sock->fd;
			unlock_sockets();
			
			fd_set fdset;
			FD_ZERO(&fdset);
			FD_SET(lookup_fd, &fdset);
			
			struct timeval tv = {
				.tv_sec  = (wait_until - now) / 1000,
				.tv_usec = ((wait_until - now) % 1000) * 1000
			};
			
			if(r_select(1, &fdset, NULL, NULL, &tv) == -1)
			{
				closesocket(lookup_fd);
				free(packet);
				
				return -1;
			}
			
			/* Reclaim the lock, ensure the socket hasn't been
			 * closed by the application (naughty!) while we were
			 * waiting.
			*/
			
			ipx_socket *reclaim_sock = get_socket(reclaim_fd);
			if(sock != reclaim_sock)
			{
				log_printf(LOG_DEBUG, "Application closed socket during connect!");
				
				closesocket(lookup_fd);
				free(packet);
				
				if(reclaim_sock)
				{
					unlock_sockets();
				}
				
				WSASetLastError(WSAENOTSOCK);
				return -1;
			}
			
			/* Read and process a single packet if available. */
			
			spxlookup_reply_t reply;
			int addrlen = sizeof(in_addr);
			
			if(recvfrom(lookup_fd, (char*)(&reply), sizeof(reply), 0, (struct sockaddr*)(&in_addr), &addrlen) == sizeof(reply)
				&& memcmp(reply.net, req.net, 4) == 0
				&& memcmp(reply.node, req.node, 6) == 0
				&& reply.socket == req.socket)
			{
				if(!(sock->flags & IPX_BOUND))
				{
					/* Connecting has to implicitly bind the
					 * socket if it isn't already. Fill in
					 * the local net/node numbers with those
					 * of the interface that received the
					 * reply.
					*/
					
					ipx_interface_t *iface = ipx_interface_by_subnet(in_addr.sin_addr.s_addr);
					
					if(iface)
					{
						addr32_out(sock->addr.sa_netnum, iface->ipx_net);
						addr48_out(sock->addr.sa_nodenum, iface->ipx_node);
					}
					
					free_ipx_interface(iface);
					
					if(!iface)
					{
						continue;
					}
				}
				
				in_addr.sin_port = reply.port;
				got_reply = true;
				
				break;
			}
		}
	}
	
	closesocket(lookup_fd);
	free(packet);
	
	if(!got_reply)
	{
		/* Didn't receive any replies. */
		
		log_printf(LOG_DEBUG, "Didn't get any replies to IPX_MAGIC_SPXLOOKUP");
		
		unlock_sockets();
		
		WSASetLastError(WSAENETUNREACH);
		return -1;
	}
	
	log_printf(LOG_DEBUG, "Got reply to IPX_MAGIC_SPXLOOKUP; connecting to %s:%hu", inet_ntoa(in_addr.sin_addr), htons(in_addr.sin_port));
	
	/* Attempt to connect the underlying TCP socket to the address we got in
	 * response to the IPX_MAGIC_SPXLOOKUP packet.
	*/
	
	if(r_connect(sock->fd, (struct sockaddr*)(&in_addr), sizeof(in_addr)) == -1)
	{
		if(WSAGetLastError() == WSAEWOULDBLOCK)
		{
			/* The socket is in non-blocking mode, so we wait for
			 * the asynchronous connect call to complete.
			 * 
			 * Keeping it synchronous until it is proven this breaks
			 * something for simplicity.
			*/
			
			fd_set w_fdset;
			FD_ZERO(&w_fdset);
			FD_SET(sock->fd, &w_fdset);
			
			fd_set e_fdset;
			FD_ZERO(&e_fdset);
			FD_SET(sock->fd, &e_fdset);
			
			if(r_select(1, NULL, &w_fdset, &e_fdset, NULL) == 1 && FD_ISSET(sock->fd, &w_fdset))
			{
				goto CONNECTED;
			}
			
			int errnum, len = sizeof(int);
			getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char*)(&errnum), &len);
			
			log_printf(LOG_DEBUG, "Connection failed: %s", w32_error(errnum));
			
			unlock_sockets();
			
			WSASetLastError(WSAEWOULDBLOCK);
			return -1;
		}
		
		unlock_sockets();
		return -1;
	}
	
	CONNECTED:
	
	log_printf(LOG_DEBUG, "Connection succeeded");
	
	/* Set the IPX_CONNECT_OK bit which indicates the next WSAAsyncSelect
	 * call with FD_CONNECT set should send a message indicating the
	 * connection succeeded and then clear this bit.
	 * 
	 * This is a hack to make asynchronous connect calls vaguely work as
	 * they should.
	*/
	
	sock->flags |= IPX_CONNECT_OK;
	
	/* The TCP connection is up!
	 * 
	 * Store the remote IPX address in remote_addr and mark the socket as
	 * connected for getpeername.
	*/
	
	memcpy(&(sock->remote_addr), ipxaddr, sizeof(*ipxaddr));
	sock->flags |= IPX_CONNECTED;
	
	/* If the socket wasn't previously bound to an IPX address, we need to
	 * make it so now.
	*/
	
	if(!(sock->flags & IPX_BOUND))
	{
		sock->addr.sa_family = AF_IPX;
		
		struct sockaddr_in local_addr;
		int addrlen = sizeof(local_addr);
		
		if(r_getsockname(sock->fd, (struct sockaddr*)(&local_addr), &addrlen) == -1)
		{
			log_printf(LOG_ERROR, "Cannot get local TCP port of SPX socket: %s", w32_error(WSAGetLastError()));
			log_printf(LOG_WARNING, "Socket %d is NOW INCONSISTENT!", sock->fd);
			
			unlock_sockets();
			
			return -1;
		}
		
		sock->port = local_addr.sin_port;
		log_printf(LOG_DEBUG, "Socket %d bound to TCP port %hu by connect", sock->fd, ntohs(sock->port));
		
		/* The sa_netnum and sa_nodenum fields are filled out above. */
		
		if(!_complete_bind(sock))
		{
			log_printf(LOG_ERROR, "Cannot allocate socket number for SPX socket");
			log_printf(LOG_WARNING, "Socket %d is NOW INCONSISTENT!", sock->fd);
			
			unlock_sockets();
			
			return -1;
		}
		
		{
			IPX_STRING_ADDR(
				addr_s,
				addr32_in(sock->addr.sa_netnum),
				addr48_in(sock->addr.sa_nodenum),
				sock->addr.sa_socket
			);
			
			log_printf(LOG_DEBUG, "Socket implicitly bound to %s", addr_s);
		}
	}
	
	/* Populate an spxinit_t structure and send it over the stream for the
	 * IPXWrapper instance on the other end to receive inside accept and
	 * initialise the new ipx_socket.
	*/
	
	spxinit_t spxinit;
	memset(&spxinit, 0, sizeof(spxinit));
	
	memcpy(spxinit.net, sock->addr.sa_netnum, 4);
	memcpy(spxinit.node, sock->addr.sa_nodenum, 6);
	spxinit.socket = sock->addr.sa_socket;
	
	for(int c = 0; c < sizeof(spxinit);)
	{
		int s = send(sock->fd, (char*)(&spxinit) + c, sizeof(spxinit) - c, 0);
		if(s == -1)
		{
			log_printf(LOG_ERROR, "Cannot send spxinit structure: %s", w32_error(WSAGetLastError()));
			log_printf(LOG_WARNING, "Socket %d is NOW INCONSISTENT!", sock->fd);
			
			unlock_sockets();
			
			return -1;
		}
		
		c += s;
	}
	
	unlock_sockets();
	
	return 0;
}

int PASCAL connect(SOCKET fd, const struct sockaddr *addr, int addrlen)
{
	log_printf(LOG_CALL, "connect(%d, %p, %d)", (int)(fd), addr, addrlen);
	
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
		
		if(sock->flags & IPX_IS_SPX)
		{
			if(addrlen < sizeof(struct sockaddr_ipx))
			{
				unlock_sockets();
				
				WSASetLastError(WSAEFAULT);
				return -1;
			}
			
			if(ipxaddr->sa_family != AF_IPX)
			{
				unlock_sockets();
				
				WSASetLastError(WSAEAFNOSUPPORT);
				return -1;
			}
			
			return _connect_spx(sock, ipxaddr);
		}
		else{
			/* Windows 2000/XP allow disconnecting a datagram socket
			 * by passing an AF_UNSPEC sockaddr.
			 * 
			 * I doubt anything using IPX depends on such recent
			 * behaviour, but better safe than sorry.
			*/
			
			if(addrlen >= sizeof(addr->sa_family) && addr->sa_family == AF_UNSPEC)
			{
				sock->flags &= ~IPX_CONNECTED;
				unlock_sockets();
				
				return 0;
			}
			
			if(addrlen < sizeof(struct sockaddr_ipx))
			{
				unlock_sockets();
				
				WSASetLastError(WSAEFAULT);
				return -1;
			}
			
			if(addr->sa_family != AF_IPX)
			{
				unlock_sockets();
				
				WSASetLastError(WSAEAFNOSUPPORT);
				return -1;
			}
			
			/* Calling connect with an sa_nodenum of all zeroes
			 * disconnects in all known versions of Windows.
			*/
			
			if(memcmp(ipxaddr->sa_nodenum, (unsigned char[]){ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 6) == 0)
			{
				sock->flags &= ~IPX_CONNECTED;
				unlock_sockets();
				
				return 0;
			}
			
			if(!(sock->flags & IPX_BOUND))
			{
				log_printf(LOG_WARNING, "connect() on unbound socket, attempting implicit bind");
				
				struct sockaddr_ipx bind_addr;
				
				bind_addr.sa_family = AF_IPX;
				memcpy(bind_addr.sa_netnum, ipxaddr->sa_netnum, 4);
				memset(bind_addr.sa_nodenum, 0, 6);
				bind_addr.sa_socket = 0;
				
				if(bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1)
				{
					unlock_sockets();
					return -1;
				}
			}
			
			memcpy(&(sock->remote_addr), addr, sizeof(*ipxaddr));
			sock->flags |= IPX_CONNECTED;
			
			unlock_sockets();
			
			return 0;
		}
	}
	else{
		return r_connect(fd, addr, addrlen);
	}
}

int PASCAL send(SOCKET fd, const char *buf, int len, int flags)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			unlock_sockets();
			
			return r_send(fd, buf, len, flags);
		}
		else{
			if(!(sock->flags & IPX_CONNECTED))
			{
				unlock_sockets();
				
				WSASetLastError(WSAENOTCONN);
				return -1;
			}
			
			int ret = sendto(fd, buf, len, 0, (struct sockaddr*)&(sock->remote_addr), sizeof(struct sockaddr_ipx));
			
			unlock_sockets();
			
			return ret;
		}
	}
	else{
		return r_send(fd, buf, len, flags);
	}
}

int PASCAL getpeername(SOCKET fd, struct sockaddr *addr, int *addrlen)
{
	ipx_socket *sock = get_socket(fd);
	
	if(sock)
	{
		if(!(sock->flags & IPX_CONNECTED))
		{
			WSASetLastError(WSAENOTCONN);
			
			unlock_sockets();
			return -1;
		}
		
		if(*addrlen < sizeof(struct sockaddr_ipx))
		{
			WSASetLastError(WSAEFAULT);
			
			unlock_sockets();
			return -1;
		}
		
		memcpy(addr, &(sock->remote_addr), sizeof(struct sockaddr_ipx));
		*addrlen = sizeof(struct sockaddr_ipx);
		
		unlock_sockets();
		return 0;
	}
	else{
		return r_getpeername(fd, addr, addrlen);
	}
}

int PASCAL listen(SOCKET s, int backlog)
{
	ipx_socket *sock = get_socket(s);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			if(!(sock->flags & IPX_BOUND))
			{
				unlock_sockets();
				
				WSASetLastError(WSAEINVAL);
				return -1;
			}
			
			if(sock->flags & IPX_LISTENING)
			{
				unlock_sockets();
				
				WSASetLastError(WSAEISCONN);
				return -1;
			}
			
			if(r_listen(sock->fd, backlog) == -1)
			{
				unlock_sockets();
				
				return -1;
			}
			
			sock->flags |= IPX_LISTENING;
			
			unlock_sockets();
			
			return 0;
		}
		else{
			unlock_sockets();
			
			WSASetLastError(WSAEOPNOTSUPP);
			return -1;
		}
	}
	else{
		return r_listen(s, backlog);
	}
}

SOCKET PASCAL accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
	ipx_socket *sock = get_socket(s);
	
	if(sock)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			if(addrlen && *addrlen < sizeof(struct sockaddr_ipx))
			{
				unlock_sockets();
				
				WSASetLastError(WSAEFAULT);
				return -1;
			}
			
			ipx_socket *nsock = malloc(sizeof(ipx_socket));
			if(!nsock)
			{
				WSASetLastError(ERROR_OUTOFMEMORY);
				return -1;
			}
			
			if((nsock->fd = r_accept(s, NULL, NULL)) == -1)
			{
				free(nsock);
				unlock_sockets();
				
				return -1;
			}
			
			log_printf(LOG_INFO, "Accepted SPX connection (fd = %d)", nsock->fd);
			
			/* The first thing sent over an SPX connection is the
			 * spxinit structure which contains the IPX address of
			 * the client.
			*/
			
			spxinit_t spxinit;
			
			for(int i = 0; i < sizeof(spxinit);)
			{
				int r = recv(nsock->fd, (char*)(&spxinit) + i, sizeof(spxinit) - i, 0);
				if(r <= 0)
				{
					if(r == -1)
					{
						log_printf(LOG_ERROR, "Error receiving spxinit structure: %s", w32_error(WSAGetLastError()));
					}
					
					closesocket(nsock->fd);
					free(nsock);
					unlock_sockets();
					
					WSASetLastError(WSAECONNRESET);
					return -1;
				}
				
				i += r;
			}
			
			nsock->flags = IPX_IS_SPX | IPX_BOUND | IPX_CONNECTED | (sock->flags & IPX_IS_SPXII);
			
			/* Copy local address from the listening socket. */
			
			nsock->addr = sock->addr;
			
			/* Duplicate the mutex handle held by the listening
			 * socket used to detect address collisions. There is no
			 * way to recover from an error here.
			*/
			
			if(!(DuplicateHandle(GetCurrentProcess(), sock->sock_mut,
				GetCurrentProcess(), &(nsock->sock_mut),
				0, FALSE, DUPLICATE_SAME_ACCESS)))
			{
				log_printf(LOG_ERROR, "Could not duplicate socket mutex: %s", w32_error(GetLastError()));
				
				closesocket(nsock->fd);
				free(nsock);
				unlock_sockets();
				
				WSASetLastError(WSAENETDOWN);
				return -1;
			}
			
			/* Copy remote address from the spxinit packet. */
			
			nsock->remote_addr.sa_family = AF_IPX;
			memcpy(nsock->remote_addr.sa_netnum, spxinit.net, 4);
			memcpy(nsock->remote_addr.sa_nodenum, spxinit.node, 6);
			nsock->remote_addr.sa_socket = spxinit.socket;
			
			HASH_ADD_INT(sockets, fd, nsock);
			
			if(addr)
			{
				*(struct sockaddr_ipx*)(addr) = nsock->remote_addr;
			}
			
			unlock_sockets();
			
			return nsock->fd;
		}
		else{
			unlock_sockets();
			
			WSASetLastError(WSAEOPNOTSUPP);
			return -1;
		}
	}
	else{
		return r_accept(s, addr, addrlen);
	}
}

int PASCAL WSAAsyncSelect(SOCKET s, HWND hWnd, unsigned int wMsg, long lEvent)
{
	if(lEvent & FD_CONNECT)
	{
		ipx_socket *sock = get_socket(s);
		
		if(sock)
		{
			if(sock->flags & IPX_CONNECT_OK)
			{
				log_printf(LOG_DEBUG, "Posting message %u for FD_CONNECT on socket %d", wMsg, sock->fd);
				
				PostMessage(hWnd, wMsg, sock->fd, MAKEWORD(FD_CONNECT, 0));
				sock->flags &= ~IPX_CONNECT_OK;
			}
			
			unlock_sockets();
		}
	}
	
	return r_WSAAsyncSelect(s, hWnd, wMsg, lEvent);
}

int WSAAPI select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const PTIMEVAL timeout)
{
	const struct timeval TIMEOUT_IMMEDIATE = { 0, 0 };
	const struct timeval *use_timeout = timeout;
	
	fd_set force_read_fds;
	FD_ZERO(&force_read_fds);
	
	for(unsigned int i = 0; i < readfds->fd_count; ++i)
	{
		int fd = readfds->fd_array[i];
		
		ipx_socket *sockptr = get_socket(fd);
		if(sockptr != NULL)
		{
			if(sockptr->flags & IPX_IS_SPX)
			{
				unlock_sockets();
				continue;
			}
			
			if(sockptr->recv_queue->n_ready > 0)
			{
				/* There is data in the receive queue for this socket, but the
				 * underlying socket isn't necessarily readable, so we reduce the
				 * select() timeout to zero to ensure it returns immediately and
				 * inject this fd back into readfds at the end if necessary.
				*/
				
				FD_SET(fd, &force_read_fds);
				use_timeout = &TIMEOUT_IMMEDIATE;
			}
			
			unlock_sockets();
		}
	}
	
	int r = r_select(nfds, readfds, writefds, exceptfds, (const PTIMEVAL)(use_timeout));
	
	if(r >= 0)
	{
		for(unsigned int i = 0; i < force_read_fds.fd_count; ++i)
		{
			int fd = force_read_fds.fd_array[i];
			
			if(!FD_ISSET(fd, readfds))
			{
				FD_SET(fd, readfds);
				++r;
			}
		}
	}
	
	return r;
}
