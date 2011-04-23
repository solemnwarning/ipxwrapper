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

#include "winstuff.h"

#include "ipxwrapper.h"

INT APIENTRY EnumProtocolsA(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	int bufsize = *bsptr, rval, i, want_ipx = 0;
	
	PROTOCOL_INFO *pinfo = buf;
	
	rval = r_EnumProtocolsA(protocols, buf, bsptr);
	if(rval == -1) {
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
		pinfo[i].lpProtocol = "IPX";
	}
	
	return rval;
}

INT APIENTRY EnumProtocolsW(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	int bufsize = *bsptr, rval, i, want_ipx = 0;
	
	PROTOCOL_INFO *pinfo = buf;
	
	rval = r_EnumProtocolsW(protocols, buf, bsptr);
	if(rval == -1) {
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
		pinfo[i].lpProtocol = (char*)L"IPX";
	}
	
	return rval;
}

int PASCAL WSARecvEx(SOCKET fd, char *buf, int len, int *flags) {
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(!(ptr->flags & IPX_BOUND)) {
			RETURN_WSA(WSAEINVAL, -1);
		}
		
		struct ipx_packet *packet = malloc(PACKET_BUF_SIZE);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		int rval = r_WSARecvEx(fd, (char*)packet, PACKET_BUF_SIZE, flags);
		if(rval == -1) {
			RETURN(-1);
		}
		
		if(packet->size <= len) {
			memcpy(buf, packet->data, packet->size);
			len = packet->size;
			free(packet);
			
			*flags = 0;
			RETURN(len);
		}else{
			memcpy(buf, packet->data, len);
			len = packet->size;
			free(packet);
			
			*flags = MSG_PARTIAL;
			RETURN(len);
		}
	}else{
		RETURN(r_WSARecvEx(fd, buf, len, flags));
	}
}

SOCKET WSAAPI socket(int af, int type, int protocol) {
	debug("socket(%d, %d, %d)", af, type, protocol);
	
	if(af == AF_IPX) {
		ipx_socket *nsock = malloc(sizeof(ipx_socket));
		if(!nsock) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		INIT_SOCKET(nsock);
		
		nsock->fd = r_socket(AF_INET, SOCK_DGRAM, 0);
		if(nsock->fd == -1) {
			debug("...failed: %s", w32_error(WSAGetLastError()));
			
			free(nsock);
			RETURN(-1);
		}
		
		if(protocol) {
			nsock->s_ptype = NSPROTO_IPX - protocol;
		}
		
		lock_mutex();
		
		nsock->next = sockets;
		sockets = nsock;
		
		debug("...success: fd=%d", nsock->fd);
		RETURN(nsock->fd);
	}else{
		return r_socket(af, type, protocol);
	}
}

int WSAAPI closesocket(SOCKET fd) {
	debug("closesocket(%d)", fd);
	
	if(r_closesocket(fd) == SOCKET_ERROR) {
		debug("...failed");
		RETURN(SOCKET_ERROR);
	}
	
	ipx_socket *ptr = get_socket(fd);
	ipx_socket *pptr = sockets;
	
	debug("...success");
	
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
		struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
		
		debug(
			"bind(%d, net=%hhx:%hhx:%hhx:%hhx node=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx socket=%hu)", fd,
			ipxaddr->sa_netnum[0],
			ipxaddr->sa_netnum[1],
			ipxaddr->sa_netnum[2],
			ipxaddr->sa_netnum[3],
			ipxaddr->sa_nodenum[0],
			ipxaddr->sa_nodenum[1],
			ipxaddr->sa_nodenum[2],
			ipxaddr->sa_nodenum[3],
			ipxaddr->sa_nodenum[4],
			ipxaddr->sa_nodenum[5],
			ntohs(ipxaddr->sa_socket)
		);
		
		struct sockaddr_in bind_addr;
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_port = 0;
		bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		
		int rval = r_bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr));
		
		if(rval == 0) {
			memcpy(ptr->netnum, ipxaddr->sa_netnum, 4);
			
			ipx_nic *nic = nics;
			int first = 1;
			
			while(nic) {
				if(first || memcmp(ipxaddr->sa_nodenum, nic->hwaddr, 6) == 0) {
					memcpy(ptr->nodenum, nic->hwaddr, 6);
					first = 0;
				}
				
				nic = nic->next;
			}
			
			ptr->socket = ntohs(ipxaddr->sa_socket);
			if(ptr->socket == 0) {
				ptr->socket = 1024;
				
				ipx_socket *sptr = sockets;
				
				while(sptr) {
					if(sptr != ptr && sptr->socket == ptr->socket) {
						ptr->socket++;
						sptr = sockets;
						continue;
					}
					
					sptr = sptr->next;
				}
			}
			
			ptr->flags |= IPX_BOUND;
			
			debug("...bound to net=%hhx:%hhx:%hhx:%hhx node=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx socket=%hu)",
				ptr->netnum[0],
				ptr->netnum[1],
				ptr->netnum[2],
				ptr->netnum[3],
				ptr->nodenum[0],
				ptr->nodenum[1],
				ptr->nodenum[2],
				ptr->nodenum[3],
				ptr->nodenum[4],
				ptr->nodenum[5],
				ptr->socket
			);
		}else{
			debug("...failed");
		}
		
		RETURN(rval);
	}else{
		RETURN(r_bind(fd, addr, addrlen));
	}
}

