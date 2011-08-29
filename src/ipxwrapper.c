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
static void *router_buf = NULL;

HMODULE winsock2_dll = NULL;
HMODULE mswsock_dll = NULL;
HMODULE wsock32_dll = NULL;

static HANDLE mutex = NULL;
static HANDLE router_thread = NULL;
static DWORD router_tid = 0;

static int init_router(void);
static DWORD WINAPI router_main(LPVOID argp);
static void add_host(const unsigned char *net, const unsigned char *node, uint32_t ipaddr);

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
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
		
		mutex = CreateMutex(NULL, FALSE, NULL);
		if(!mutex) {
			log_printf("Failed to create mutex");
			return FALSE;
		}
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err) {
			log_printf("Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		if(!init_router()) {
			return FALSE;
		}
	}else if(why == DLL_PROCESS_DETACH) {
		if(router_thread && GetCurrentThreadId() != router_tid) {
			TerminateThread(router_thread, 0);
			router_thread = NULL;
		}
		
		free(router_buf);
		router_buf = NULL;
		
		if(net_fd >= 0) {
			closesocket(net_fd);
			net_fd = -1;
		}
		
		if(mutex) {
			CloseHandle(mutex);
			mutex = NULL;
		}
		
		free_interfaces(nics);
		
		WSACleanup();
		
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
	lock_mutex();
	
	ipx_socket *ptr = sockets;
	
	while(ptr) {
		if(ptr->fd == fd) {
			break;
		}
		
		ptr = ptr->next;
	}
	
	if(!ptr) {
		unlock_mutex();
	}
	
	return ptr;
}

/* Lock the mutex */
void lock_mutex(void) {
	WaitForSingleObject(mutex, INFINITE);
}

/* Unlock the mutex */
void unlock_mutex(void) {
	while(ReleaseMutex(mutex)) {}
}

/* Initialize and start the router thread */
static int init_router(void) {
	net_fd = r_socket(AF_INET, SOCK_DGRAM, 0);
	if(net_fd == -1) {
		log_printf("Failed to create network socket: %s", w32_error(WSAGetLastError()));
		return 0;
	}
	
	struct sockaddr_in bind_addr;
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_port = htons(global_conf.udp_port);
	
	if(r_bind(net_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
		log_printf("Failed to bind network socket: %s", w32_error(WSAGetLastError()));
		return 0;
	}
	
	BOOL broadcast = TRUE;
	int bufsize = 524288;	/* 512KiB */
	
	r_setsockopt(net_fd, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(BOOL));
	r_setsockopt(net_fd, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(int));
	r_setsockopt(net_fd, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(int));
	
	router_buf = malloc(PACKET_BUF_SIZE);
	if(!router_buf) {
		log_printf("Not enough memory for router buffer (64KiB)");
		return 0;
	}
	
	router_thread = CreateThread(NULL, 0, &router_main, NULL, 0, &router_tid);
	if(!router_thread) {
		log_printf("Failed to create router thread");
		return 0;
	}
	
	return 1;
}

/* Router thread main function
 *
 * The router thread recieves packets from the listening port and forwards them
 * to the UDP sockets which emulate IPX.
*/
static DWORD WINAPI router_main(LPVOID notused) {
	ipx_packet *packet = router_buf;
	int addrlen, rval, sval;
	unsigned char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
	struct sockaddr_in addr;
	ipx_socket *sockptr;
	
	while(1) {
		addrlen = sizeof(addr);
		rval = r_recvfrom(net_fd, (char*)packet, PACKET_BUF_SIZE, 0, (struct sockaddr*)&addr, &addrlen);
		if(rval <= 0) {
			log_printf("Error recieving packet: %s", w32_error(WSAGetLastError()));
			continue;
		}
		
		if(global_conf.filter) {
			struct ipx_interface *nic = nics;
			
			while(nic) {
				if((nic->ipaddr & nic->netmask) == (addr.sin_addr.s_addr & nic->netmask)) {
					break;
				}
				
				nic = nic->next;
			}
			
			if(!nic) {
				/* Packet not recieved from subnet of an enabled interface */
				continue;
			}
		}
		
		packet->size = ntohs(packet->size);
		
		if(packet->size > MAX_PACKET_SIZE || packet->size+sizeof(ipx_packet)-1 != rval) {
			log_printf("Recieved packet with incorrect size field, discarding");
			continue;
		}
		
		lock_mutex();
		
		add_host(packet->src_net, packet->src_node, addr.sin_addr.s_addr);
		
		for(sockptr = sockets; sockptr; sockptr = sockptr->next) {
			if(
				sockptr->flags & IPX_RECV &&
				(
					!(sockptr->flags & IPX_FILTER) ||
					packet->ptype == sockptr->f_ptype
				) && ((
					sockptr->flags & IPX_BOUND &&
					packet->dest_socket == sockptr->socket &&
					(
						memcmp(packet->dest_net, sockptr->nic->ipx_net, 4) == 0 ||
						(
							memcmp(packet->dest_net, f6, 4) == 0 &&
							(!global_conf.w95_bug || sockptr->flags & IPX_BROADCAST)
						)
					) && (
						memcmp(packet->dest_node, sockptr->nic->ipx_node, 6) == 0 ||
						(
							memcmp(packet->dest_node, f6, 6) == 0 &&
							(!global_conf.w95_bug || sockptr->flags & IPX_BROADCAST)
						)
					)
				) || (
					sockptr->flags & IPX_EX_BOUND &&
					packet->dest_socket == sockptr->ex_socket &&
					(
						memcmp(packet->dest_net, sockptr->ex_nic->ipx_net, 4) == 0 ||
						memcmp(packet->dest_net, f6, 4) == 0
					) && (
						memcmp(packet->dest_node, sockptr->ex_nic->ipx_node, 6) == 0 ||
						memcmp(packet->dest_node, f6, 6) == 0
					)
				))
			) {
				addrlen = sizeof(addr);
				if(r_getsockname(sockptr->fd, (struct sockaddr*)&addr, &addrlen) == -1) {
					continue;
				}
				
				sval = r_sendto(sockptr->fd, (char*)packet, rval, 0, (struct sockaddr*)&addr, addrlen);
				if(sval == -1) {
					log_printf("Error relaying packet: %s", w32_error(WSAGetLastError()));
				}
			}
		}
		
		unlock_mutex();
	}
	
	return 0;
}

/* Add a host to the hosts list or update an existing one */
static void add_host(const unsigned char *net, const unsigned char *node, uint32_t ipaddr) {
	ipx_host *hptr = hosts;
	
	while(hptr) {
		if(memcmp(hptr->ipx_net, net, 4) == 0 && memcmp(hptr->ipx_node, node, 6) == 0) {
			hptr->ipaddr = ipaddr;
			hptr->last_packet = time(NULL);
			
			return;
		}
		
		hptr = hptr->next;
	}
	
	hptr = malloc(sizeof(ipx_host));
	if(!hptr) {
		log_printf("No memory for hosts list entry");
		return;
	}
	
	memcpy(hptr->ipx_net, net, 4);
	memcpy(hptr->ipx_node, node, 6);
	
	hptr->ipaddr = ipaddr;
	hptr->last_packet = time(NULL);
	
	hptr->next = hosts;
	hosts = hptr;
}

/* Search the hosts list */
ipx_host *find_host(const unsigned char *net, const unsigned char *node) {
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
				
				return NULL;
			}else{
				return hptr;
			}
		}
		
		pptr = hptr;
		hptr = hptr->next;
	}
	
	return NULL;
}
