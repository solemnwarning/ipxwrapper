/* IPXWrapper - Router header
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

#ifndef IPXWRAPPER_ROUTER_H
#define IPXWRAPPER_ROUTER_H

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <stdint.h>

/* Special address family for use when binding AF_IPX sockets, allows multiple
 * sockets to share the same address.
*/
#define AF_IPX_SHARE 42

/* Represents a bound IPX address */
struct router_addr {
	struct sockaddr_ipx addr;
	
	uint16_t local_port;	/* Local UDP port */
	SOCKET ws_socket;	/* Application socket */
	SOCKET control_socket;	/* Control socket */
	
	struct router_addr *next;
};

struct router_vars {
	BOOL running;
	
	struct ipx_interface *interfaces;
	
	SOCKET udp_sock;
	SOCKET listner;
	
	WSAEVENT wsa_event;
	
	CRITICAL_SECTION crit_sec;
	BOOL crit_sec_init;
	
	struct router_addr *addrs;
	
	char *recvbuf;
};

struct router_vars *router_init(BOOL global);
void router_destroy(struct router_vars *router);

int router_bind(struct router_vars *router, SOCKET control, SOCKET sock, struct sockaddr_ipx *addr);
void router_set_port(struct router_vars *router, SOCKET control, SOCKET sock, uint16_t port);
void router_close(struct router_vars *router, SOCKET control, SOCKET sock);

#endif /* !IPXWRAPPER_ROUTER_H */
