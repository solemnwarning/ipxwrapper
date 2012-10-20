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
#include "addrcache.h"

extern const char *version_string;
extern const char *compile_time;

struct ipaddr_list {
	uint32_t ipaddr;
	struct ipaddr_list *next;
};

ipx_socket *sockets = NULL;
SOCKET send_fd = -1;
struct reg_global global_conf;

struct rclient g_rclient;

static CRITICAL_SECTION sockets_cs;
static CRITICAL_SECTION addrs_cs;

/* List of local IP addresses with associated IPX interfaces */
static struct ipaddr_list *local_addrs = NULL;
static time_t local_updated = 0;

static void init_cs(CRITICAL_SECTION *cs)
{
	if(!InitializeCriticalSectionAndSpinCount(cs, 0x80000000))
	{
		log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
		abort();
	}
}

static void free_local_ips(void);

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	if(why == DLL_PROCESS_ATTACH) {
		log_open("ipxwrapper.log");
		
		log_printf(LOG_INFO, "IPXWrapper %s", version_string);
		log_printf(LOG_INFO, "Compiled at %s", compile_time);
		
		if(!getenv("SystemRoot")) {
			log_printf(LOG_WARNING, "SystemRoot is not set in the environment");
			
			char env[268] = "SystemRoot=";
			GetSystemWindowsDirectory(env+11, 256);
			
			log_printf(LOG_INFO, "Setting SystemRoot to '%s'", env+11);
			_putenv(env);
		}
		
		addr_cache_init();
		
		if(!rclient_init(&g_rclient)) {
			return FALSE;
		}
		
		reg_open(KEY_QUERY_VALUE);
		
		if(reg_get_bin("global", &global_conf, sizeof(global_conf)) != sizeof(global_conf)) {
			global_conf.udp_port = DEFAULT_PORT;
			global_conf.w95_bug = 1;
			global_conf.bcast_all = 0;
			global_conf.filter = 1;
		}
		
		min_log_level = reg_get_dword("min_log_level", LOG_INFO);
		
		init_cs(&sockets_cs);
		init_cs(&addrs_cs);
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err) {
			log_printf(LOG_ERROR, "Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		if(!rclient_start(&g_rclient)) {
			return FALSE;
		}
		
		if(g_rclient.router) {
			send_fd = g_rclient.router->udp_sock;
		}else{
			/* Create UDP socket for sending packets if not using a private router */
			
			if((send_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
				log_printf(LOG_ERROR, "Failed to create UDP socket: %s", w32_error(WSAGetLastError()));
				return FALSE;
			}
			
			struct sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = INADDR_ANY;
			addr.sin_port = 0;
			
			if(bind(send_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
				log_printf(LOG_ERROR, "Failed to bind UDP socket (send_fd): %s", w32_error(WSAGetLastError()));
				return FALSE;
			}
		}
	}else if(why == DLL_PROCESS_DETACH) {
		if(send_fd != -1 && !g_rclient.router) {
			closesocket(send_fd);
		}
		
		send_fd = -1;
		
		rclient_stop(&g_rclient);
		
		free_local_ips();
		
		WSACleanup();
		
		DeleteCriticalSection(&addrs_cs);
		DeleteCriticalSection(&sockets_cs);
		
		reg_close();
		
		addr_cache_cleanup();
		
		unload_dlls();
		
		log_close();
	}
	
	return TRUE;
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

/* Check if supplied IP (network byte order) is a local address */
BOOL ip_is_local(uint32_t ipaddr) {
	EnterCriticalSection(&addrs_cs);
	
	if(local_updated + IFACE_TTL < time(NULL)) {
		/* TODO: Use all local IPs rather than just the ones with associated IPX addresses? */
		
		struct ipx_interface *ifaces = get_interfaces(-1);
		struct ipx_interface *i = ifaces;
		
		while(i) {
			struct ipaddr_list *nn = malloc(sizeof(struct ipaddr_list));
			if(!nn) {
				log_printf(LOG_ERROR, "Out of memory! Can't allocate ipaddr_list structure!");
				break;
			}
			
			nn->ipaddr = i->ipaddr;
			nn->next = local_addrs;
			local_addrs = nn;
			
			i = i->next;
		}
		
		free_interfaces(ifaces);
		
		local_updated = time(NULL);
	}
	
	struct ipaddr_list *p = local_addrs;
	
	while(p) {
		if(p->ipaddr == ipaddr) {
			LeaveCriticalSection(&addrs_cs);
			return TRUE;
		}
		
		p = p->next;
	}
	
	LeaveCriticalSection(&addrs_cs);
	return FALSE;
}

/* Free local IP address list */
static void free_local_ips(void) {
	struct ipaddr_list *p = local_addrs, *d;
	
	while(p) {
		d = p;
		p = p->next;
		
		free(d);
	}
	
	local_addrs = NULL;
}
