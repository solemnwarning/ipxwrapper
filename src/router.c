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
#include "ipxwrapper.h"
#include "interface.h"
#include "addrcache.h"

static bool router_running   = false;
static WSAEVENT router_event = WSA_INVALID_EVENT;
static HANDLE router_thread  = NULL;
static char *router_buf      = NULL;

/* The shared socket uses the UDP port number specified in the configuration,
 * every IPXWrapper instance will share it and use it to receive broadcast
 * packets.
 * 
 * The private socket uses a randomly allocated UDP port number and is used to
 * send packets and receive unicast, it is unique to a single IPXWrapper
 * instance.
*/

SOCKET shared_socket  = -1;
SOCKET private_socket = -1;

static DWORD router_main(void *arg);

/* Initialise a UDP socket. */
static void _init_socket(SOCKET *sock, uint16_t port, BOOL reuseaddr)
{
	/* Socket used for sending and receiving packets on the network. */
	
	if((*sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		log_printf(LOG_ERROR, "Error creating UDP socket: %s", w32_error(WSAGetLastError()));
		abort();
	}
	
	/* Enable broadcast and address reuse. */
	
	BOOL broadcast = TRUE;
	
	setsockopt(*sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(BOOL));
	setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseaddr, sizeof(BOOL));
	
	/* Set send/receive buffer size to 512KiB. */
	
	int bufsize = 524288;
	
	setsockopt(*sock, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(int));
	setsockopt(*sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(int));
	
	struct sockaddr_in addr;
	
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(port);
	
	if(bind(*sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
	{
		log_printf(LOG_ERROR, "Error binding UDP socket: %s", w32_error(WSAGetLastError()));
		abort();
	}
	
	if(WSAEventSelect(*sock, router_event, FD_READ) == -1)
	{
		log_printf(LOG_ERROR, "WSAEventSelect error: %s", w32_error(WSAGetLastError()));
		abort();
	}
}

/* Initialise the UDP socket and router worker thread.
 * Aborts on failure.
*/
void router_init(void)
{
	if(!(router_buf = malloc(MAX_PKT_SIZE)))
	{
		log_printf(LOG_ERROR, "Out of memory! Cannot allocate router buffer");
		abort();
	}
	
	/* Event object used for notification of new packets and exit signal. */
	
	if((router_event = WSACreateEvent()) == WSA_INVALID_EVENT)
	{
		log_printf(LOG_ERROR, "Error creating WSA event object: %s", w32_error(WSAGetLastError()));
		abort();
	}
	
	_init_socket(&shared_socket, main_config.udp_port, TRUE);
	_init_socket(&private_socket, 0, FALSE);
	
	router_running = true;
	
	if(!(router_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(&router_main), NULL, 0, NULL)))
	{
		log_printf(LOG_ERROR, "Cannot create router worker thread: %s", w32_error(GetLastError()));
		abort();
	}
}

void router_cleanup(void)
{
	/* Signal the router thread to exit. */
	
	router_running = false;
	SetEvent(router_event);
	
	/* Wait for it to exit, kill if it takes too long. */
	
	if(WaitForSingleObject(router_thread, 3000) == WAIT_TIMEOUT)
	{
		log_printf(LOG_WARNING, "Router thread didn't exit in 3 seconds, killing");
		TerminateThread(router_thread, 0);
	}
	
	CloseHandle(router_thread);
	router_thread = NULL;
	
	/* Release resources. */
	
	if(private_socket != -1)
	{
		closesocket(private_socket);
		private_socket = -1;
	}
	
	if(shared_socket != -1)
	{
		closesocket(shared_socket);
		shared_socket = -1;
	}
	
	if(router_event != WSA_INVALID_EVENT)
	{
		WSACloseEvent(router_event);
		router_event = WSA_INVALID_EVENT;
	}
	
	free(router_buf);
	router_buf = NULL;
}

/* Recieve and process a packet from one of the UDP sockets. */
static bool handle_recv(int fd)
{
	const unsigned char f6[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);
	
	ipx_packet *packet = (ipx_packet*)(router_buf);
	
	int len = recvfrom(fd, (char*)packet, MAX_PKT_SIZE, 0, (struct sockaddr*)(&addr), &addrlen);
	if(len == -1)
	{
		if(WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAECONNRESET)
		{
			return true;
		}
		
		return false;
	}
	
	packet->size = ntohs(packet->size);
	
	if(len < sizeof(ipx_packet) - 1 || packet->size > MAX_DATA_SIZE || packet->size + sizeof(ipx_packet) - 1 != len)
	{
		/* Packet size incorrect. */
		
		return true;
	}
	
	if(min_log_level <= LOG_DEBUG)
	{
		IPX_STRING_ADDR(src_addr, addr32_in(packet->src_net), addr48_in(packet->src_node), packet->src_socket);
		IPX_STRING_ADDR(dest_addr, addr32_in(packet->dest_net), addr48_in(packet->dest_node), packet->dest_socket);
		
		log_printf(LOG_DEBUG, "Recieved packet from %s (%s) for %s", src_addr, inet_ntoa(addr.sin_addr), dest_addr);
	}
	
	addr_cache_set(
		(struct sockaddr*)(&addr), sizeof(addr),
		addr32_in(packet->src_net), addr48_in(packet->src_node), packet->src_socket
	);
	
	lock_sockets();
	
	ipx_socket *sock = sockets;
	
	for(; sock; sock = sock->next)
	{
		if(
			/* Socket is bound and not shutdown for recv. */
			
			(sock->flags & IPX_BOUND) && (sock->flags & IPX_RECV)
			
			/* Packet type filtering is off, or packet type matches
			 * filter.
			*/
			
			&& (!(sock->flags & IPX_FILTER) || sock->f_ptype == packet->ptype)
			
			/* Packet destination address is correct. */
			
			&& (memcmp(packet->dest_net, sock->addr.sa_netnum, 4) == 0 || (memcmp(packet->dest_net, f6, 4) == 0 && (sock->flags & IPX_BROADCAST || !main_config.w95_bug) && sock->flags & IPX_RECV_BCAST))
			&& (memcmp(packet->dest_node, sock->addr.sa_nodenum, 6) == 0 || (memcmp(packet->dest_node, f6, 6) == 0 && (sock->flags & IPX_BROADCAST || !main_config.w95_bug) && sock->flags & IPX_RECV_BCAST))
			&& packet->dest_socket == sock->addr.sa_socket
			
			/* Socket has not been connected, or the IPX source
			 * address is correct.
			*/
			
			&& (
				!(sock->flags & IPX_CONNECTED)
				
				|| (
					memcmp(sock->remote_addr.sa_netnum, packet->src_net, 4) == 0
					&& memcmp(sock->remote_addr.sa_nodenum, packet->src_node, 6) == 0
					&& sock->remote_addr.sa_socket == packet->src_socket
				)
			)
		) {
			addr32_t iface_net  = addr32_in(sock->addr.sa_netnum);
			addr48_t iface_node = addr48_in(sock->addr.sa_nodenum);
			
			/* Fetch the interface this socket is bound to. */
			
			ipx_interface_t *iface = ipx_interface_by_addr(iface_net, iface_node);
			
			if(!iface)
			{
				char net_s[ADDR32_STRING_SIZE], node_s[ADDR48_STRING_SIZE];
				
				addr32_string(net_s, iface_net);
				addr48_string(node_s, iface_node);
				
				log_printf(LOG_WARNING, "No iface for %s/%s! Stale bind?", net_s, node_s);
				
				continue;
			}
			
			/* Iterate over the subnets and compare to the packet
			 * source IP address.
			*/
			
			ipx_interface_ip_t *ip;
			
			int source_ok = 0;
			
			DL_FOREACH(iface->ipaddr, ip)
			{
				if((ip->ipaddr & ip->netmask) == (addr.sin_addr.s_addr & ip->netmask))
				{
					source_ok = 1;
					break;
				}
			}
			
			free_ipx_interface(iface);
			
			if(!source_ok)
			{
				/* Packet did not originate from an associated
				 * network.
				*/
				
				continue;
			}
			
			log_printf(LOG_DEBUG, "Relaying packet to local port %hu", ntohs(sock->port));
			
			struct sockaddr_in send_addr;
			
			send_addr.sin_family      = AF_INET;
			send_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			send_addr.sin_port        = sock->port;
			
			if(sendto(private_socket, router_buf, len, 0, (struct sockaddr*)(&send_addr), sizeof(send_addr)) == -1)
			{
				log_printf(LOG_ERROR, "Error relaying packet: %s", w32_error(WSAGetLastError()));
			}
		}
	}
	
	unlock_sockets();
	
	return true;
}

static DWORD router_main(void *arg)
{
	while(1)
	{
		WaitForSingleObject(router_event, INFINITE);
		
		WSAResetEvent(router_event);
		
		if(!router_running)
		{
			return 0;
		}
		
		if(!handle_recv(shared_socket) || !handle_recv(private_socket))
		{
			return 1;
		}
	}
	
	return 0;
}
