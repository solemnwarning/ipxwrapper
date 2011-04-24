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
#include <stdio.h>

#define DEFAULT_PORT 54792
#define TTL 60

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

#define NET_TO_STRING(s, net) \
	sprintf( \
		s, "%02X:%02X:%02X:%02X", \
		(unsigned int)net[0], \
		(unsigned int)net[1], \
		(unsigned int)net[2], \
		(unsigned int)net[3] \
	)

#define NODE_TO_STRING(s, node) \
	sprintf( \
		s, "%02X:%02X:%02X:%02X:%02X:%02X", \
		(unsigned int)node[0], \
		(unsigned int)node[1], \
		(unsigned int)node[2], \
		(unsigned int)node[3], \
		(unsigned int)node[4], \
		(unsigned int)node[5] \
	)

typedef struct ipx_socket ipx_socket;
typedef struct ipx_packet ipx_packet;
typedef struct ipx_nic ipx_nic;
typedef struct ipx_host ipx_host;

struct ipx_socket {
	SOCKET fd;
	
	int flags;
	uint8_t s_ptype;
	uint8_t f_ptype;	/* Undefined when IPX_FILTER isn't set */
	
	/* The following values are undefined when IPX_BOUND is not set */
	ipx_nic *nic;
	uint16_t socket; /* Stored in NETWORK BYTE ORDER */
	
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
	uint32_t netmask;
	uint32_t bcast;
	
	unsigned char hwaddr[6];
	
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	
	ipx_nic *next;
};

struct ipx_host {
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	
	uint32_t ipaddr;
	time_t last_packet;
	
	ipx_host *next;
};

/* Interface settings stored in registry */
struct reg_value {
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	unsigned char enabled;
	unsigned char primary;
} __attribute__((__packed__));

struct reg_global {
	uint16_t udp_port;
	unsigned char w95_bug;
	unsigned char bcast_all;
	unsigned char filter;
} __attribute__((__packed__));

extern ipx_socket *sockets;
extern ipx_nic *nics;
extern ipx_host *hosts;
extern SOCKET net_fd;
extern struct reg_global global_conf;

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
ipx_host *find_host(const unsigned char *net, const unsigned char *node);

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
