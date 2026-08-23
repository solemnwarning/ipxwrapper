/* IPXWrapper - Sequenced Packet Exchange
 * Copyright (C) 2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define WINSOCK_API_LINKAGE

#include <assert.h>
#include <stdint.h>
#include <utlist.h>

#include "ipxwrapper.h"
#include "addr.h"
#include "spx.h"

/**
 * @brief The maximum payload to put in an SPX data packet.
 *
 * I've found various documents claiming IPX has a maximum packet size of 576 bytes, however
 * Windows is quite happy to send larger datagrams than that in my experience and a Windows XP
 * system I tested on sent out 1,496 byte packets when fragmenting larger SPX messages
 * (1454 byte payload) by default.
*/
static const size_t SPX_FRAGMENT_MAX_DATA_SIZE = 1454;

struct spx_queue_element
{
	void *data;
	size_t data_size;
	size_t data_pos;
	
	bool end_of_message;
	
	struct spx_queue_element *next;
};

struct spx_queue
{
	struct spx_queue_element *front;
	struct spx_queue_element *back;
	
	size_t num_complete_messages;
};

static void spx_process_connection_request_packet(
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const struct spx_packet_header *spx_header);

static void spx_process_data_packet(
	ipx_socket *sock,
	const struct spx_packet_header *spx_header,
	const void *data,
	size_t data_size);

static void spx_send_pump(ipx_socket *socket);

static void spx_recv_pump(ipx_socket *socket);

static void spx_rtt_insert(int spx_rtt_history[SPX_RTT_BACKLOG_COUNT], int value);

static BOOL spx_connect_finish(ipx_socket *sock);

/**
 * @brief Push a packet to the back of an spx_queue list.
 *
 * @param queue           The spx_queue to copy the packet into.
 * @param data            Pointer to the SPX packet payload.
 * @param size            The size of the SPX packet payload.
 * @param end_of_message  Whether the packet is the end of a completed message.
 *
 * @return true on success, false on memory allocation failure.
*/
static bool spx_queue_push(struct spx_queue *queue, const void *data, size_t size, bool end_of_message);

static bool spx_queue_push_hdr(struct spx_queue *queue, const struct spx_packet_header *header, const void *data, size_t size, bool end_of_message);

static void spx_queue_merge(struct spx_queue *dst_queue, struct spx_queue *src_queue);

/**
 * @brief Pop and free the packet from the front of an spx_queue list.
 *
 * NOTE: Calling this on an empty queue is undefined behaviour.
*/
static void spx_queue_pop(struct spx_queue *queue);

DWORD ipx_send_packet(
	uint8_t type,
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const void *data,
	size_t data_size);

uint16_t spx_allocate_connection_id(void)
{
	uint16_t id = get_ticks() & 0xFFFF;
	
	bool unique;
	do {
		if(id == 0 || id == 0xFFFF)
		{
			id = 1;
		}
		
		unique = true;
		
		ipx_socket *sock, *tmp;
		HASH_ITER(hh, socket_by_fd, sock, tmp)
		{
			if((sock->flags & IPX_IS_SPX)
				&& (sock->flags & (IPX_CONNECTED | IPX_CONNECTING | IPX_LISTENING)) != 0
				&& sock->local_conn == id)
			{
				unique = false;
				break;
			}
		}
		
		if(!unique)
		{
			++id;
		}
	} while(!unique);
	
	return id;
}

static ipx_socket *spx_find_socket_by_local(addr32_t local_net, addr48_t local_node, uint16_t local_socket, uint16_t local_connection_id)
{
	ipx_socket *sock, *tmp;
	HASH_ITER(hh, socket_by_fd, sock, tmp)
	{
		if((sock->flags & IPX_IS_SPX)
			&& (sock->flags & IPX_BOUND)
			&& (sock->flags & (IPX_LISTENING | IPX_CONNECTING | IPX_CONNECTED | IPX_CLOSED | IPX_CLOSING))
			
			&& local_net == addr32_in(sock->addr.sa_netnum)
			&& local_node == addr48_in(sock->addr.sa_nodenum)
			&& local_socket == sock->addr.sa_socket
			&& local_connection_id == sock->local_conn)
		{
			return sock;
		}
	}
	
	return NULL;
}

static ipx_socket *spx_find_socket_by_remote(addr32_t remote_net, addr48_t remote_node, uint16_t remote_socket, uint16_t remote_connection_id)
{
	ipx_socket *sock, *tmp;
	HASH_ITER(hh, socket_by_fd, sock, tmp)
	{
		if((sock->flags & IPX_IS_SPX)
			&& (sock->flags & IPX_BOUND)
			&& (sock->flags & IPX_CONNECTED)
			
			&& remote_net == addr32_in(sock->remote_addr.sa_netnum)
			&& remote_node == addr48_in(sock->remote_addr.sa_nodenum)
			&& remote_socket == sock->remote_addr.sa_socket
			&& remote_connection_id == sock->remote_conn)
		{
			return sock;
		}
	}
	
	return NULL;
}

