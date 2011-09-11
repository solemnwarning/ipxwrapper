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
				break;
			}
		}
		
		if(i == rval) {
			*bsptr += sizeof(PROTOCOL_INFO);
			rval++;
		}
		
		if(*bsptr > bufsize) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return -1;
		}
		
		pinfo[i].dwServiceFlags = 5641;
		pinfo[i].iAddressFamily = AF_IPX;
		pinfo[i].iMaxSockAddr = 16;
		pinfo[i].iMinSockAddr = 14;
		pinfo[i].iSocketType = SOCK_DGRAM;
		pinfo[i].iProtocol = NSPROTO_IPX;
		pinfo[i].dwMessageSize = 576;
		pinfo[i].lpProtocol = unicode ? (char*)L"IPX" : "IPX";
	}
	
	return rval;
}

INT APIENTRY EnumProtocolsA(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	return do_EnumProtocols(protocols, buf, bsptr, FALSE);
}

INT APIENTRY EnumProtocolsW(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	return do_EnumProtocols(protocols, buf, bsptr, TRUE);
}

SOCKET WSAAPI socket(int af, int type, int protocol) {
	log_printf("socket(%d, %d, %d)", af, type, protocol);
	
	if(af == AF_IPX) {
		ipx_socket *nsock = malloc(sizeof(ipx_socket));
		if(!nsock) {
			WSASetLastError(ERROR_OUTOFMEMORY);
			return -1;
		}
		
		nsock->fd = r_socket(AF_INET, SOCK_DGRAM, 0);
		if(nsock->fd == -1) {
			log_printf("Creating fake socket failed: %s", w32_error(WSAGetLastError()));
			
			free(nsock);
			return -1;
		}
		
		nsock->flags = IPX_SEND | IPX_RECV;
		nsock->s_ptype = (protocol ? NSPROTO_IPX - protocol : 0);
		
		lock_sockets();
		
		nsock->next = sockets;
		sockets = nsock;
		
		log_printf("IPX socket created (fd = %d)", nsock->fd);
		
		RETURN(nsock->fd);
	}else{
		return r_socket(af, type, protocol);
	}
}

int WSAAPI closesocket(SOCKET fd) {
	int ret = r_closesocket(fd);
	
	ipx_socket *ptr = get_socket(fd);
	ipx_socket *pptr = sockets;
	
	if(!ptr) {
		/* Not an IPX socket */
		return ret;
	}
	
	if(ret == SOCKET_ERROR) {
		log_printf("closesocket(%d) failed: %s", fd, w32_error(WSAGetLastError()));
		RETURN(SOCKET_ERROR);
	}
	
	log_printf("IPX socket closed (fd = %d)", fd);
	
	rclient_unbind(&g_rclient, fd);
	
	if(ptr == sockets) {
		sockets = ptr->next;
		free(ptr);
	}else{
		while(ptr && pptr->next) {
			if(ptr == pptr->next) {
				pptr->next = ptr->next;
				free(ptr);
			}
			
			pptr = pptr->next;
		}
	}
	
	RETURN(0);
}

