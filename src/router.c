/* IPXWrapper - Router code
 * Copyright (C) 2011 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include "router.h"
#include "common.h"
#include "ipxwrapper.h"
#include "interface.h"

static struct router_addr *router_get(struct router_vars *router, SOCKET control, SOCKET sock);

/* Allocate router_vars structure and initialise all members
 * Returns NULL on failure
*/
struct router_vars *router_init(BOOL global) {
	struct router_vars *router = malloc(sizeof(struct router_vars));
	if(!router) {
		log_printf("Not enough memory to create router_vars!");
		return NULL;
	}
	
	router->running = TRUE;
	router->interfaces = NULL;
	router->udp_sock = -1;
	router->listner = -1;
	router->wsa_event = WSA_INVALID_EVENT;
	router->crit_sec_init = FALSE;
	router->addrs = NULL;
	router->recvbuf = NULL;
	
	if(InitializeCriticalSectionAndSpinCount(&(router->crit_sec), 0x80000000)) {
		router->crit_sec_init = TRUE;
	}else{
		log_printf("Error creating critical section: %s", w32_error(GetLastError()));
		
		router_destroy(router);
		return NULL;
	}
	
	if((router->wsa_event = WSACreateEvent()) == WSA_INVALID_EVENT) {
		log_printf("Error creating WSA event object: %s", w32_error(WSAGetLastError()));
		
		router_destroy(router);
		return NULL;
	}
	
	router->interfaces = get_interfaces(-1);
	