DWORD spx_send_connection_request(ipx_socket *socket)
{
	struct spx_packet_header spx_header;

	spx_header.connection_control = SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK;
	spx_header.datastream_type = 0;
	spx_header.src_connection_id = socket->local_conn;
	spx_header.dst_connection_id = 0xFFFF;
	spx_header.seq_number = 0;
	spx_header.ack_number = 0;
	spx_header.allocation_number = 0;

	return ipx_send_packet(
		IPX_TYPE_SPX,
		addr32_in(socket->addr.sa_netnum),
		addr48_in(socket->addr.sa_nodenum),
		socket->addr.sa_socket,
		addr32_in(socket->remote_addr.sa_netnum),
		addr48_in(socket->remote_addr.sa_nodenum),
		socket->remote_addr.sa_socket,
		&spx_header,
		sizeof(spx_header));
}

DWORD spx_send_ack(ipx_socket *socket)
{
	struct spx_packet_header spx_header;

	spx_header.connection_control = SPX_CONNCTRL_SYS;
	spx_header.datastream_type = 0;
	spx_header.src_connection_id = socket->local_conn;
	spx_header.dst_connection_id = socket->remote_conn;
	spx_header.seq_number = htons(socket->spx_send_seq);
	spx_header.ack_number = htons(socket->spx_recv_seq);
	spx_header.allocation_number = htons(socket->spx_recv_seq);

	DWORD result = ipx_send_packet(
		IPX_TYPE_SPX,
		addr32_in(socket->addr.sa_netnum),
		addr48_in(socket->addr.sa_nodenum),
		socket->addr.sa_socket,
		addr32_in(socket->remote_addr.sa_netnum),
		addr48_in(socket->remote_addr.sa_nodenum),
		socket->remote_addr.sa_socket,
		&spx_header,
		sizeof(spx_header));
	
	if(result == ERROR_SUCCESS)
	{
		socket->spx_verify_time = mclock_add_ms(mclock_now(), SPX_VERIFY_TIMEOUT);
	}
	
	return result;
}

DWORD spx_send_watchdog_request(ipx_socket *socket)
{
	struct spx_packet_header spx_header;

	spx_header.connection_control = SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK;
	spx_header.datastream_type = 0;
	spx_header.src_connection_id = socket->local_conn;
	spx_header.dst_connection_id = socket->remote_conn;
	spx_header.seq_number = htons(socket->spx_send_seq);
	spx_header.ack_number = htons(socket->spx_recv_seq);
	spx_header.allocation_number = htons(socket->spx_recv_seq);

	DWORD result = ipx_send_packet(
		IPX_TYPE_SPX,
		addr32_in(socket->addr.sa_netnum),
		addr48_in(socket->addr.sa_nodenum),
		socket->addr.sa_socket,
		addr32_in(socket->remote_addr.sa_netnum),
		addr48_in(socket->remote_addr.sa_nodenum),
		socket->remote_addr.sa_socket,
		&spx_header,
		sizeof(spx_header));
	
	if(result == ERROR_SUCCESS)
	{
		socket->spx_verify_time = mclock_add_ms(mclock_now(), SPX_VERIFY_TIMEOUT);
	}
	
	return result;
}

DWORD spx_send_informed_disconnect(ipx_socket *socket)
{
	struct spx_packet_header spx_header;

	spx_header.connection_control = SPX_CONNCTRL_ACK;
	spx_header.datastream_type = SPX_END_OF_CONNECTION;
	spx_header.src_connection_id = socket->local_conn;
	spx_header.dst_connection_id = socket->remote_conn;
	spx_header.seq_number = htons(socket->spx_send_seq);
	spx_header.ack_number = htons(socket->spx_recv_seq);
	spx_header.allocation_number = htons(socket->spx_recv_seq);

	DWORD result = ipx_send_packet(
		IPX_TYPE_SPX,
		addr32_in(socket->addr.sa_netnum),
		addr48_in(socket->addr.sa_nodenum),
		socket->addr.sa_socket,
		addr32_in(socket->remote_addr.sa_netnum),
		addr48_in(socket->remote_addr.sa_nodenum),
		socket->remote_addr.sa_socket,
		&spx_header,
		sizeof(spx_header));
	
	return result;
}

DWORD spx_send_informed_disconnect_ack(ipx_socket *socket)
{
	struct spx_packet_header spx_header;

	spx_header.connection_control = 0;
	spx_header.datastream_type = SPX_END_OF_CONNECTION_ACK;
	spx_header.src_connection_id = socket->local_conn;
	spx_header.dst_connection_id = socket->remote_conn;
	spx_header.seq_number = htons(socket->spx_send_seq);
	spx_header.ack_number = htons(socket->spx_recv_seq);
	spx_header.allocation_number = htons(socket->spx_recv_seq);

	DWORD result = ipx_send_packet(
		IPX_TYPE_SPX,
		addr32_in(socket->addr.sa_netnum),
		addr48_in(socket->addr.sa_nodenum),
		socket->addr.sa_socket,
		addr32_in(socket->remote_addr.sa_netnum),
		addr48_in(socket->remote_addr.sa_nodenum),
		socket->remote_addr.sa_socket,
		&spx_header,
		sizeof(spx_header));
	
	if(result == ERROR_SUCCESS)
	{
		socket->spx_verify_time = mclock_add_ms(mclock_now(), SPX_VERIFY_TIMEOUT);
	}
	
	return result;
}

