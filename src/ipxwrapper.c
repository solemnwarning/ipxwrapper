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

#define DLL_UNLOAD(dll) \
	if(dll) {\
		FreeModule(dll);\
		dll = NULL;\
	}

ipx_socket *sockets = NULL;
ipx_nic *nics = NULL;
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
static HKEY regkey = NULL;

static HMODULE load_sysdll(char const *name);
static int init_router(void);
static DWORD WINAPI router_main(LPVOID argp);
static void add_host(const unsigned char *net, const unsigned char *node, uint32_t ipaddr);
static BOOL load_nics(void);

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	if(why == DLL_PROCESS_ATTACH) {
		log_open();
		
		winsock2_dll = load_sysdll("ws2_32.dll");
		mswsock_dll = load_sysdll("mswsock.dll");
		wsock32_dll = load_sysdll("wsock32.dll");
		
		if(!winsock2_dll || !mswsock_dll || !wsock32_dll) {
			return FALSE;
		}
		
		int reg_err = RegOpenKeyEx(
			HKEY_CURRENT_USER,
			"Software\\IPXWrapper",
			0,
			KEY_QUERY_VALUE,
			&regkey
		);
		
		if(reg_err != ERROR_SUCCESS) {
			regkey = NULL;
			log_printf("Could not open registry: %s", w32_error(reg_err));
		}
		
		DWORD gsize = sizeof(global_conf);
		
		if(!regkey || RegQueryValueEx(regkey, "global", NULL, NULL, (BYTE*)&global_conf, &gsize) != ERROR_SUCCESS || gsize != sizeof(global_conf)) {
			global_conf.udp_port = DEFAULT_PORT;
			global_conf.w95_bug = 1;
			global_conf.bcast_all = 0;
			global_conf.filter = 1;
		}
		
		if(!load_nics()) {
			return FALSE;
		}
		
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
		
		WSACleanup();
		
		if(regkey) {
			RegCloseKey(regkey);
			regkey = NULL;
		}
		
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

IP_ADAPTER_INFO *get_nics(void) {
	IP_ADAPTER_INFO *buf, tbuf;
	ULONG bufsize = sizeof(IP_ADAPTER_INFO);
	
	int rval = GetAdaptersInfo(&tbuf, &bufsize);
	if(rval != ERROR_SUCCESS && rval != ERROR_BUFFER_OVERFLOW) {
		WSASetLastError(rval);
		return NULL;
	}
	
	buf = malloc(bufsize);
	if(!buf) {
		WSASetLastError(ERROR_OUTOFMEMORY);
		return NULL;
	}
	
	rval = GetAdaptersInfo(buf, &bufsize);
	if(rval != ERROR_SUCCESS) {
		WSASetLastError(rval);
		free(buf);
		
		return NULL;
	}
	
	return buf;
}

/* Load a system DLL */
static HMODULE load_sysdll(char const *name) {
	char sysdir[1024], path[1024];
	HMODULE ret = NULL;
	
	GetSystemDirectory(sysdir, 1024);
	snprintf(path, 1024, "%s\\%s", sysdir, name);
	
	ret = LoadLibrary(path);
	if(!ret) {
		log_printf("Error loading %s: %s", path, w32_error(GetLastError()));
	}
	
	return ret;
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
			ipx_nic *nic = nics;
			
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
				sockptr->flags & IPX_BOUND &&
				sockptr->flags & IPX_RECV &&
				packet->dest_socket == sockptr->socket &&
				(
					!(sockptr->flags & IPX_FILTER) ||
					packet->ptype == sockptr->f_ptype
				) && (
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

/* Convert a windows error number to an error message */
char const *w32_error(DWORD errnum) {
	static char buf[1024] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, 1023, NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
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

static BOOL load_nics(void) {
	IP_ADAPTER_INFO *ifroot = get_nics();
	IP_ADAPTER_INFO *ifptr = ifroot;
	ipx_nic *enic = NULL;
	
	if(!ifptr) {
		log_printf("No NICs: %s", w32_error(WSAGetLastError()));
	}
	
	while(ifptr) {
		struct reg_value rv;
		int got_rv = 0;
		
		if(regkey) {
			char vname[18];
			NODE_TO_STRING(vname, ifptr->Address);
			
			DWORD rv_size = sizeof(rv);
			
			int reg_err = RegQueryValueEx(regkey, vname, NULL, NULL, (BYTE*)&rv, &rv_size);
			if(reg_err == ERROR_SUCCESS && rv_size == sizeof(rv)) {
				got_rv = 1;
			}
		}
		
		if(got_rv && !rv.enabled) {
			/* Interface has been disabled, don't add it */
			ifptr = ifptr->Next;
			continue;
		}
		
		ipx_nic *nnic = malloc(sizeof(ipx_nic));
		if(!nnic) {
			return FALSE;
		}
		
		nnic->ipaddr = inet_addr(ifptr->IpAddressList.IpAddress.String);
		nnic->netmask = inet_addr(ifptr->IpAddressList.IpMask.String);
		nnic->bcast = nnic->ipaddr | ~nnic->netmask;
		
		memcpy(nnic->hwaddr, ifptr->Address, 6);
		
		if(got_rv) {
			memcpy(nnic->ipx_net, rv.ipx_net, 4);
			memcpy(nnic->ipx_node, rv.ipx_node, 6);
		}else{
			unsigned char net[] = {0,0,0,1};
			
			memcpy(nnic->ipx_net, net, 4);
			memcpy(nnic->ipx_node, nnic->hwaddr, 6);
		}
		
		nnic->next = NULL;
		
		if(got_rv && rv.primary) {
			/* Force primary flag set, insert at start of NIC list */
			nnic->next = nics;
			nics = nnic;
		}else if(enic) {
			enic->next = nnic;
			enic = nnic;
		}else{
			enic = nics = nnic;
		}
		
		ifptr = ifptr->Next;
	}
	
	free(ifroot);
	
	return TRUE;
}