	if((router->udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_printf("Error creating UDP socket: %s", w32_error(WSAGetLastError()));
		
		router_destroy(router);
		return NULL;
	}
	
	struct sockaddr_in addr;
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(global_conf.udp_port);
	
	if(bind(router->udp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		log_printf("Error binding UDP socket: %s", w32_error(WSAGetLastError()));
		
		router_destroy(router);
		return NULL;
	}
	
	BOOL broadcast = TRUE;
	int bufsize = 524288;	/* 512KiB */
	
	setsockopt(router->udp_sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(BOOL));
	setsockopt(router->udp_sock, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(int));
	setsockopt(router->udp_sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(int));
	
	if(WSAEventSelect(router->udp_sock, router->wsa_event, FD_READ) == -1) {
		log_printf("WSAEventSelect error: %s", w32_error(WSAGetLastError()));
		
		router_destroy(router);
		return NULL;
	}
	
	if(!(router->recvbuf = malloc(PACKET_BUF_SIZE))) {
		log_printf("Out of memory! Cannot allocate recv buffer");
		
		router_destroy(router);
		return NULL;
	}
	
	if(global) {
		/* TODO: Global (service) router support */
	}
	
	return router;
}

/* Release all resources allocated by a router and free it */
void router_destroy(struct router_vars *router) {
	struct router_addr *addr = router->addrs;
	
	while(addr) {
		struct router_addr *del = addr;
		addr = addr->next;
		
		free(del);
	}
	
	free(router->recvbuf);
	
	if(router->udp_sock != -1) {
		closesocket(router->udp_sock);
	}
	
	free_interfaces(router->interfaces);
	
	if(router->wsa_event != WSA_INVALID_EVENT) {
		WSACloseEvent(router->wsa_event);
	}
	
	if(router->crit_sec_init) {
		DeleteCriticalSection(&(router->crit_sec));
	}
	
	free(router);
}

DWORD router_main(void *arg) {
	struct router_vars *router = arg;
	
	const unsigned char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
	
	while(1) {
		WaitForSingleObject(router->wsa_event, INFINITE);
		
		EnterCriticalSection(&(router->crit_sec));
		
		WSAResetEvent(router->wsa_event);
		
		if(!router->running) {
			LeaveCriticalSection(&(router->crit_sec));
			return 0;
		}
		
		LeaveCriticalSection(&(router->crit_sec));
		
		struct sockaddr_in addr;
		int addrlen = sizeof(addr);
		
		int len = r_recvfrom(router->udp_sock, router->recvbuf, PACKET_BUF_SIZE, 0, (struct sockaddr*)&addr, &addrlen);
		if(len == -1) {
			if(WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAECONNRESET) {
				continue;
			}
			
			log_printf("Error reading from UDP socket: %s", w32_error(WSAGetLastError()));
			return 1;
		}
		
		EnterCriticalSection(&(router->crit_sec));
		
		ipx_packet *packet = (ipx_packet*)router->recvbuf;
		
		/* Check that the packet arrived from the subnet of an enabled network
		 * interface and drop it if not.
		*/
		
		if(global_conf.filter) {
			struct ipx_interface *iface = router->interfaces;
			
			while(iface) {
				if((iface->ipaddr & iface->netmask) == (addr.sin_addr.s_addr & iface->netmask)) {
					break;
				}
				
				iface = iface->next;
			}
			
			if(!iface) {
				LeaveCriticalSection(&(router->crit_sec));
				continue;
			}
		}
		
		packet->size = ntohs(packet->size);
		
		if(packet->size > MAX_PACKET_SIZE || packet->size + sizeof(ipx_packet) - 1 != len) {
			LeaveCriticalSection(&(router->crit_sec));
			continue;
		}
		
		/* Replace destination network field of packet with source IP address
		 * so that the client can cache it.
		*/
		
		char dest_net[4];
		
		memcpy(dest_net, packet->dest_net, 4);
		memcpy(packet->dest_net, &(addr.sin_addr.s_addr), 4);
		
		struct router_addr *ra = router->addrs;
		
		while(ra) {
			if(
				ra->local_port &&
				(ra->filter_ptype < 0 || ra->filter_ptype == packet->ptype) &&
				(memcmp(dest_net, ra->addr.sa_netnum, 4) == 0 || memcmp(dest_net, f6, 4) == 0) &&
				(memcmp(packet->dest_node, ra->addr.sa_nodenum, 6) == 0 || memcmp(packet->dest_node, f6, 6) == 0) &&
				packet->dest_socket == ra->addr.sa_socket
			) {
				addr.sin_addr.s_addr = inet_addr("127.0.0.1");
				addr.sin_port = ra->local_port;
				
				if(r_sendto(router->udp_sock, (char*)packet, len, 0, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
					log_printf("Error relaying packet: %s", w32_error(WSAGetLastError()));
				}
			}
			
			ra = ra->next;
		}
		
		LeaveCriticalSection(&(router->crit_sec));
	}
	
	return 0;
}

int router_bind(struct router_vars *router, SOCKET control, SOCKET sock, struct sockaddr_ipx *addr, uint32_t *nic_bcast) {
	/* Network number 00:00:00:00 is specified as the "current" network, this code
	 * treats it as a wildcard when used for the network OR node numbers.
	 *
	 * According to MSDN 6, IPX socket numbers are unique to systems rather than
	 * interfaces and as such, the same socket number cannot be bound to more than
	 * one interface, my code lacks any "catch all" address like INADDR_ANY as I have
	 * not found any mentions of an equivalent address for IPX. This means that a
	 * given socket number may only be used on one interface.
	 *
	 * If you know the above information about IPX socket numbers to be incorrect,
	 * PLEASE email me with corrections!
	*/
	
	struct ipx_interface *ifaces = get_interfaces(-1), *iface;
	unsigned char z6[] = {0,0,0,0,0,0};
	
	for(iface = ifaces; iface; iface = iface->next) {
		if(
			(memcmp(addr->sa_netnum, iface->ipx_net, 4) == 0 || memcmp(addr->sa_netnum, z6, 4) == 0) &&
			(memcmp(addr->sa_nodenum, iface->ipx_node, 6) == 0 || memcmp(addr->sa_nodenum, z6, 6) == 0)
		) {
			break;
		}
	}
	
	if(!iface) {
		log_printf("bind failed: no such address");
		
		free_interfaces(ifaces);
		
		WSASetLastError(WSAEADDRNOTAVAIL);
		return -1;
	}
	
	memcpy(addr->sa_netnum, iface->ipx_net, 4);
	memcpy(addr->sa_nodenum, iface->ipx_node, 6);
	
	*nic_bcast = iface->bcast;
	
	free_interfaces(ifaces);
	
	EnterCriticalSection(&(router->crit_sec));
	
	if(router_get(router, control, sock)) {
		log_printf("bind failed: socket already bound");
		
		LeaveCriticalSection(&(router->crit_sec));
		
		WSASetLastError(WSAEINVAL);
		return -1;
	}
	
	if(addr->sa_socket == 0) {
		/* Automatic socket allocations start at 1024, I have no idea if
		 * this is normal IPX behaviour, but IP does it and it doesn't seem
		 * to interfere with any IPX software I've tested.
		*/
		
		uint16_t s = 1024;
		struct router_addr *a = router->addrs;
		
		while(a) {
			if(ntohs(a->addr.sa_socket) == s) {
				if(s == 65535) {
					log_printf("bind failed: out of sockets?!");
					
					LeaveCriticalSection(&(router->crit_sec));
					
					WSASetLastError(WSAEADDRNOTAVAIL);
					return -1;
				}
				
				s++;
				a = router->addrs;
				
				continue;
			}
			
			a = a->next;
		}
		
		addr->sa_socket = htons(s);
	}else if(addr->sa_family != AF_IPX_SHARE) {
		/* Test if any bound socket is using the requested socket number. */
		
		struct router_addr *a = router->addrs;
		
		while(a) {
			if(a->addr.sa_socket == addr->sa_socket) {
				log_printf("bind failed: requested socket in use");
				
				LeaveCriticalSection(&(router->crit_sec));
				
				WSASetLastError(WSAEADDRINUSE);
				return -1;
			}
			
			a = a->next;
		}
	}
	
	struct router_addr *new_addr = malloc(sizeof(struct router_addr));
	if(!new_addr) {
		LeaveCriticalSection(&(router->crit_sec));
		
		WSASetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	
	memcpy(&(new_addr->addr), addr, sizeof(struct sockaddr_ipx));
	
	new_addr->local_port = 0;
	new_addr->ws_socket = sock;
	new_addr->control_socket = control;
	new_addr->filter_ptype = -1;
	new_addr->next = NULL;
	
	router->addrs = new_addr;
	
	LeaveCriticalSection(&(router->crit_sec));
	
	return 0;
}

/* Set loopback UDP port of emulation socket in NETWORK BYTE ORDER
 * Disable recv by setting to zero
*/
void router_set_port(struct router_vars *router, SOCKET control, SOCKET sock, uint16_t port) {
	EnterCriticalSection(&(router->crit_sec));
	
	struct router_addr *addr = router_get(router, control, sock);
	if(addr) {
		addr->local_port = port;
	}
	
	LeaveCriticalSection(&(router->crit_sec));
}

void router_unbind(struct router_vars *router, SOCKET control, SOCKET sock) {
	EnterCriticalSection(&(router->crit_sec));
	
	struct router_addr *addr = router->addrs, *prev = NULL;
	
	while(addr) {
		if(addr->control_socket == control && addr->ws_socket == sock) {
			if(prev) {
				prev->next = addr->next;
			}else{
				router->addrs = addr->next;
			}
			
			free(addr);
			break;
		}
		
		prev = addr;
		addr = addr->next;
	}
	
	LeaveCriticalSection(&(router->crit_sec));
}

/* Return the address a given socket is bound to, NULL if unbound */
static struct router_addr *router_get(struct router_vars *router, SOCKET control, SOCKET sock) {
	EnterCriticalSection(&(router->crit_sec));
	
	struct router_addr *addr = router->addrs;
	
	while(addr && (addr->control_socket != control || addr->ws_socket != sock)) {
		addr = addr->next;
	}
	
	LeaveCriticalSection(&(router->crit_sec));
	
	return addr;
}

/* Set packet type filter for a socket
 * Disable filter by setting to negative value
*/
void router_set_filter(struct router_vars *router, SOCKET control, SOCKET sock, int ptype) {
	EnterCriticalSection(&(router->crit_sec));
	
	struct router_addr *addr = router_get(router, control, sock);
	if(addr) {
		addr->filter_ptype = ptype;
	}
	
	LeaveCriticalSection(&(router->crit_sec));
}