void spx_process_packet(
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const void *data,
	size_t data_size)
{
	
	if(data_size < sizeof(struct spx_packet_header))
	{
		IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
		log_printf(LOG_DEBUG, "Discarding SPX packet from %s with truncated header from", ipx_src_addr);
		
		return;
	}

	const struct spx_packet_header *spx_header = (const struct spx_packet_header*)(data);

	data = spx_header + 1;
	data_size -= sizeof(*spx_header);
	
	if(spx_header->src_connection_id == 0 || spx_header->src_connection_id == 0xFFFF)
	{
		IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
		log_printf(LOG_DEBUG, "Discarding SPX packet from %s with invalid source connection ID (0x%04X)", ipx_src_addr, (unsigned)(ntohs(spx_header->src_connection_id)));
		
		return;
	}
	
	if((spx_header->connection_control & SPX_CONNCTRL_SPX2) != 0)
	{
		IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
		log_printf(LOG_DEBUG, "Discarding SPX packet from %s with SPX2 connection control bit set", ipx_src_addr);
		
		return;
	}
	
	if((spx_header->connection_control & SPX_CONNCTRL_NEG) != 0)
	{
		IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
		log_printf(LOG_DEBUG, "Discarding SPX packet from %s with NEG connection control bit set", ipx_src_addr);
		
		return;
	}
	
	if((spx_header->connection_control & SPX_CONNCTRL_XHD) != 0)
	{
		IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
		log_printf(LOG_DEBUG, "Discarding SPX packet from %s with XHD connection control bit set", ipx_src_addr);
		
		return;
	}
	
	if(spx_header->dst_connection_id == 0xFFFF)
	{
		/* Request for new connection. */
		
		if((spx_header->connection_control & SPX_CONNCTRL_SYS) == 0)
		{
			IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
			log_printf(LOG_DEBUG, "Discarding SPX connection request from %s without SYS connection control bit set", ipx_src_addr);
			
			return;
		}
		
		if((spx_header->connection_control & SPX_CONNCTRL_ACK) == 0)
		{
			IPX_STRING_ADDR(ipx_src_addr, src_net, src_node, src_socket);
			log_printf(LOG_DEBUG, "Discarding SPX connection request from %s without ACK connection control bit set", ipx_src_addr);
			
			return;
		}
		
		spx_process_connection_request_packet(src_net, src_node, src_socket, dest_net, dest_node, dest_socket, spx_header);
	}
	else{
		/* Packet for existing connection. */
		
		SPX_STRING_ADDR(spx_src_addr, src_net, src_node, src_socket, spx_header->src_connection_id);
		SPX_STRING_ADDR(spx_dst_addr, dest_net, dest_node, dest_socket, spx_header->dst_connection_id);
		
		lock_sockets();
		
		ipx_socket *sock = spx_find_socket_by_local(dest_net, dest_node, dest_socket, spx_header->dst_connection_id);
		if(sock == NULL)
		{
			log_printf(LOG_DEBUG, "Discarding SPX packet from %s addressed to unknown socket (%s)", spx_src_addr, spx_dst_addr);
			
			unlock_sockets();
			return;
		}
		
		log_printf(LOG_DEBUG, "Processing SPX packet from %s for %s (socket %u)", spx_src_addr, spx_dst_addr, (unsigned)(sock->fd));
		
		mclock_point_t now = mclock_now();
		
		if(sock->remote_conn == 0xFFFF)
		{
			/* Connection acknowledgement. */
			
			log_printf(LOG_DEBUG, "Received response to SPX connection request");
			
			assert((sock->flags & IPX_CONNECTING));
			
			sock->remote_conn = spx_header->src_connection_id;
			sock->flags &= ~IPX_CONNECTING;
			
			sock->spx_verify_time = mclock_add_ms(now, SPX_VERIFY_TIMEOUT);
			
			if(spx_connect_finish(sock))
			{
				sock->flags |= IPX_CONNECTED;
				
				for(int i = 0; i < SPX_RTT_BACKLOG_COUNT; ++i)
				{
					sock->spx_rtt_history[i] = 0;
				}
				
				if(sock->spx_retransmit_count == 0)
				{
					spx_rtt_insert(sock->spx_rtt_history, mclock_delta(sock->spx_transmit_time, now));
				}
				else{
					assert(sock->spx_retransmit_count > 0);
					spx_rtt_insert(sock->spx_rtt_history, (-1 * sock->spx_retransmit_count));
				}
				
				/* Set the IPX_CONNECT_OK bit which indicates the next WSAAsyncSelect
				 * call with FD_CONNECT set should send a message indicating the
				 * connection succeeded and then clear this bit.
				 * 
				 * This is a hack to make asynchronous connect calls vaguely work as
				 * they should.
				*/
				
				sock->flags |= IPX_CONNECT_OK;
			}
			else{
				sock->flags |= IPX_ABORTED;
			}
		}
		
		if(spx_header->datastream_type == SPX_END_OF_CONNECTION)
		{
			if((sock->flags & (IPX_CLOSING | IPX_CLOSED)) == 0)
			{
				log_printf(LOG_DEBUG, "Received informed disconnect message");

				assert(sock->spx_master_fd != SOCKET_ERROR);

				closesocket(sock->spx_master_fd);
				sock->spx_master_fd = SOCKET_ERROR;

				sock->flags |= IPX_CLOSED;
				sock->flags &= ~IPX_CONNECTED;

				sock->spx_retransmit_time = mclock_never();
				sock->spx_abort_time      = mclock_add_ms(now, SPX_ABORT_TIMEOUT);
				sock->spx_verify_time     = mclock_never();
			}

			if((spx_header->connection_control & SPX_CONNCTRL_ACK) != 0 && (sock->flags & IPX_CLOSED) != 0)
			{
				spx_send_informed_disconnect_ack(sock);
			}
		}
		else if(spx_header->datastream_type == SPX_END_OF_CONNECTION_ACK)
		{
			if((sock->flags & IPX_CLOSING) != 0)
			{
				log_printf(LOG_DEBUG, "Received informed disconnect acknowledgement");

				assert(sock->fd == SOCKET_ERROR);

				DL_DELETE(all_sockets, sock);
				free(sock);
			}
		}
		else if((sock->flags & IPX_CONNECTED) != 0)
		{
			sock->spx_abort_time = mclock_add_ms(now, SPX_ABORT_TIMEOUT);

			if((spx_header->connection_control & SPX_CONNCTRL_SYS) == 0)
			{
				spx_process_data_packet(sock, spx_header, data, data_size);
			}
			else if((spx_header->connection_control & SPX_CONNCTRL_ACK) != 0)
			{
				spx_send_ack(sock);
			}
			
			uint16_t ack = ntohs(spx_header->ack_number);
			if((ack - 1) == sock->spx_send_seq && sock->spx_send_queue->front != NULL)
			{
				if(sock->spx_retransmit_count == 0)
				{
					uint32_t rtt = mclock_delta(sock->spx_transmit_time, now);
					spx_rtt_insert(sock->spx_rtt_history, rtt);
					
					log_printf(LOG_DEBUG,
						"Received ack for SPX data packet sequence %u after %ums",
						(unsigned)(ack - 1), (unsigned)(rtt));
				}
				else{
					assert(sock->spx_retransmit_count > 0);
					spx_rtt_insert(sock->spx_rtt_history, (-1 * sock->spx_retransmit_count));
					
					log_printf(LOG_DEBUG,
						"Received ack for SPX data packet sequence %u after %d retransmissions",
						(unsigned)(ack - 1), sock->spx_retransmit_count);
				}
				
				struct spx_packet_header *header = (struct spx_packet_header*)(sock->spx_send_queue->front->data);
				
				assert(ntohs(header->seq_number) == sock->spx_send_seq);
				
				spx_queue_pop(sock->spx_send_queue);
				
				sock->spx_send_seq += 1;
				
				sock->spx_transmit_time = mclock_now();
				sock->spx_retransmit_count = 0;
				
				spx_send_pump(sock);
			}
		}
		
		unlock_sockets();
	}
}

