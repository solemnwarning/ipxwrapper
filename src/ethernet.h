/* IPXWrapper - Ethernet frame handling
 * Copyright (C) 2017 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef IPXWRAPPER_ETHERNET_H
#define IPXWRAPPER_ETHERNET_H

#include <stdbool.h>
#include <stdint.h>

#include "addr.h"

/* An IPX packet in its normal form on the wire */

#define NOVELL_IPX_PACKET_MAX_PAYLOAD ((uint16_t)(0xFFFF) - sizeof(novell_ipx_packet))

typedef struct novell_ipx_packet novell_ipx_packet;

struct novell_ipx_packet {
	uint16_t checksum;
	uint16_t length;
	uint8_t  hops;
	uint8_t  type;
	
	unsigned char dest_net[4];
	unsigned char dest_node[6];
	uint16_t dest_socket;
	
	unsigned char src_net[4];
	unsigned char src_node[6];
	uint16_t src_socket;
	
	unsigned char data[0];
} __attribute__((__packed__));

/* This file declares three types of functions:
 * 
 * XXX_frame_size
 * 
 *   Returns the size of a whole frame and IPX packet with the given number of
 *   bytes of payload.
 *   
 *   Returns zero if the payload is too large to fit in a frame of this format.
 * 
 * XXX_frame_pack
 * 
 *   Serialises a frame and IPX packet to the given buffer, which must be at
 *   least as large as the size returned by the corresponding XXX_frame_size()
 *   function.
 * 
 * XXX_frame_unpack
 * 
 *   Deserialises a frame, giving the inner IPX packet and its size. Returns
 *   true on success, false if the frame was invalid.
*/

size_t ethII_frame_size(size_t ipx_payload_len);
void ethII_frame_pack(void *frame_buffer,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dest_net, addr48_t dest_node, uint16_t dest_socket,
	const void *payload, size_t payload_len);
bool ethII_frame_unpack(const novell_ipx_packet **packet, size_t *packet_len,
	const void *frame_data, size_t frame_len);

size_t novell_frame_size(size_t ipx_payload_len);
void novell_frame_pack(void *frame_buffer,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dest_net, addr48_t dest_node, uint16_t dest_socket,
	const void *payload, size_t payload_len);
bool novell_frame_unpack(const novell_ipx_packet **packet, size_t *packet_len,
	const void *frame_data, size_t frame_len);

size_t llc_frame_size(size_t ipx_payload_len);
void llc_frame_pack(void *frame_buffer,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dest_net, addr48_t dest_node, uint16_t dest_socket,
	const void *payload, size_t payload_len);
bool llc_frame_unpack(const novell_ipx_packet **packet, size_t *packet_len,
	const void *frame_data, size_t frame_len);

#endif /* !IPXWRAPPER_ETHERNET_H */
