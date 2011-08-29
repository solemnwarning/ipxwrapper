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
	router->udp_sock = -1;
	router->listner = -1;
	router->wsa_event = WSA_INVALID_EVENT;
	router->crit_sec_init = FALSE;
	router->addrs = NULL;
	
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
	
	if((router->udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_printf("Error creating UDP socket: %s", w32_error(WSAGetLastError()));
		
		router_destroy(router);
		return NULL;
	}
	
	struct sockaddr_in addr;
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = 9999;
	
	if(bind(router->udp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
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
	if(router->udp_sock != -1) {
		closesocket(router->udp_sock);
	}
	
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
	
	while(1) {
		WaitForSingleObject(router->wsa_event, INFINITE);
		
		EnterCriticalSection(&(router->crit_sec));
		
		if(!router->running) {
			LeaveCriticalSection(&(router->crit_sec));
			return 0;
		}
		
		/* TODO: Step through sockets, deal with timeouts/etc */
		
		LeaveCriticalSection(&(router->crit_sec));
	}
	
	return 0;
}

int router_bind(struct router_vars *router, SOCKET control, SOCKET sock, struct sockaddr_ipx *addr) {
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
	
	free_interfaces(ifaces);
	
	EnterCriticalSection(&(router->crit_sec));
	
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
	new_addr->next = NULL;
	
	router->addrs = new_addr;
	
	LeaveCriticalSection(&(router->crit_sec));
	
	return 0;
}