static void spx_process_connection_request_packet(
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const struct spx_packet_header *spx_header)
{
	lock_sockets();
	
	ipx_socket *accepted_socket = spx_find_socket_by_remote(src_net, src_node, src_socket, spx_header->src_connection_id);
	if(accepted_socket != NULL
		&& addr32_in(accepted_socket->addr.sa_netnum) == dest_net
		&& addr48_in(accepted_socket->addr.sa_nodenum) == dest_node
		&& accepted_socket->addr.sa_socket == dest_socket)
	{
		/* This is a retransmitted connection request for a connection which has been accepted by
		 * the application, retransmit the acknowledgement.
		*/
		
		spx_send_ack(accepted_socket);
		
		unlock_sockets();
		return;
	}
	
	ipx_socket *listener = spx_find_socket_by_local(dest_net, dest_node, dest_socket, 0xFFFF);
	if(listener == NULL || !(listener->flags & IPX_LISTENING))
	{
		IPX_STRING_ADDR(ipx_dst_addr, dest_net, dest_node, dest_socket);
		log_printf(LOG_DEBUG, "SPX connection request for %s doesn't match any listener in this process, ignoring", ipx_dst_addr);
		
		unlock_sockets();
		return;
	}
	
	{
		SPX_STRING_ADDR(spx_src_addr, src_net, src_node, src_socket, spx_header->src_connection_id);
		IPX_STRING_ADDR(ipx_dst_addr, dest_net, dest_node, dest_socket);
		
		log_printf(LOG_DEBUG, "Processing SPX connection request from %s for %s", spx_src_addr, ipx_dst_addr);
	}
	
	assert(listener->spx_current_backlog <= listener->spx_max_backlog);
	
	for(size_t i = 0; i < listener->spx_current_backlog; ++i)
	{
		struct spx_pending_connection *pc = &(listener->spx_connection_queue[i]);
		
		if(pc->remote_net == src_net
			&& pc->remote_node == src_node
			&& pc->remote_socket == src_socket
			&& pc->remote_connection_id == spx_header->src_connection_id)
		{
			/* This is a retransmitted connection request for a connection which has not yet been
			 * accepted by the application, ignore it.
			*/
			
			log_printf(LOG_DEBUG, "Connection request is a retransmission, ignoring");
			
			unlock_sockets();
			return;
		}
	}
	
	if(listener->spx_current_backlog == listener->spx_max_backlog)
	{
		log_printf(LOG_DEBUG, "Received SPX connection request, but listening socket backlog is full, ignoring");
		
		unlock_sockets();
		return;
	}
	
	struct spx_pending_connection *pc = &(listener->spx_connection_queue[listener->spx_current_backlog]);
	
	pc->remote_net = src_net;
	pc->remote_node = src_node;
	pc->remote_socket = src_socket;
	pc->remote_connection_id = spx_header->src_connection_id;
	
	pc->master_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(pc->master_fd == SOCKET_ERROR)
	{
		log_printf(LOG_ERROR, "Error creating TCP socket for incoming SPX connection: %s", w32_error(WSAGetLastError()));
		
		unlock_sockets();
		return;
	}

	/* We explicitly bind to a random port on the loopback address before attempting to
	 * connect to the listening socket to ensure the address is known before we yield the
	 * lock and allow accept() to process the connection.
	 *
	 * Relying on the implicit bind and then calling getsockname() after connect() returns
	 * results in a race where master_local_addr.sin_addr may be initialised to INADDR_ANY
	 * OR INADDR_LOOPBACK.
	*/

	pc->master_local_addr.sin_family = AF_INET;
	pc->master_local_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	pc->master_local_addr.sin_port = htons(0);

	int master_addrlen = sizeof(pc->master_local_addr);

	if(r_bind(pc->master_fd, (struct sockaddr*)(&pc->master_local_addr), sizeof(pc->master_local_addr)) == SOCKET_ERROR
		|| r_getsockname(pc->master_fd, (struct sockaddr*)(&pc->master_local_addr), &master_addrlen) == SOCKET_ERROR)
	{
		log_printf(LOG_ERROR, "Error setting local address for incoming SPX connection: %s", w32_error(WSAGetLastError()));
		
		closesocket(pc->master_fd);
		unlock_sockets();
		return;
	}
	
	struct sockaddr_in listener_addr;
	listener_addr.sin_family = AF_INET;
	listener_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	listener_addr.sin_port = listener->port;
	
	unsigned long argp = 1;
	ioctlsocket(pc->master_fd, FIONBIO, &argp);
	
	int connect_result = r_connect(pc->master_fd, (struct sockaddr*)(&listener_addr), sizeof(listener_addr));
	if(connect_result == SOCKET_ERROR)
	{
		DWORD connect_error = WSAGetLastError();
		
		if(connect_error != WSAEWOULDBLOCK)
		{
			log_printf(LOG_ERROR, "Error connecting loopback TCP socket for incoming SPX connection: %s", w32_error(connect_error));
			
			closesocket(pc->master_fd);
			unlock_sockets();
			return;
		}
	}
	
	++(listener->spx_current_backlog);
	
	log_printf(LOG_DEBUG, "Queued incoming SPX connection");
	
	unlock_sockets();
}

