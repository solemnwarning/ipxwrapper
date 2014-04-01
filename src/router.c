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
#include <uthash.h>
#include <time.h>
#include <pcap.h>
#include <Win32-Extensions.h>

#include "router.h"
#include "common.h"
#include "ipxwrapper.h"
#include "interface.h"
#include "addrcache.h"
#include "addrtable.h"

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

#define BCAST_NET  addr32_in((unsigned char[]){0xFF,0xFF,0xFF,0xFF})
#define BCAST_NODE addr48_in((unsigned char[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF})

static void _deliver_packet(
	uint8_t type,
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const void *data,
	size_t data_size)
{
	{
		IPX_STRING_ADDR(src_addr, src_net, src_node, src_socket);
		IPX_STRING_ADDR(dest_addr, dest_net, dest_node, dest_socket);
		
		log_printf(LOG_DEBUG, "Delivering %u byte payload from %s to %s",
			(unsigned int)(data_size), src_addr, dest_addr);
	}
	
	lock_sockets();
	
	ipx_socket *sock, *tmp;
	HASH_ITER(hh, sockets, sock, tmp)
	{
		if(sock->flags & IPX_IS_SPX)
		{
			/* Socket is SPX */
			continue;
		}
		
		if(!(sock->flags & IPX_BOUND))
		{
			/* Socket isn't bound */
			continue;
		}
		
		if(!(sock->flags & IPX_RECV))
		{
			/* Socket is shut down for receive operations. */
			continue;
		}
		
		if((sock->flags & IPX_FILTER) && sock->f_ptype != type)
		{
			/* Socket has packet type filtering enabled and this
			 * packet is of the wrong type.
			*/
			continue;
		}
		
		if((dest_net != addr32_in(sock->addr.sa_netnum) && dest_net != BCAST_NET)
			|| (dest_node != addr48_in(sock->addr.sa_nodenum) && dest_node != BCAST_NODE)
			|| dest_socket != sock->addr.sa_socket)
		{
			/* Packet destination address is neither the local
			 * address of this socket nor broadcast.
			*/
			continue;
		}
		
		if((sock->flags & IPX_CONNECTED)
			&& (src_net != addr32_in(sock->remote_addr.sa_netnum)
			|| src_node != addr48_in(sock->remote_addr.sa_nodenum)
			|| src_socket != sock->remote_addr.sa_socket))
		{
			/* Socket is "connected" and the source address isn't
			 * the remote address of the socket.
			*/
			continue;
		}
		
		log_printf(LOG_DEBUG, "...relaying to local port %hu", ntohs(sock->port));
		
		size_t packet_size = (sizeof(ipx_packet) + data_size) - 1;
		
		ipx_packet *packet = malloc(packet_size);
		if(!packet)
		{
			log_printf(LOG_ERROR, "Cannot allocate memory!");
			continue;
		}
		
		packet->ptype = type;
		
		addr32_out(packet->dest_net, dest_net);
		addr48_out(packet->dest_node, dest_node);
		packet->dest_socket = dest_socket;
		
		addr32_out(packet->src_net, src_net);
		addr48_out(packet->src_node, src_node);
		packet->src_socket = src_socket;
		
		packet->size = data_size;
		memcpy(packet->data, data, data_size);
		
		struct sockaddr_in send_addr;
		
		send_addr.sin_family      = AF_INET;
		send_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		send_addr.sin_port        = sock->port;
		
		if(sendto(private_socket, (void*)(packet), packet_size, 0, (struct sockaddr*)(&send_addr), sizeof(send_addr)) == -1)
		{
			log_printf(LOG_ERROR, "Error relaying packet: %s", w32_error(WSAGetLastError()));
		}
		
		free(packet);
	}
	
	unlock_sockets();
}

