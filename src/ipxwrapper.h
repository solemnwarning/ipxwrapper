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
#include <uthash.h>

#include "config.h"
#include "router.h"

/* The standard Windows driver (in XP) only allows 1467 bytes anyway */
#define MAX_DATA_SIZE 8192
#define MAX_PKT_SIZE 8219

#define IPX_FILTER	(int)(1<<0)
#define IPX_BOUND	(int)(1<<1)
#define IPX_BROADCAST	(int)(1<<2)
#define IPX_SEND	(int)(1<<3)
#define IPX_RECV	(int)(1<<4)
#define IPX_REUSE	(int)(1<<6)
#define IPX_CONNECTED	(int)(1<<7)
#define IPX_RECV_BCAST	(int)(1<<8)
#define IPX_EXT_ADDR	(int)(1<<9)

#define RETURN(...) \
	unlock_sockets();\
	return __VA_ARGS__;

#define RETURN_WSA(errnum, ...) \
	unlock_sockets();\
	WSASetLastError(errnum);\
	return __VA_ARGS__;

typedef struct ipx_socket ipx_socket;
typedef struct ipx_packet ipx_packet;

struct ipx_socket {
	SOCKET fd;
	
	/* Locally bound UDP port number (Network byte order).
	 * Undefined before IPX bind() call.
	*/
	uint16_t port;
	
	int flags;
	uint8_t s_ptype;
	uint8_t f_ptype;	/* Undefined when IPX_FILTER isn't set */
	
	/* The following values are undefined when IPX_BOUND is not set */
	struct sockaddr_ipx addr;
	
	/* Address used with connect call, only set when IPX_CONNECTED is */
	struct sockaddr_ipx remote_addr;
	
	UT_hash_handle hh;
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

extern ipx_socket *sockets;
extern main_config_t main_config;

ipx_socket *get_socket(SOCKET sockfd);
void lock_sockets(void);
void unlock_sockets(void);

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
int PASCAL r_connect(SOCKET fd, const struct sockaddr *addr, int addrlen);
int PASCAL r_send(SOCKET fd, const char *buf, int len, int flags);
int PASCAL r_getpeername(SOCKET fd, struct sockaddr *addr, int *addrlen);

#endif /* !IPXWRAPPER_H */