static void spx_process_data_packet(
	ipx_socket *sock,
	const struct spx_packet_header *spx_header,
	const void *data,
	size_t data_size)
{
	uint16_t seq = ntohs(spx_header->seq_number);
	
	if(seq == sock->spx_recv_seq)
	{
		log_printf(LOG_DEBUG, "Received SPX data packet sequence %u", (unsigned)(seq));
		
		if(spx_queue_push(sock->spx_recv_queue, data, data_size, ((spx_header->connection_control & SPX_CONNCTRL_EOM) != 0)))
		{
			sock->spx_recv_seq = seq + 1;
			spx_send_ack(sock);
			
			spx_recv_pump(sock);
		}
	}
	else if((seq - 1) == sock->spx_recv_seq)
	{
		log_printf(LOG_DEBUG, "Received retransmitted SPX data packet sequence %u", (unsigned)(seq));
		spx_send_ack(sock);
	}
	else{
		log_printf(LOG_DEBUG, "Received unexpected SPX data packet sequence %u", (unsigned)(seq));
	}
}

static void spx_send_pump(ipx_socket *socket)
{
	if(socket->spx_send_queue->front != NULL)
	{
		struct spx_packet_header *header = (struct spx_packet_header*)(socket->spx_send_queue->front->data);
		
		log_printf(LOG_DEBUG, "Transmitting SPX data packet sequence number %u from socket %u", (unsigned)(ntohs(header->seq_number)), (unsigned)(socket->fd));
		
		assert(ntohs(header->seq_number) == socket->spx_send_seq);
		
		/* Patch the ack number in the prepared header to be up-to-date with the current stream status. */
		header->ack_number = htons(socket->spx_recv_seq);
		header->allocation_number = htons(socket->spx_recv_seq);
		
		DWORD result = ipx_send_packet(
			IPX_TYPE_SPX,
			
			addr32_in(socket->addr.sa_netnum),
			addr48_in(socket->addr.sa_nodenum),
			socket->addr.sa_socket,
			
			addr32_in(socket->remote_addr.sa_netnum),
			addr48_in(socket->remote_addr.sa_nodenum),
			socket->remote_addr.sa_socket,
			
			header,
			socket->spx_send_queue->front->data_size);
		
		uint32_t retransmit_delay = spx_compute_retransmit_time(socket->spx_rtt_history, socket->spx_retransmit_count);
		socket->spx_retransmit_time = mclock_add_ms(mclock_now(), retransmit_delay);
		
		if(result == ERROR_SUCCESS)
		{
			socket->spx_verify_time = mclock_add_ms(mclock_now(), SPX_VERIFY_TIMEOUT);
		}
	}
}