static void _handle_udp_recv(ipx_packet *packet, size_t packet_size, struct sockaddr_in src_ip)
{
	size_t data_size = ntohs(packet->size);
	
	if(packet_size < sizeof(ipx_packet) - 1 || data_size > MAX_DATA_SIZE || data_size + sizeof(ipx_packet) - 1 != packet_size)
	{
		/* Packet size field is incorrect. */
		return;
	}
	
	if(packet->src_socket == 0)
	{
		/* A source socket of zero indicates internal IPXWrapper
		 * traffic. The ptype determines what should be done.
		 * 
		 * The destination address of any such packet will usually be
		 * all zeroes to prevent older versions of IPXWrapper from
		 * passing them on to applications, since they can't bind to
		 * socket zero. This is also what prevents them from generating
		 * such packets.
		*/
		
		if(packet->ptype == IPX_MAGIC_SPXLOOKUP)
		{
			/* The other system is trying to resolve the IP address
			 * and port number of a listening SPX socket.
			*/
			
			if(data_size != sizeof(spxlookup_req_t))
			{
				log_printf(LOG_DEBUG, "Recieved IPX_MAGIC_SPXLOOKUP packet with %hu byte payload, dropping", data_size);
				return;
			}
			
			spxlookup_req_t *req = (spxlookup_req_t*)(packet->data);
			
			/* Search the sockets table for a listening socket which
			 * is bound to the requested address.
			*/
			
			lock_sockets();
			
			ipx_socket *s, *tmp;
			HASH_ITER(hh, sockets, s, tmp)
			{
				if(
					s->flags & IPX_IS_SPX
					&& s->flags & IPX_LISTENING
					&& memcmp(req->net, s->addr.sa_netnum, 4) == 0
					&& memcmp(req->node, s->addr.sa_nodenum, 6) == 0
					&& req->socket == s->addr.sa_socket)
				{
					/* This socket seems to fit the bill.
					 * Reply with the port number.
					*/
					
					spxlookup_reply_t reply;
					memset(&reply, 0, sizeof(reply));
					
					memcpy(reply.net, req->net, 4);
					memcpy(reply.node, req->node, 6);
					reply.socket = req->socket;
					
					reply.port = s->port;
					
					if(sendto(private_socket, (char*)(&reply), sizeof(reply), 0, (struct sockaddr*)(&src_ip), sizeof(src_ip)) == -1)
					{
						log_printf(LOG_ERROR, "Cannot send spxlookup_reply packet: %s", w32_error(WSAGetLastError()));
					}
					
					break;
				}
			}
			
			unlock_sockets();
		}
		else{
			log_printf(LOG_DEBUG, "Recieved magic packet unknown ptype %u, dropping", (unsigned int)(packet->ptype));
		}
		
		return;
	}
	
	if(min_log_level <= LOG_DEBUG)
	{
		IPX_STRING_ADDR(src_addr, addr32_in(packet->src_net), addr48_in(packet->src_node), packet->src_socket);
		IPX_STRING_ADDR(dest_addr, addr32_in(packet->dest_net), addr48_in(packet->dest_node), packet->dest_socket);
		
		log_printf(LOG_DEBUG, "Recieved packet from %s (%s) for %s", src_addr, inet_ntoa(src_ip.sin_addr), dest_addr);
	}
	
	/* Check that the source IP of the UDP packet is within the subnet of a
	 * valid interface. IPX broadcast packets will be accepted on any
	 * enabled interface, unicast only on the interface with the destination
	 * address.
	*/
	
	ipx_interface_t *allow_interfaces;
	BOOL source_ok = FALSE;
	
	if(addr48_in(packet->dest_node) == BCAST_NODE)
	{
		allow_interfaces = get_ipx_interfaces();
	}
	else{
		allow_interfaces = ipx_interface_by_addr(
			addr32_in(packet->dest_net), addr32_in(packet->dest_node));
	}
	
	ipx_interface_t *i;
	DL_FOREACH(allow_interfaces, i)
	{
		ipx_interface_ip_t *ip;
		DL_FOREACH(i->ipaddr, ip)
		{
			if((ip->ipaddr & ip->netmask) == (src_ip.sin_addr.s_addr & ip->netmask))
			{
				source_ok = TRUE;
			}
		}
	}
	
	free_ipx_interface_list(&allow_interfaces);
	
	if(!source_ok)
	{
		return;
	}
	
	/* Packet appears to have arrived from where we expect. Cache the source
	 * IP address and destination IPX address so future send operations to
	 * that IPX address can be unicast.
	*/
	
	addr_cache_set(
		(struct sockaddr*)(&src_ip), sizeof(src_ip),
		addr32_in(packet->src_net), addr48_in(packet->src_node), packet->src_socket
	);
	
	_deliver_packet(packet->ptype,
		addr32_in(packet->src_net),
		addr48_in(packet->src_node),
		packet->src_socket,
		
		addr32_in(packet->dest_net),
		addr48_in(packet->dest_node),
		packet->dest_socket,
		
		packet->data,
		data_size);
}