int WSAAPI bind(SOCKET fd, const struct sockaddr *addr, int addrlen) {
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		struct sockaddr_ipx ipxaddr;
		char net_s[12], node_s[18];
		
		if(addrlen < sizeof(ipxaddr)) {
			RETURN_WSA(WSAEFAULT, -1);
		}
		
		memcpy(&ipxaddr, addr, sizeof(ipxaddr));
		
		NET_TO_STRING(net_s, ipxaddr.sa_netnum);
		NODE_TO_STRING(node_s, ipxaddr.sa_nodenum);
		
		log_printf("bind(%d, net=%s node=%s socket=%hu)", fd, net_s, node_s, ntohs(ipxaddr.sa_socket));
		
		if(ptr->flags & IPX_BOUND) {
			log_printf("bind failed: socket already bound");
			RETURN_WSA(WSAEINVAL, -1);
		}
		
		if(!rclient_bind(&g_rclient, fd, &ipxaddr, &(ptr->nic_bcast), ptr->flags & IPX_REUSE ? TRUE : FALSE)) {
			RETURN(-1);
		}
		
		NET_TO_STRING(net_s, ipxaddr.sa_netnum);
		NODE_TO_STRING(node_s, ipxaddr.sa_nodenum);
		
		log_printf("bind address: net=%s node=%s socket=%hu", net_s, node_s, ntohs(ipxaddr.sa_socket));
		
		struct sockaddr_in bind_addr;
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		bind_addr.sin_port = 0;
		
		if(r_bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
			log_printf("Binding local UDP socket failed: %s", w32_error(WSAGetLastError()));
			
			rclient_unbind(&g_rclient, fd);
			RETURN(-1);
		}
		
		int al = sizeof(bind_addr);
		
		if(r_getsockname(fd, (struct sockaddr*)&bind_addr, &al) == -1) {
			log_printf("getsockname failed: %s", w32_error(WSAGetLastError()));
			
			rclient_unbind(&g_rclient, fd);
			RETURN(-1);
		}
		
		memcpy(&(ptr->addr), &ipxaddr, sizeof(ipxaddr));
		ptr->flags |= IPX_BOUND;
		
		rclient_set_port(&g_rclient, fd, bind_addr.sin_port);
		
		RETURN(0);
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
static int recv_packet(ipx_socket *sockptr, char *buf, int bufsize, int flags, struct sockaddr_ipx *addr) {
	SOCKET fd = sockptr->fd;
	int is_bound = sockptr->flags & IPX_BOUND;
	
	unlock_sockets();
	
	if(!is_bound) {
		WSASetLastError(WSAEINVAL);
		return -1;
	}
	
	struct ipx_packet *packet = malloc(PACKET_BUF_SIZE);
	if(!packet) {
		WSASetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	
	int rval = r_recv(fd, (char*)packet, PACKET_BUF_SIZE, flags);
	if(rval == -1) {
		free(packet);
		return -1;
	}
	
	if(rval < sizeof(ipx_packet) || rval != packet->size + sizeof(ipx_packet) - 1) {
		log_printf("Invalid packet received on loopback port!");
		
		free(packet);
		WSASetLastError(WSAEWOULDBLOCK);
		return -1;
	}
	
	/* Router thread replaces destination network number with source IP address */
	add_host(packet->src_net, packet->src_node, *((uint32_t*)packet->dest_net));
	
	if(addr) {
		addr->sa_family = AF_IPX;
		memcpy(addr->sa_netnum, packet->src_net, 4);
		memcpy(addr->sa_nodenum, packet->src_node, 6);
		addr->sa_socket = packet->src_socket;
	}
	
	memcpy(buf, packet->data, packet->size <= bufsize ? packet->size : bufsize);
	rval = packet->size;
	free(packet);
	
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
		
		int rval = recv_packet(sockptr, buf, len, flags, (struct sockaddr_ipx*)addr);
		
		/* The value pointed to by addrlen is only set if the recv call was
		 * successful, may not be correct.
		*/
		if(rval >= 0 && addr && addrlen) {
			*addrlen = sizeof(struct sockaddr_ipx);
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
		int rval = recv_packet(sockptr, buf, len, flags, NULL);
		
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
		int rval = recv_packet(sockptr, buf, len, 0, NULL);
		
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
				*intval = MAX_PACKET_SIZE;
				
				RETURN(0);
			}
			
			if(optname == IPX_ADDRESS) {
				CHECK_OPTLEN(sizeof(IPX_ADDRESS_DATA));
				
				IPX_ADDRESS_DATA *ipxdata = (IPX_ADDRESS_DATA*)optval;
				
				struct ipx_interface *nic = get_interfaces(ipxdata->adapternum);
				
				if(!nic) {
					WSASetLastError(ERROR_NO_DATA);
					return -1;
				}
				
				memcpy(ipxdata->netnum, nic->ipx_net, 4);
				memcpy(ipxdata->nodenum, nic->ipx_node, 6);
				
				/* TODO: LAN/WAN detection, link speed detection */
				ipxdata->wan = FALSE;
				ipxdata->status = FALSE;
				ipxdata->maxpkt = MAX_PACKET_SIZE;
				ipxdata->linkspeed = 100000; /* 10MBps */
				
				free_interfaces(nic);
				
				RETURN(0);
			}
			
			/* NOTE: IPX_MAX_ADAPTER_NUM implies it may be the maximum index
			 * for referencing an IPX interface. This behaviour makes no sense
			 * and a code example in MSDN implies it should be the number of
			 * IPX interfaces, this code follows the latter.
			*/
			if(optname == IPX_MAX_ADAPTER_NUM) {
				CHECK_OPTLEN(sizeof(int));
				
				*intval = 0;
				
				struct ipx_interface *ifaces = get_interfaces(-1), *nic;
				
				for(nic = ifaces; nic;) {
					(*intval)++;
					nic = nic->next;
				}
				
				free_interfaces(ifaces);
				
				RETURN(0);
			}
			
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

int WSAAPI setsockopt(SOCKET fd, int level, int optname, const char FAR *optval, int optlen) {
	int *intval = (int*)optval;
	BOOL *bval = (BOOL*)optval;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(level == NSPROTO_IPX) {
			if(optname == IPX_PTYPE) {
				sockptr->s_ptype = *intval;
				RETURN(0);
			}
			
			if(optname == IPX_FILTERPTYPE) {
				if(!rclient_set_filter(&g_rclient, fd, *intval)) {
					RETURN(-1);
				}
				
				sockptr->f_ptype = *intval;
				sockptr->flags |= IPX_FILTER;
				
				RETURN(0);
			}
			
			if(optname == IPX_STOPFILTERPTYPE) {
				if(!rclient_set_filter(&g_rclient, fd, -1)) {
					RETURN(-1);
				}
				
				sockptr->flags &= ~IPX_FILTER;
				
				RETURN(0);
			}
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
		
		if(level == SOL_SOCKET) {
			if(optname == SO_BROADCAST) {
				if(*bval) {
					sockptr->flags |= IPX_BROADCAST;
				}else{
					sockptr->flags &= ~IPX_BROADCAST;
				}
				
				RETURN(0);
			}else if(optname == SO_REUSEADDR) {
				if(!rclient_set_reuse(&g_rclient, fd, *bval)) {
					RETURN(-1);
				}
				
				if(*bval) {
					sockptr->flags |= IPX_REUSE;
				}else{
					sockptr->flags &= ~IPX_REUSE;
				}
				
				RETURN(0);
			}
		}
		
		unlock_sockets();
	}
	
	return r_setsockopt(fd, level, optname, optval, optlen);
}

int WSAAPI sendto(SOCKET fd, const char *buf, int len, int flags, const struct sockaddr *addr, int addrlen) {
	struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(!addr || addrlen < sizeof(struct sockaddr_ipx)) {
			RETURN_WSA(WSAEDESTADDRREQ, -1);
		}
		
		if(!(sockptr->flags & IPX_SEND)) {
			RETURN_WSA(WSAESHUTDOWN, -1);
		}
		
		if(!(sockptr->flags & IPX_BOUND)) {
			log_printf("sendto() on unbound socket, attempting implicit bind");
			
			struct sockaddr_ipx bind_addr;
			
			bind_addr.sa_family = AF_IPX;
			memcpy(bind_addr.sa_netnum, ipxaddr->sa_netnum, 4);
			memset(bind_addr.sa_nodenum, 0, 6);
			bind_addr.sa_socket = 0;
			
			if(bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
				RETURN(-1);
			}
		}
		
		if(len > MAX_PACKET_SIZE) {
			RETURN_WSA(WSAEMSGSIZE, -1);
		}
		
		int psize = sizeof(ipx_packet)+len-1;
		
		ipx_packet *packet = malloc(psize);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		packet->ptype = sockptr->s_ptype;
		
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
		
		ipx_host *host = find_host(packet->dest_net, packet->dest_node);
		
		struct sockaddr_in saddr;
		saddr.sin_family = AF_INET;
		saddr.sin_port = htons(global_conf.udp_port);
		saddr.sin_addr.s_addr = (host ? host->ipaddr : (global_conf.bcast_all ? INADDR_BROADCAST : sockptr->nic_bcast));
		
		int sval = r_sendto(send_fd, (char*)packet, psize, 0, (struct sockaddr*)&saddr, sizeof(saddr));
		if(sval == -1) {
			len = -1;
		}
		
		free(packet);
		RETURN(len);
	}else{
		return r_sendto(fd, buf, len, flags, addr, addrlen);
	}
}

int PASCAL shutdown(SOCKET fd, int cmd) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(cmd == SD_RECEIVE || cmd == SD_BOTH) {
			if(!rclient_set_port(&g_rclient, fd, 0)) {
				RETURN(-1);
			}
			
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
		ipx_packet packet;
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
		
		r = r_recv(sockptr->fd, (char*)&packet, sizeof(packet), MSG_PEEK);
		if(r == -1 && WSAGetLastError() != WSAEMSGSIZE) {
			RETURN(-1);
		}
		
		*(unsigned long*)argp = packet.size;
		RETURN(0);
	}
	
	if(sockptr) {
		unlock_sockets();
	}
	
	return r_ioctlsocket(fd, cmd, argp);
}