int WSAAPI getsockname(SOCKET fd, struct sockaddr *addr, int *addrlen) {
	struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(ptr->flags & IPX_BOUND) {
			if(*addrlen < sizeof(struct sockaddr_ipx)) {
				*addrlen = sizeof(struct sockaddr_ipx);
				RETURN_WSA(WSAEFAULT, -1);
			}
			
			ipxaddr->sa_family = AF_IPX;
			memcpy(ipxaddr->sa_netnum, ptr->netnum, 4);
			memcpy(ipxaddr->sa_nodenum, ptr->nodenum, 6);
			ipxaddr->sa_socket = htons(ptr->socket);
			
			*addrlen = sizeof(struct sockaddr_ipx);
			
			RETURN(0);
		}else{
			RETURN_WSA(WSAEINVAL, -1);
		}
	}else{
		RETURN(r_getsockname(fd, addr, addrlen));
	}
}

int WSAAPI recvfrom(SOCKET fd, char *buf, int len, int flags, struct sockaddr *addr, int *addrlen) {
	struct sockaddr_ipx *from = (struct sockaddr_ipx*)addr;
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(!(ptr->flags & IPX_BOUND)) {
			RETURN_WSA(WSAEINVAL, -1);
		}
		
		struct ipx_packet *packet = malloc(PACKET_BUF_SIZE);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		int rval = r_recv(fd, (char*)packet, PACKET_BUF_SIZE, flags);
		if(rval == -1) {
			free(packet);
			RETURN(-1);
		}
		
		if(from) {
			from->sa_family = AF_IPX;
			memcpy(from->sa_netnum, packet->src_net, 4);
			memcpy(from->sa_nodenum, packet->src_node, 6);
			from->sa_socket = htons(packet->src_socket);
			
			if(addrlen) {
				*addrlen = sizeof(struct sockaddr_ipx);
			}
		}
		
		if(packet->size <= len) {
			memcpy(buf, packet->data, packet->size);
			rval = packet->size;
			free(packet);
			
			RETURN(rval);
		}else{
			memcpy(buf, packet->data, len);
			free(packet);
			
			RETURN_WSA(WSAEMSGSIZE, -1);
		}
	}else{
		RETURN(r_recvfrom(fd, buf, len, flags, addr, addrlen));
	}
}

int WSAAPI recv(SOCKET fd, char *buf, int len, int flags) {
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(!(ptr->flags & IPX_BOUND)) {
			RETURN_WSA(WSAEINVAL, -1);
		}
		
		struct ipx_packet *packet = malloc(PACKET_BUF_SIZE);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		int rval = r_recv(fd, (char*)packet, PACKET_BUF_SIZE, flags);
		if(rval == -1) {
			free(packet);
			RETURN(-1);
		}
		
		if(packet->size <= len) {
			memcpy(buf, packet->data, packet->size);
			rval = packet->size;
			free(packet);
			
			RETURN(rval);
		}else{
			memcpy(buf, packet->data, len);
			free(packet);
			
			RETURN_WSA(WSAEMSGSIZE, -1);
		}
	}else{
		RETURN(r_recv(fd, buf, len, flags));
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
				
				ipx_nic *nic = nics;
				int i = 0;
				
				while(nic && i < ipxdata->adapternum) {
					nic = nic->next;
					i++;
				}
				
				if(!nic) {
					WSASetLastError(ERROR_NO_DATA);
					return -1;
				}
				
				memset(ipxdata->netnum, 0, 4);
				memcpy(ipxdata->nodenum, nic->hwaddr, 6);
				
				/* TODO: LAN/WAN detection, link speed detection */
				ipxdata->wan = FALSE;
				ipxdata->status = FALSE;
				ipxdata->maxpkt = MAX_PACKET_SIZE;
				ipxdata->linkspeed = 100000; /* 10MBps */
				
				RETURN(0);
			}
			
			if(optname == IPX_MAX_ADAPTER_NUM) {
				CHECK_OPTLEN(sizeof(int));
				
				*intval = 0;
				
				ipx_nic *nic = nics;
				while(nic) {
					(*intval)++;
					nic = nic->next;
				}
				
				RETURN(0);
			}
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
	}
	
	RETURN(r_getsockopt(fd, level, optname, optval, optlen));
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
				sockptr->f_ptype = *intval;
				sockptr->flags |= IPX_FILTER;
				
				RETURN(0);
			}
			
			if(optname == IPX_STOPFILTERPTYPE) {
				sockptr->flags &= ~IPX_FILTER;
				RETURN(0);
			}
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
		
		if(level == SOL_SOCKET) {
			if(optname == SO_BROADCAST) {
				if(*bval == TRUE) {
					sockptr->flags |= IPX_BROADCAST;
				}else{
					sockptr->flags &= ~IPX_BROADCAST;
				}
				
				RETURN(0);
			}
		}
	}
	
	RETURN(r_setsockopt(fd, level, optname, optval, optlen));
}

