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

#ifndef IPXWRAPPER_SPX_H
#define IPXWRAPPER_SPX_H

#include <stdint.h>

#include "ipxwrapper.h"

static const uint8_t IPX_TYPE_SPX = 0x05;

static const uint8_t SPX_CONNCTRL_XHD  = 0x01;  /**< Reserved by SPX II for extended header */
static const uint8_t SPX_CONNCTRL_RES1 = 0x02;  /**< Undefined, must be 0 */
static const uint8_t SPX_CONNCTRL_NEG  = 0x04;  /**< SPX II negotiate size request/response, must be 0 for SPX */
static const uint8_t SPX_CONNCTRL_SPX2 = 0x08;  /**< SPX II type packet, must be 0 for SPX */
static const uint8_t SPX_CONNCTRL_EOM  = 0x10;  /**< Set by an SPX client to indicate end of message. */
static const uint8_t SPX_CONNCTRL_ATN  = 0x20;  /**< Reserved for attention indication (Not supported by SPX) */
static const uint8_t SPX_CONNCTRL_ACK  = 0x40;  /**< Set to request the receiving partner acknowledge that this packet has been received. Acknowledgement requests and responses are handled by SPX. */
static const uint8_t SPX_CONNCTRL_SYS  = 0x80;  /**< Set to indicate a packet is a system packet. System packets are internal SPX packets, are not delivered to the application, and do not consume sequence numbers. */

static const uint8_t SPX_END_OF_CONNECTION = 0xFE;
static const uint8_t SPX_END_OF_CONNECTION_ACK = 0xFF;

static const uint32_t SPX_VERIFY_TIMEOUT = 3000;  /**< Number of milliseconds of sending no packets before SPX will send a watchdog request packet. */
static const uint32_t SPX_ABORT_TIMEOUT  = 30000; /**< Number of milliseconds of receiving no packets before SPX will drop a connection. */

/* SPX RTT measurement and retransmit time algorithm.
 *
 * The SPX specification defines a very simple way to calculate round-trip time:
 *
 * > SPX does not attempt to track round trip time closely. It calculates round trip time RTT by
 * > using the "time to net" value returned from the nearest router. This value is doubled and then
 * > 3/4 of a second fudge factor is added. Each time a retry is necessary, the current RTT is
 * > increased by 50% and bounded by MAX_ROUND_TRIP_TIMEOUT. RTT is tracked and modified on a per
 * > session basis.
 *
 * IPXWrapper doesn't support IPX routing... and I doubt many people are going to be setting up IPX
 * WANs with routers, so this method wouldn't make much sense, so we use a different method loosely
 * inspired by how TCP works:
 *
 * - For the past SPX_RTT_BACKLOG_COUNT packets transmitted which required an acknowledgement from
 *   the reciving side, if we get the ack without having to retransmit the packet, we record the
 *   round-trip time in milliseconds, if we had to retransmit even once, we record the number of
 *   retransmissions.
 *
 * - When sending a packet, we calculate the retransmission time as follows:
 *
 *   1) Let t equal the average (mean) RTT of all recent packets which were acknowledged without
 *      any retransmissions, multiplied by 1.5 to allow for small deviations in latency. If there
 *      are no such recorded packets, r is SPX_RETRANSMIT_DEFAULT_TIME.
 *
 *   2) Let r equal the average (mean) number of retransmissions of all recent packets which were
 *      acknowledged after one or more retransmissions, zero if there are no such packets.
 *
 *   3) Let c equal the retransmission count of the current packet (zero on first transmission).
 *
 *   4) Let d equal the largest of c or r.
 *
 *   5) Let u equal t multiplied by the d'th power of 2 (i.e. double it for every anticipated
 *      retransmission from the initial t.
 *
 *   6) Clamp u between SPX_RETRANSMIT_MIN_TIME and SPX_RETRANSMIT_TIME_MAX to avoid unnecessary
 *      retransmissions on low-latency links or stratospheric retransmissions when the connection
 *      has died like TCP (typically) has, since SPX dictates a 30 second timeout on inactivity.
 *
 *   7) u is the interval after which to send a retransmission.
*/