static bool _do_udp_recv(int fd)
{
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
	
	_handle_udp_recv(packet, len, addr);
	
	return true;
}

static void _handle_pcap_frame(u_char *user, const struct pcap_pkthdr *pkt_header, const u_char *pkt_data)
{
	ipx_interface_t *iface = (ipx_interface_t*)(user);
	
	if(pkt_header->caplen < sizeof(ethernet_frame_t))
	{
		/* Frame isn't big enough to contain a full IPX header. */
		return;
	}
	
	ethernet_frame_t *frame = (ethernet_frame_t*)(pkt_data);
	
	if(ntohs(frame->ethertype) != 0x8137)
	{
		/* The ethertype field isn't IPX. */
		return;
	}
	
	if(frame->packet.checksum != 0xFFFF)
	{
		/* The "checksum" field doesn't have the magic IPX value. */
		return;
	}
	
	if(ntohs(frame->packet.length) > (pkt_header->caplen - (sizeof(*frame) - sizeof(frame->packet))))
	{
		/* The "length" field in the IPX header is too big. */
		return;
	}
	
	{
		addr48_t dest  = addr48_in(frame->packet.dest_node);
		addr48_t bcast = addr48_in((unsigned char[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF});
		
		if(dest != iface->mac_addr && dest != bcast)
		{
			/* The destination node number isn't that of the card or
			 * the broadcast address.
			*/
			return;
		}
	}
	
	_deliver_packet(frame->packet.type,
		addr32_in(frame->packet.src_net),
		addr48_in(frame->packet.src_node),
		frame->packet.src_socket,
		
		addr32_in(frame->packet.dest_net),
		addr48_in(frame->packet.dest_node),
		frame->packet.dest_socket,
		
		frame->packet.data,
		(ntohs(frame->packet.length) - sizeof(real_ipx_packet_t)));
}

static DWORD router_main(void *arg)
{
	DWORD exit_status = 0;
	
	time_t last_at_update = 0;
	
	ipx_interface_t *interfaces = NULL;
	
	HANDLE *wait_events = &router_event;
	int n_events = 1;
	
	if(ipx_use_pcap)
	{
		interfaces = get_ipx_interfaces();
		ipx_interface_t *i;
		
		DL_FOREACH(interfaces, i)
		{
			++n_events;
		}
		
		wait_events = malloc(n_events * sizeof(HANDLE));
		if(!wait_events)
		{
			log_printf(LOG_ERROR, "Could not allocate memory!");
			
			free_ipx_interface_list(&interfaces);
			return 1;
		}
		
		n_events = 1;
		wait_events[0] = router_event;
		
		DL_FOREACH(interfaces, i)
		{
			wait_events[n_events++] = pcap_getevent(i->pcap);
		}
	}
	
	while(1)
	{
		WaitForMultipleObjects(n_events, wait_events, FALSE, 1000);
		WSAResetEvent(router_event);
		
		if(!router_running)
		{
			break;
		}
		
		if(last_at_update != time(NULL))
		{
			addr_table_update();
			last_at_update = time(NULL);
		}
		
		if(ipx_use_pcap)
		{
			ipx_interface_t *i;
			DL_FOREACH(interfaces, i)
			{
				if(pcap_dispatch(i->pcap, -1, &_handle_pcap_frame, (u_char*)(i)) == -1)
				{
					log_printf(LOG_ERROR, "Could not dispatch frames on WinPcap interface: %s", pcap_geterr(i->pcap));
					log_printf(LOG_WARNING, "No more IPX packets will be received");
					
					exit_status = 1;
					break;
				}
			}
		}
		else{
			if(!_do_udp_recv(shared_socket) || !_do_udp_recv(private_socket))
			{
				exit_status = 1;
				break;
			}
		}
	}
	
	if(ipx_use_pcap)
	{
		free(wait_events);
		free_ipx_interface_list(&interfaces);
	}
	
	return exit_status;
}
