/* ipxwrapper - Library header
 * Copyright (C) 2008-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wsipx.h>
#include <stdint.h>
#include <stdio.h>
#include <uthash.h>

#include "config.h"
#include "funcprof.h"
#include "mclock.h"
#include "router.h"

/* The standard Windows driver (in XP) only allows 1467 bytes anyway */
#define MAX_DATA_SIZE 8192
#define MAX_PKT_SIZE 8219

#define IPX_CONNECT_TIMEOUT 6
#define IPX_CONNECT_TRIES   3

/* Maximum number of milliseconds to block waiting for IPX networking to be ready.
 *
 * This blocks functions which usually don't block (e.g. bind()) so that they don't fail right as
 * the process is starting up because we are still connecting to a DOSBox IPX server.
*/
#define IPX_READY_TIMEOUT 3000

#define IPX_FILTER	(int)(1<<0)
#define IPX_BOUND	(int)(1<<1)
#define IPX_BROADCAST	(int)(1<<2)
#define IPX_SEND	(int)(1<<3)
#define IPX_RECV	(int)(1<<4)
#define IPX_REUSE	(int)(1<<6)
#define IPX_CONNECTED	(int)(1<<7)
#define IPX_RECV_BCAST	(int)(1<<8)
#define IPX_EXT_ADDR	(int)(1<<9)
#define IPX_IS_SPX	(int)(1<<10)
#define IPX_IS_SPXII	(int)(1<<11)
#define IPX_LISTENING	(int)(1<<12)
#define IPX_CONNECT_OK	(int)(1<<13)
#define IPX_CONNECTING	(int)(1<<14)
#define IPX_CLOSING (int)(1<<15) /**< SPX socket closed locally, awaiting acknowledgement of disconnection from peer, or spx_abort_time */
#define IPX_CLOSED (int)(1<<16) /**< SPX socket closed by peer, awaiting acknowledgement by application and spx_abort_time */
#define IPX_ABORTED (int)(1<<17) /**< SPX socket closed by timeout */
#define IPX_NONBLOCK (int)(1<<18) /**< Socket is in non-blocking mode. */

#define SPX_RTT_BACKLOG_COUNT 8 /**< Number of most recent packet RTTs to record. */

typedef struct ipx_socket ipx_socket;
typedef struct ipx_packet ipx_packet;

#define RECV_QUEUE_MAX_PACKETS 32

#define IPX_RECV_QUEUE_FREE -1
#define IPX_RECV_QUEUE_LOCKED -2

/* Any AF_IPX IPX socket has an associated recv_queue.
 *
 * When a recv_pump() operation is running, the sockets lock has to be released
 * in case the recv() blocks, which means the socket could be closed before it
 * regains the lock.
 *
 * An ipx_recv_queue isn't destroyed until the refcount reaches zero. The
 * ipx_socket holds one reference and each in-progress recv_pump() also holds a
 * reference while the sockets lock isn't held.
 *
 * Access to the refcount is protected by refcount_lock.
 *
 * data[] holds an array of buffers for each queued packet, the status and size
 * of which is indicated by the sizes[] array.
 *
 * If sizes[x] is IPX_RECV_QUEUE_FREE, the buffer is available to be claimed by
 * a recv_pump() operation, which then sets it to IPX_RECV_QUEUE_LOCKED until
 * it completes, which prevents a recv_pump() in another thread from trying to
 * use the same receive buffer. Once a packet is read in, sizes[x] is set to
 * the size of the packet and x is added to the end of the ready[] array.
 *
 * When a read is requested, the packet will be read from the data[] index
 * stored in ready[0], and unless MSG_PEEK was used, that slot will then be
 * released (sizes[x] set to IPX_RECV_QUEUE_FREE) and any subsequent slots in
 * read[] will be advanced for the next read to pick up from ready[0].
 *
 * Access to the ready, n_ready and sizes members is only permitted when a
 * thread holds the main sockets lock.
*/

struct ipx_recv_queue
{
	CRITICAL_SECTION refcount_lock;
	int refcount;
	
	int ready[RECV_QUEUE_MAX_PACKETS];
	int n_ready;
	