DWORD spx_queue_message(ipx_socket *socket, const void *data, size_t size)
{
	struct spx_queue fragments;
	fragments.front = fragments.back = NULL;
	fragments.num_complete_messages = 0;
	
	uint16_t seq = socket->spx_send_seq;
	bool queue_was_empty = true;
	
	if(socket->spx_send_queue->back != NULL)
	{
		const struct spx_packet_header *header = (const struct spx_packet_header*)(socket->spx_send_queue->back->data);
		seq = ntohs(header->seq_number) + 1;
		
		queue_was_empty = false;
	}
	
	for(size_t pos = 0; pos < size;)
	{
		size_t fragment_size = min((size - pos), SPX_FRAGMENT_MAX_DATA_SIZE);
		
		struct spx_packet_header header;
		header.connection_control = SPX_CONNCTRL_ACK;
		header.datastream_type = 0;
		header.src_connection_id = socket->local_conn;
		header.dst_connection_id = socket->remote_conn;
		header.seq_number = ntohs(seq);
		
		++seq;
		
		bool is_last_fragment = (pos + fragment_size) == size;
		if(is_last_fragment)
		{
			header.connection_control |= SPX_CONNCTRL_EOM;
		}
		
		if(!spx_queue_push_hdr(&fragments, &header, ((const char*)(data) + pos), fragment_size, is_last_fragment))
		{
			while(fragments.front != NULL)
			{
				spx_queue_pop(&fragments);
			}
			
			return ERROR_OUTOFMEMORY;
		}
		
		pos += fragment_size;
	}
	
	spx_queue_merge(socket->spx_send_queue, &fragments);
	
	if(queue_was_empty)
	{
		socket->spx_transmit_time = mclock_now();
		socket->spx_retransmit_count = 0;
		
		spx_send_pump(socket);
	}
	
	return ERROR_SUCCESS;
}

static void spx_recv_pump(ipx_socket *socket)
{
	if(socket->spx_recv_queue->num_complete_messages == 0)
	{
		return;
	}
	
	/* Accumulate all outstanding buffers in the lead message to send them at once. We should only
	 * be called from a thread that holds the sockets lock, so the fragments buffer is statically
	 * allocated to avoid a large stack allocation or potential heap allocation failure.
	*/
	
	#define MAX_SPX_FRAGMENTS 128
	static WSABUF fragments[MAX_SPX_FRAGMENTS];
	
	size_t n_fragments = 0;
	
	for(struct spx_queue_element *p = socket->spx_recv_queue->front; p != NULL; p = p->next)
	{
		if(n_fragments < MAX_SPX_FRAGMENTS)
		{
			fragments[n_fragments].buf = (char*)(p->data) + p->data_pos;
			fragments[n_fragments].len = p->data_size - p->data_pos;
			
			++n_fragments;
		}
		else{
			log_printf(LOG_WARNING, "Encountered message with more than %d fragments! This will not be delivered to the application in one piece.", (int)(MAX_SPX_FRAGMENTS));
			break;
		}
		
		if(p->end_of_message)
		{
			break;
		}
	}
	
	DWORD num_sent;
	if(WSASend(socket->spx_master_fd, fragments, n_fragments, &num_sent, 0, NULL, NULL) != 0)
	{
		log_printf(LOG_ERROR, "Error delivering message to local SPX socket: %s", w32_error(WSAGetLastError()));
		return;
	}
	
	socket->spx_recv_inflight += num_sent;
	
	for(struct spx_queue_element *p = socket->spx_recv_queue->front; num_sent > 0;)
	{
		assert(p != NULL);
		
		size_t x = min(num_sent, (p->data_size - p->data_pos));
		
		p->data_pos += x;
		num_sent -= x;
		
		if(p->end_of_message)
		{
			assert(num_sent == 0);
			break;
		}
		
		if(p->data_pos == p->data_size)
		{
			p = p->next;
			spx_queue_pop(socket->spx_recv_queue);
		}
		else{
			p = p->next;
		}
	}
}

