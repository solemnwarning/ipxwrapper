/* ipxwrapper - Library functions
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
#include <nspapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#include "ipxwrapper.h"
#include "common.h"
#include "interface.h"
#include "router.h"

#define DLL_UNLOAD(dll) \
	if(dll) {\
		FreeModule(dll);\
		dll = NULL;\
	}

ipx_socket *sockets = NULL;
struct ipx_interface *nics = NULL;
ipx_host *hosts = NULL;
SOCKET net_fd = -1;
struct reg_global global_conf;

HMODULE winsock2_dll = NULL;
HMODULE mswsock_dll = NULL;
HMODULE wsock32_dll = NULL;

static HANDLE router_thread = NULL;
struct router_vars *router = NULL;

static CRITICAL_SECTION sockets_cs;
static CRITICAL_SECTION hosts_cs;

static BOOL start_router(void);

#define INIT_CS(cs) if(!init_cs(cs, &initialised_cs)) { return FALSE; }

static BOOL init_cs(CRITICAL_SECTION *cs, int *counter) {
	if(!InitializeCriticalSectionAndSpinCount(cs, 0x80000000)) {
		log_printf("Failed to initialise critical section: %s", w32_error(GetLastError()));
		return FALSE;
	}
	
	(*counter)++;
	return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	static int initialised_cs = 0;
	
	if(why == DLL_PROCESS_ATTACH) {
		log_open();
		
		winsock2_dll = load_sysdll("ws2_32.dll");
		mswsock_dll = load_sysdll("mswsock.dll");
		wsock32_dll = load_sysdll("wsock32.dll");
		
		if(!winsock2_dll || !mswsock_dll || !wsock32_dll) {
			return FALSE;
		}
		
		reg_open(KEY_QUERY_VALUE);
		
		if(reg_get_bin("global", &global_conf, sizeof(global_conf)) != sizeof(global_conf)) {
			global_conf.udp_port = DEFAULT_PORT;
			global_conf.w95_bug = 1;
			global_conf.bcast_all = 0;
			global_conf.filter = 1;
		}
		
		nics = get_interfaces(-1);
		
		INIT_CS(&sockets_cs);
		INIT_CS(&hosts_cs);
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err) {
			log_printf("Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		if(!start_router()) {
			return FALSE;
		}
	}else if(why == DLL_PROCESS_DETACH) {
		if(router_thread) {
			EnterCriticalSection(&(router->crit_sec));
			
			router->running = FALSE;
			SetEvent(router->wsa_event);
			
			LeaveCriticalSection(&(router->crit_sec));
			
			if(WaitForSingleObject(router_thread, 3000) == WAIT_TIMEOUT) {
				log_printf("Router thread didn't exit in 3 seconds, killing");
				TerminateThread(router_thread, 0);
			}
			
			CloseHandle(router_thread);
			router_thread = NULL;
			
			router_destroy(router);
			router = NULL;
		}
		
		WSACleanup();
		
		switch(initialised_cs) {
			case 2: DeleteCriticalSection(&hosts_cs);
			case 1: DeleteCriticalSection(&sockets_cs);
			default: break;
		}
		
		initialised_cs = 0;
		
		free_interfaces(nics);
		
		reg_close();
		
		DLL_UNLOAD(winsock2_dll);
		DLL_UNLOAD(mswsock_dll);
		DLL_UNLOAD(wsock32_dll);
		
		log_close();
	}
	
	return TRUE;
}

void __stdcall *find_sym(char const *symbol) {
	void *addr = GetProcAddress(winsock2_dll, symbol);
	
	if(!addr) {
		addr = GetProcAddress(mswsock_dll, symbol);
	}
	if(!addr) {
		addr = GetProcAddress(wsock32_dll, symbol);
	}
	
	if(!addr) {
		log_printf("Unknown symbol: %s", symbol);
		abort();
	}
	
	return addr;
}

/* Lock the mutex and search the sockets list for an ipx_socket structure with
 * the requested fd, if no matching fd is found, unlock the mutex
 *
 * TODO: Change this behaviour. It is almost as bad as the BKL.
*/
ipx_socket *get_socket(SOCKET fd) {
	lock_sockets();
	
	ipx_socket *ptr = sockets;
	
	while(ptr) {
		if(ptr->fd == fd) {
			break;
		}
		
		ptr = ptr->next;
	}
	
	if(!ptr) {
		unlock_sockets();
	}
	
	return ptr;
}

/* Lock the mutex */
void lock_sockets(void) {
	EnterCriticalSection(&sockets_cs);
}

/* Unlock the mutex */
void unlock_sockets(void) {
	LeaveCriticalSection(&sockets_cs);
}

/* Initialize and start the router thread */
static BOOL start_router(void) {
	if(!(router = router_init(FALSE))) {
		return FALSE;
	}
	
	router_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&router_main, router, 0, NULL);
	if(!router_thread) {
		log_printf("Failed to create router thread: %s", w32_error(GetLastError()));
		
		router_destroy(router);
		router = NULL;
		
		return FALSE;
	}
	
	net_fd = router->udp_sock;
	
	return TRUE;
}

/* Add a host to the hosts list or update an existing one */
void add_host(const unsigned char *net, const unsigned char *node, uint32_t ipaddr) {
	EnterCriticalSection(&hosts_cs);
	
	ipx_host *hptr = hosts;
	
	while(hptr) {
		if(memcmp(hptr->ipx_net, net, 4) == 0 && memcmp(hptr->ipx_node, node, 6) == 0) {
			hptr->ipaddr = ipaddr;
			hptr->last_packet = time(NULL);
			
			LeaveCriticalSection(&hosts_cs);
			return;
		}
		
		hptr = hptr->next;
	}
	
	hptr = malloc(sizeof(ipx_host));
	if(!hptr) {
		LeaveCriticalSection(&hosts_cs);
		
		log_printf("No memory for hosts list entry");
		return;
	}
	
	memcpy(hptr->ipx_net, net, 4);
	memcpy(hptr->ipx_node, node, 6);
	
	hptr->ipaddr = ipaddr;
	hptr->last_packet = time(NULL);
	
	hptr->next = hosts;
	hosts = hptr;
	
	LeaveCriticalSection(&hosts_cs);
}

/* Search the hosts list */
ipx_host *find_host(const unsigned char *net, const unsigned char *node) {
	EnterCriticalSection(&hosts_cs);
	
	ipx_host *hptr = hosts, *pptr = NULL;
	
	while(hptr) {
		if(memcmp(hptr->ipx_net, net, 4) == 0 && memcmp(hptr->ipx_node, node, 6) == 0) {
			if(hptr->last_packet+TTL < time(NULL)) {
				/* Host record has expired, delete */
				
				if(pptr) {
					pptr->next = hptr->next;
					free(hptr);
				}else{
					hosts = hptr->next;
					free(hptr);
				}
				
				hptr = NULL;
			}
			
			break;
		}
		
		pptr = hptr;
		hptr = hptr->next;
	}
	
	LeaveCriticalSection(&hosts_cs);
	return hptr;
}