int WSAAPI sendto(SOCKET fd, const char *buf, int len, int flags, const struct sockaddr *addr, int addrlen) {
	struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
	unsigned char z6[] = {0,0,0,0,0,0};
	int sval, psize;
	struct sockaddr_in saddr;
	struct sockaddr_ipx baddr;
	ipx_packet *packet;
	ipx_nic *nic;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(!addr || addrlen < sizeof(struct sockaddr_ipx)) {
			RETURN_WSA(WSAEDESTADDRREQ, -1);
		}
		
		if(!(sockptr->flags & IPX_SEND)) {
			RETURN_WSA(WSAESHUTDOWN, -1);
		}
		
		if(!(sockptr->flags & IPX_BOUND)) {
			baddr.sa_family = AF_IPX;
			memset(baddr.sa_netnum, 0, 4);
			memset(baddr.sa_nodenum, 0, 6);
			baddr.sa_socket = 0;
			
			if(bind(fd, (struct sockaddr*)&baddr, sizeof(baddr)) == -1) {
				debug("Implicit bind on %d failed: %s", (int)fd, w32_error(WSAGetLastError()));
				RETURN(-1);
			}
		}
		
		if(len > MAX_PACKET_SIZE) {
			RETURN_WSA(WSAEMSGSIZE, -1);
		}
		
		psize = sizeof(ipx_packet)+len-1;
		
		packet = malloc(psize);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		INIT_PACKET(packet);
		packet->ptype = sockptr->s_ptype;
		
		memcpy(packet->dest_net, ipxaddr->sa_netnum, 4);
		memcpy(packet->dest_node, ipxaddr->sa_nodenum, 6);
		packet->dest_socket = ipxaddr->sa_socket;
		
		packet->size = htons(len);
		memcpy(packet->data, buf, len);
		
		lock_mutex();
		
		ipx_host *hptr = find_host(packet->dest_net, packet->dest_node);
		
		for(nic = nics; nic; nic = nic->next) {
			if((
				memcmp(sockptr->nodenum, nic->hwaddr, 6) == 0 ||
				memcmp(sockptr->nodenum, z6, 6) == 0)
			) {
				memset(packet->src_net, 0, 4);
				memcpy(packet->src_node, nic->hwaddr, 6);
				packet->src_socket = htons(sockptr->socket);
				
				saddr.sin_family = AF_INET;
				saddr.sin_addr.s_addr = htonl(hptr ? hptr->ipaddr : nic->bcast);
				saddr.sin_port = htons(PORT);
				
				sval = r_sendto(net_fd, (char*)packet, psize, 0, (struct sockaddr*)&saddr, sizeof(saddr));
				if(sval == -1) {
					len = -1;
					break;
				}
			}
		}
		
		free(packet);
		RETURN(len);
	}else{
		RETURN(r_sendto(fd, buf, len, flags, addr, addrlen));
	}
}

int PASCAL shutdown(SOCKET fd, int cmd) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(cmd == SD_SEND || cmd == SD_BOTH) {
			sockptr->flags &= ~IPX_SEND;
		}
		
		if(cmd == SD_RECEIVE || cmd == SD_BOTH) {
			sockptr->flags &= ~IPX_RECV;
		}
		
		RETURN(0);
	}else{
		RETURN(r_shutdown(fd, cmd));
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
	}else{
		RETURN(r_ioctlsocket(fd, cmd, argp));
	}
}