static void spx_rtt_insert(int spx_rtt_history[SPX_RTT_BACKLOG_COUNT], int value)
{
	memmove((spx_rtt_history + 1), spx_rtt_history, (sizeof(*spx_rtt_history) * (SPX_RTT_BACKLOG_COUNT - 1)));
	spx_rtt_history[0] = value;
}

void spx_recv_advance(ipx_socket *socket, size_t received_bytes)
{
	assert(received_bytes <= socket->spx_recv_inflight);
	socket->spx_recv_inflight -= received_bytes;
	
	assert(socket->spx_recv_queue->front != NULL);
	
	spx_recv_pump(socket);
	
	if(socket->spx_recv_inflight == 0)
	{
		assert(socket->spx_recv_queue->front != NULL);
		
		if(socket->spx_recv_queue->front->data_pos == socket->spx_recv_queue->front->data_size
			&& socket->spx_recv_queue->front->end_of_message)
		{
			spx_queue_pop(socket->spx_recv_queue);
		}
	}
	
	spx_recv_pump(socket);
}

void spx_retransmit_lost(void)
{
	lock_sockets();

	mclock_point_t now = mclock_now();

	ipx_socket *sock, *tmp;
	DL_FOREACH_SAFE(all_sockets, sock, tmp)
	{
		if((sock->flags & IPX_IS_SPX) != 0)
		{
			if((sock->flags & (IPX_CLOSING | IPX_CLOSED)) != 0)
			{
				if(mclock_ms_until(sock->spx_abort_time, now) == 0)
				{
					if(sock->fd == SOCKET_ERROR)
					{
						/* Socket has been closed and inner structures freed by closesocket(), all
						 * we have left to do is remove it from the all_sockets list and free it.
						*/

						DL_DELETE(all_sockets, sock);
						free(sock);
					}
				}
				else if((sock->flags & IPX_CLOSING) != 0 && mclock_ms_until(sock->spx_retransmit_time, now) == 0)
				{
					/* Retransmit informed disconnect message to peer of closed connection. */

					DWORD err = spx_send_informed_disconnect(sock);
					if(err == ERROR_SUCCESS)
					{
						sock->spx_retransmit_count += 1;
						
						uint32_t retransmit_delay = spx_compute_retransmit_time(sock->spx_rtt_history, sock->spx_retransmit_count);
						sock->spx_retransmit_time = mclock_add_ms(now, retransmit_delay);
					}
				}
			}
			else if((sock->flags & (IPX_CONNECTED | IPX_CONNECTING)) != 0 && mclock_ms_until(sock->spx_abort_time, now) == 0)
			{
				if((sock->flags & IPX_CONNECTING) != 0)
				{
					sock->flags &= ~IPX_CONNECTING;
					sock->flags |= IPX_ABORTED;
					
					spx_connect_finish(sock);
				}
				else{
					assert((sock->flags & IPX_CONNECTED) != 0);
					
					sock->flags &= ~IPX_CONNECTED;
					sock->flags |= IPX_ABORTED;
					
					assert(sock->spx_master_fd != SOCKET_ERROR);
					
					closesocket(sock->spx_master_fd);
					sock->spx_master_fd = SOCKET_ERROR;
				}
			}
			else if(((sock->flags & IPX_CONNECTING) || sock->spx_send_queue->front != NULL)
				&& mclock_ms_until(sock->spx_retransmit_time, now) == 0)
			{
				if((sock->flags & IPX_CONNECTING))
				{
					spx_send_connection_request(sock);
					sock->spx_retransmit_time = mclock_add_ms(now, main_config.spx_retransmit_delay > 0 ? main_config.spx_retransmit_delay : SPX_CONNECTION_RETRANSMIT_TIME);
				}
				else{
					sock->spx_retransmit_count += 1;
					spx_send_pump(sock);
				}
			}
			else if((sock->flags & IPX_CONNECTED) && mclock_ms_until(sock->spx_verify_time, now) == 0)
			{
				spx_send_watchdog_request(sock);
			}
		}
	}

	unlock_sockets();
}