static const uint32_t SPX_RETRANSMIT_MIN_TIME     = 80;    /**< Minimum computed SPX retransmit time (milliseconds). */
static const uint32_t SPX_RETRANSMIT_MAX_TIME     = 8000;  /**< Maximum computed SPX retransmit time (milliseconds). */
static const uint32_t SPX_RETRANSMIT_DEFAULT_TIME = 1000;  /**< Default SPX retransmit time (milliseconds). */

static const uint32_t SPX_CONNECTION_RETRANSMIT_TIME = 1000;  /**< Time before retransmitting an SPX connection request (milliseconds). */

struct spx_packet_header
{
	uint8_t connection_control;
	uint8_t datastream_type;
	uint16_t src_connection_id;
	uint16_t dst_connection_id;
	uint16_t seq_number;
	uint16_t ack_number;
	uint16_t allocation_number;
}  __attribute__((__packed__));

struct spx_pending_connection
{
	addr32_t remote_net;
	addr48_t remote_node;
	uint16_t remote_socket;
	uint16_t remote_connection_id;
	
	SOCKET master_fd;
	struct sockaddr_in master_local_addr;
};

/**
 * @brief Allocate an SPX connection ID.
 *
 * Generates and returns a unique SPX connection ID in network byte order. The
 * caller is responsible for holding the sockets lock prior to calling this
 * function and assigning the return value to a socket before releasing it or
 * calling the function again to ensure the returned ID *REMAINS* unique.
*/
uint16_t spx_allocate_connection_id(void);

/**
 * @brief Send an SPX connection request.
*/
DWORD spx_send_connection_request(ipx_socket *socket);

DWORD spx_send_watchdog_request(ipx_socket *socket);

DWORD spx_send_informed_disconnect(ipx_socket *socket);

/**
 * @brief Process an SPX packet received from the network.
 *
 * @param src_net      Source network number from the IPX header (network byte order).
 * @param src_node     Source node number from the IPX header (network byte order).
 * @param src_socket   Source socket number from the IPX header (network byte order).
 * @param dest_net     Destination network number from the IPX header (network byte order).
 * @param dest_node    Destination node number from the IPX header (network byte order).
 * @param dest_socket  Destination socket number from the IPX header (network byte order).
 * @param data         IPX packet payload (including SPX header).
 * @param data_size    Length of IPX packet payload (including SPX header).
*/
void spx_process_packet(
	addr32_t src_net,
	addr48_t src_node,
	uint16_t src_socket,
	addr32_t dest_net,
	addr48_t dest_node,
	uint16_t dest_socket,
	const void *data,
	size_t data_size);

/**
 * @brief Queue a message for transmission on a connected "SPX" socket.
 *
 * @param socket  Source socket.
 * @param data    SPX packet payload.
 * @param size    SPX packet payload size.
 *
 * @return ERROR_SUCCESS on success, a Win32 error code on failure.
*/
DWORD spx_queue_message(ipx_socket *socket, const void *data, size_t size);

/**
 * @brief Acknowledge receipt of some data from an "SPX" socket by the application.
 *
 * @param socket          Socket data was received from.
 * @param received_bytes  Number of bytes received.
 *
 * This function is called by the WinSock emulation functions when data has been read from the TCP
 * socket (masquerading as an SPX socket) into an application-provided buffer so that the SPX code
 * can track when a full message has been received by the application before writing any subsequent
 * messages to it.
*/
void spx_recv_advance(ipx_socket *socket, size_t received_bytes);

void spx_retransmit_lost(void);

/**
 * @brief Calculate retransmission time for an SPX packet (in milliseconds).
 *
 * @param rtt_history       The spx_rtt_history from the connection's ipx_socket.
 * @param retransmit_count  The retransmission number of THIS packet (zero for initial packet).
 *
 * See the "SPX RTT measurement and retransmit time algorithm" comment earlier in this file for a
 * full description for our packet retransmission timing algorithm.
*/
uint32_t spx_compute_retransmit_time(const int rtt_history[SPX_RTT_BACKLOG_COUNT], int retransmit_count);

/**
 * @brief Allocate and initialise an empty spx_queue structure.
*/
struct spx_queue *spx_queue_alloc(void);

/**
 * @brief Free an spx_queue structure and all queued packets.
*/
void spx_queue_free(struct spx_queue *queue);

struct spx_pending_connection *spx_pending_alloc(int backlog);
void spx_pending_free(struct spx_pending_connection *queue, size_t count);

#endif /* !IPXWRAPPER_SPX_H */
