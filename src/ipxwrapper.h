/* ipxwrapper - Library header
 * Copyright (C) 2008-2011 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <wsipx.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "router.h"

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
#define IPX_EX_BOUND	(int)(1<<5)
#define IPX_REUSE	(int)(1<<6)

#define RETURN(...) \
	unlock_sockets();\
	return __VA_ARGS__;

#define RETURN_WSA(errnum, ...) \
	unlock_sockets();\
	WSASetLastError(errnum);\
	return __VA_ARGS__;

typedef struct ipx_socket ipx_socket;
typedef struct ipx_packet ipx_packet;
typedef struct ipx_host ipx_host;

struct ipx_socket {
	SOCKET fd;
	
	int flags;
	uint8_t s_ptype;
	uint8_t f_ptype;	/* Undefined when IPX_FILTER isn't set */
	
	/* The following values are undefined when IPX_BOUND is not set */
	struct sockaddr_ipx addr;
	uint32_t nic_bcast;
	
	/* Extra bind address, only used for receiving packets.
	 * Only defined when IPX_EX_BOUND is set.
	*/
	struct ipx_interface *ex_nic;
	uint16_t ex_socket;
	
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

struct ipx_host {
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	
	uint32_t ipaddr;
	time_t last_packet;
	
	ipx_host *next;
};

extern ipx_socket *sockets;
extern struct ipx_interface *nics;
extern ipx_host *hosts;
extern SOCKET net_fd;
extern struct reg_global global_conf;
extern struct router_vars *router;

extern HMODULE winsock2_dll;
extern HMODULE mswsock_dll;
extern HMODULE wsock32_dll;

void __stdcall *find_sym(char const *sym);
ipx_socket *get_socket(SOCKET fd);
void lock_sockets(void);
void unlock_sockets(void);
ipx_host *find_host(const unsigned char *net, const unsigned char *node);
void add_host(const unsigned char *net, const unsigned char *node, uint32_t ipaddr);

void log_open();
void log_close();

int ipx_ex_bind(SOCKET fd, const struct sockaddr_ipx *ipxaddr);

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