static BOOL spx_connect_finish(ipx_socket *sock)
{
	if(sock->spx_connect_event != NULL)
	{
		SetEvent(sock->spx_connect_event);
	}
	
	SOCKET listener_fd = r_socket(AF_INET, SOCK_STREAM, 0);
	if(listener_fd == SOCKET_ERROR)
	{
		log_printf(LOG_ERROR, "Error creating temporary listener for SPX connection completion: %s", w32_error(WSAGetLastError()));
		return FALSE;
	}
	
	struct sockaddr_in listener_addr;
	listener_addr.sin_family = AF_INET;
	listener_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	listener_addr.sin_port = 0;

	int addrlen = sizeof(listener_addr);
	
	if(r_bind(listener_fd, (struct sockaddr*)(&listener_addr), sizeof(listener_addr)) == SOCKET_ERROR
		|| r_getsockname(listener_fd, (struct sockaddr*)(&listener_addr), &addrlen) == SOCKET_ERROR
		|| r_listen(listener_fd, 8) == SOCKET_ERROR)
	{
		log_printf(LOG_ERROR, "Error setting up temporary listener for SPX connection completion: %s", w32_error(WSAGetLastError()));
		
		closesocket(listener_fd);
		return FALSE;
	}

	if(r_connect(sock->fd, (struct sockaddr*)(&listener_addr), sizeof(listener_addr)) == SOCKET_ERROR)
	{
		DWORD connect_err = WSAGetLastError();

		if(connect_err != WSAEWOULDBLOCK)
		{
			log_printf(LOG_ERROR, "Error connecting to temporary listener: %s", w32_error(connect_err));

			closesocket(listener_fd);
			return FALSE;
		}
	}

	do {
		struct sockaddr_in client_addr;
		addrlen = sizeof(client_addr);

		sock->spx_master_fd = r_accept(listener_fd, (struct sockaddr*)(&client_addr), &addrlen);
		if(sock->spx_master_fd == SOCKET_ERROR)
		{
			log_printf(LOG_ERROR, "Error accepting connection on temporary listener: %s", w32_error(WSAGetLastError()));
			
			closesocket(listener_fd);
			return FALSE;
		}

		if(client_addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || client_addr.sin_port != sock->port)
		{
			log_printf(LOG_DEBUG, "Discarding stray connection to temporary listener from %s:%u", inet_ntoa(client_addr.sin_addr), (unsigned)(ntohs(client_addr.sin_port)));
			
			closesocket(sock->spx_master_fd);
			sock->spx_master_fd = SOCKET_ERROR;
		}
	} while(sock->spx_master_fd == SOCKET_ERROR);
	
	{
		unsigned long argp = 1;
		ioctlsocket(sock->spx_master_fd, FIONBIO, &argp);
	}
	
	closesocket(listener_fd);
	
	return TRUE;
}

struct spx_queue *spx_queue_alloc(void)
{
	struct spx_queue *queue = malloc(sizeof(struct spx_queue));
	if(queue == NULL)
	{
		return NULL;
	}
	
	queue->front = queue->back = NULL;
	queue->num_complete_messages = 0;
	
	return queue;
}

void spx_queue_free(struct spx_queue *queue)
{
	while(queue->front != NULL)
	{
		spx_queue_pop(queue);
	}
	
	free(queue);
}

static bool spx_queue_push(struct spx_queue *queue, const void *data, size_t size, bool end_of_message)
{
	struct spx_queue_element *e = malloc(sizeof(struct spx_queue_element) + size);
	if(e == NULL)
	{
		return false;
	}
	
	e->data = e + 1;
	e->data_size = size;
	e->data_pos = 0;
	e->end_of_message = end_of_message;
	e->next = NULL;
	
	memcpy(e->data, data, size);
	
	if(queue->front == NULL)
	{
		queue->front = queue->back = e;
	}
	else{
		queue->back->next = e;
		queue->back = e;
	}
	
	if(end_of_message)
	{
		++(queue->num_complete_messages);
	}
	
	return true;
}

static bool spx_queue_push_hdr(struct spx_queue *queue, const struct spx_packet_header *header, const void *data, size_t size, bool end_of_message)
{
	struct spx_queue_element *e = malloc(sizeof(struct spx_queue_element) + sizeof(*header) + size);
	if(e == NULL)
	{
		return false;
	}
	
	e->data = e + 1;
	e->data_size = sizeof(*header) + size;
	e->data_pos = 0;
	e->end_of_message = end_of_message;
	e->next = NULL;
	
	memcpy(e->data, header, sizeof(*header));
	memcpy(((char*)(e->data) + sizeof(*header)), data, size);
	
	if(queue->front == NULL)
	{
		queue->front = queue->back = e;
	}
	else{
		queue->back->next = e;
		queue->back = e;
	}
	
	if(end_of_message)
	{
		++(queue->num_complete_messages);
	}
	
	return true;
}

static void spx_queue_merge(struct spx_queue *dst_queue, struct spx_queue *src_queue)
{
	if(dst_queue->front == NULL)
	{
		*dst_queue = *src_queue;
	}
	else{
		dst_queue->back->next = src_queue->front;
		dst_queue->back = src_queue->back;
		
		dst_queue->num_complete_messages += src_queue->num_complete_messages;
	}
	
	src_queue->back = src_queue->front = NULL;
	src_queue->num_complete_messages = 0;
}

static void spx_queue_pop(struct spx_queue *queue)
{
	assert(queue->front != NULL);
	
	struct spx_queue_element *next = queue->front->next;
	
	if(queue->front->end_of_message)
	{
		--(queue->num_complete_messages);
	}
	
	free(queue->front);
	
	if(queue->back == queue->front)
	{
		queue->front = queue->back = NULL;
	}
	else{
		queue->front = next;
	}
}

struct spx_pending_connection *spx_pending_alloc(int backlog)
{
	// TODO: overflow check
	return malloc(sizeof(struct spx_pending_connection) * backlog);
}

void spx_pending_free(struct spx_pending_connection *queue, size_t count)
{
	for(size_t i = 0; i < count; ++i)
	{
		closesocket(queue[i].master_fd);
	}
	
	free(queue);
}
