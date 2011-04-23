/* ipxwrapper - Library header
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

#ifndef IPXWRAPPER_H
#define IPXWRAPPER_H
#include <windows.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <stdint.h>

#define PORT 54792
#define DEBUG "ipxwrapper.log"

/* Maximum UDP data size is 65467, we use a smaller value to ensure we have
 * plenty of space to play with for headers, etc
*/
#define MAX_PACKET_SIZE 63487
#define PACKET_BUF_SIZE 65536

#define IPX_FILTER	(int)(1<<0)
#define IPX_BOUND	(int)(1<<1)
#define IPX_BROADCAST	(int)(1<<2)
#define IPX_SEND	(int)(1<<3)
#define IPX_RECV	(int)(1<<4)

#define INIT_SOCKET(ptr) \
	(ptr)->fd = -1;\
	(ptr)->flags = IPX_SEND | IPX_RECV;\
	(ptr)->s_ptype = 0;\
	(ptr)->f_ptype = 0;\
	memset((ptr)->netnum, 0, 4);\
	memset((ptr)->nodenum, 0, 6);\
	(ptr)->socket = 0;\
	(ptr)->next = NULL;

#define INIT_PACKET(ptr) \
	(ptr)->ptype = 0;\
	memset((ptr)->dest_net, 0, 4);\
	memset((ptr)->dest_node, 0, 6);\
	(ptr)->dest_socket = 0;\
	memset((ptr)->src_net, 0, 4);\
	memset((ptr)->src_node, 0, 6);\
	(ptr)->src_socket = 0;\
	(ptr)->size = 0;

#define INIT_NIC(ptr) \
	(ptr)->ipaddr = 0;\
	(ptr)->bcast = 0;\
	memset((ptr)->hwaddr, 0, 6);\
	(ptr)->next = NULL;

#define INIT_HOST(ptr) \
	memset((ptr)->hwaddr, 0, 6);\
	(ptr)->ipaddr = 0;\
	(ptr)->next = NULL;

#define RETURN(...) \
	unlock_mutex();\
	return __VA_ARGS__;

#define RETURN_WSA(errnum, ...) \
	unlock_mutex();\
	WSASetLastError(errnum);\
	return __VA_ARGS__;

#define RETURN_ERR(errnum, ...) \
	unlock_mutex();\
	SetLastError(errnum);\
	return __VA_ARGS__;

typedef struct ipx_socket ipx_socket;
typedef struct ipx_packet ipx_packet;
typedef struct ipx_nic ipx_nic;
typedef struct ipx_host ipx_host;

struct ipx_socket {
	SOCKET fd;
	
	int flags;
	uint8_t s_ptype;
	uint8_t f_ptype;
	
	unsigned char netnum[4];
	unsigned char nodenum[6];
	uint16_t socket;
	
	ipx_socket *next;
};

struct ipx_packet {
	uint8_t ptype;
	
	unsigned char dest_net[4];
	unsigned char dest_node[6];
	uint16_t dest_socket;
	
	unsigned char src_net[4];
	unsigned char src_node[6];
	uint16_t src_socket;
	
	uint16_t size;
	char data[1];
} __attribute__((__packed__));

struct ipx_nic {
	uint32_t ipaddr;
	uint32_t bcast;
	
	unsigned char hwaddr[6];
	
	ipx_nic *next;
};

struct ipx_host {
	unsigned char hwaddr[6];
	uint32_t ipaddr;
	
	ipx_host *next;
};

/* Interface settings stored in registry */
struct reg_value {
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	unsigned char enabled;
	unsigned char primary;
} __attribute__((__packed__));

extern ipx_socket *sockets;
extern ipx_nic *nics;
extern ipx_host *hosts;
extern SOCKET net_fd;

extern HMODULE winsock2_dll;
extern HMODULE mswsock_dll;
extern HMODULE wsock32_dll;

void __stdcall *find_sym(char const *sym);
void debug(char const *fmt, ...);
ipx_socket *get_socket(SOCKET fd);
void lock_mutex(void);
void unlock_mutex(void);
IP_ADAPTER_INFO *get_nics(void);
char const *w32_error(DWORD errnum);
ipx_host *find_host(unsigned char *hwaddr);

INT APIENTRY r_EnumProtocolsA(LPINT,LPVOID,LPDWORD);
INT APIENTRY r_EnumProtocolsW(LPINT,LPVOID,LPDWORD);
int PASCAL FAR r_WSARecvEx(SOCKET,char*,int,int*);
int WSAAPI r_bind(SOCKET,const struct sockaddr*,int);
int WSAAPI r_closesocket(SOCKET);
int WSAAPI r_getsockname(SOCKET,struct sockaddr*,int*);
int WSAAPI r_getsockopt(SOCKET,int,int,char*,int*);
int WSAAPI r_recv(SOCKET,char*,int,int);
int WSAAPI r_recvfrom(SOCKET,char*,int,int,struct sockaddr*,int*);
int WSAAPI r_sendto(SOCKET,const char*,int,int,const struct sockaddr*,int);
int WSAAPI r_setsockopt(SOCKET,int,int,const char*,int);
int WSAAPI r_shutdown(SOCKET,int);
SOCKET WSAAPI r_socket(int,int,int);
int PASCAL r_ioctlsocket(SOCKET fd, long cmd, u_long *argp);

#endif /* !IPXWRAPPER_H */