	unsigned char data[RECV_QUEUE_MAX_PACKETS][MAX_PKT_SIZE];
	int sizes[RECV_QUEUE_MAX_PACKETS];
};

typedef struct ipx_recv_queue ipx_recv_queue;

struct spx_queue;
struct spx_pending_connection;

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
	HANDLE sock_mut;
	
	/* Address used with connect call, only set when IPX_CONNECTED is */
	struct sockaddr_ipx remote_addr;

	uint16_t local_conn;   /**< Local SPX connection ID (only valid when IPX_IS_SPX and IPX_LISTENING, IPX_CONNECTING or IPX_CONNECTED is set). */
	uint16_t remote_conn;  /**< Peer SPX connection ID (only valid when IPX_IS_SPX and IPX_CONNECTED is set). */
	
	struct ipx_recv_queue *recv_queue;
	
	SOCKET spx_master_fd;
	HANDLE spx_connect_event;
	
	struct spx_pending_connection *spx_connection_queue;
	size_t spx_max_backlog, spx_current_backlog;
	
	struct spx_queue *spx_recv_queue;
	uint16_t spx_recv_seq; /**< Sequence number of next expected SPX data packet (host byte order). */
	size_t spx_recv_inflight; /**< Number of received bytes written to the socket and awaiting confirmation from recv_packet(). */
	
	struct spx_queue *spx_send_queue;
	uint16_t spx_send_seq; /**< Sequence number of next SPX data packet to send (host byte order). */

	mclock_point_t spx_retransmit_time; /**< Time when last packet needs to be retransmitted. */
	
	mclock_point_t spx_verify_time; /**< Time when watchdog request will next be transmitted. */
	mclock_point_t spx_abort_time;  /**< Time when connection will be aborted due to a (assumed) dead peer. */
	
	/**
	 * @brief Tracking of RTT from acknowledged packets on this SPX connection.
	 *
	 * This array tracks the RTT of recently transmitted SPX packets. Each entry is one of the
	 * following values:
	 *
	 * Zero     - No data (early in connection lifetime).
	 * Positive - Round trip time in milliseconds of a packet.
	 * Negative - Retransmission count of a packet.
	*/
	int spx_rtt_history[SPX_RTT_BACKLOG_COUNT];
	
	mclock_point_t spx_transmit_time; /**< Time of first transmission of current in-flight SPX packet. */
	int spx_retransmit_count; /**< Number of retransmissions of current in-flight SPX packet. */
	
	UT_hash_handle hh;

	ipx_socket *prev;
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

#define IPX_MAGIC_SPXLOOKUP 1
#define IPX_MAGIC_COALESCED 2

extern ipx_socket *socket_by_fd;
extern ipx_socket *all_sockets;

extern main_config_t main_config;

extern struct FuncStats ipxwrapper_fstats[];

enum {
	#define FPROF_DECL(func) IPXWRAPPER_FSTATS_ ## func,
	#include "ipxwrapper_prof_defs.h"
	#undef FPROF_DECL
};

extern unsigned int send_packets, send_bytes;  /* Sent from emulated socket */
extern unsigned int recv_packets, recv_bytes;  /* Forwarded to emulated socket */

extern unsigned int send_packets_udp, send_bytes_udp;  /* Sent over UDP transport */
extern unsigned int recv_packets_udp, recv_bytes_udp;  /* Received over UDP transport */

ipx_socket *get_socket(SOCKET sockfd);
ipx_socket *get_socket_wait_for_ready(SOCKET sockfd, int timeout_ms);
void lock_sockets(void);
void unlock_sockets(void);
uint64_t get_ticks(void);
uint64_t get_uticks(void);

void add_self_to_firewall(void);

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
int PASCAL r_listen(SOCKET s, int backlog);
SOCKET PASCAL r_accept(SOCKET s, struct sockaddr *addr, int *addrlen);
int PASCAL r_WSAAsyncSelect(SOCKET s, HWND hWnd, unsigned int wMsg, long lEvent);
int WSAAPI r_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const PTIMEVAL timeout);

#endif /* !IPXWRAPPER_H */
