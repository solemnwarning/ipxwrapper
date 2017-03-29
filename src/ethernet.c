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

/* This file implements three types of functions:
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

#define WINSOCK_API_LINKAGE
#include <winsock2.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "addr.h"
#include "ethernet.h"

#define ETHERTYPE_IPX 0x8137

typedef struct ethernet_header ethernet_header;

struct ethernet_header
{
	unsigned char dest_mac[6];
	unsigned char src_mac[6];
	
	union {
		/* Depends on frame type. */
		uint16_t ethertype;
		uint16_t length;
	};
} __attribute__((__packed__));

#define LLC_SAP_NETWARE 0xE0

typedef struct llc_header llc_header;

struct llc_header
{
	uint8_t dsap;
	uint8_t ssap;
	
	/* TODO: Support for 16-bit control fields? */
	uint8_t control;
} __attribute__((__packed__));

static void _pack_ipx_packet(void *buf,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dst_net, addr48_t dst_node, uint16_t dst_socket,
	const void *payload, size_t payload_len)
{
	novell_ipx_packet *packet = buf;
	
	packet->checksum = 0xFFFF;
	packet->length   = htons(sizeof(novell_ipx_packet) + payload_len);
	packet->hops     = 0;
	packet->type     = type;

	addr32_out(packet->dest_net,  dst_net);
	addr48_out(packet->dest_node, dst_node);
	packet->dest_socket = dst_socket;

	addr32_out(packet->src_net,  src_net);
	addr48_out(packet->src_node, src_node);
	packet->src_socket = src_socket;

	memcpy(packet->data, payload, payload_len);
}

size_t ethII_frame_size(size_t ipx_payload_len)
{
	if(ipx_payload_len > NOVELL_IPX_PACKET_MAX_PAYLOAD)
	{
		return 0;
	}
	
	return sizeof(ethernet_header)
		+ sizeof(novell_ipx_packet)
		+ ipx_payload_len;
}

void ethII_frame_pack(void *frame_buffer,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dst_net, addr48_t dst_node, uint16_t dst_socket,
	const void *payload, size_t payload_len)
{
	ethernet_header *eth_h = frame_buffer;
	
	addr48_out(eth_h->dest_mac, dst_node);
	addr48_out(eth_h->src_mac,  src_node);
	eth_h->ethertype = htons(ETHERTYPE_IPX);
	
	_pack_ipx_packet(eth_h + 1,
		type,
		src_net, src_node, src_socket,
		dst_net, dst_node, dst_socket,
		payload, payload_len);
}

bool ethII_frame_unpack(const novell_ipx_packet **packet, size_t *packet_len, const void *frame_data, size_t frame_len)
{
	if(frame_len < sizeof(ethernet_header) + sizeof(novell_ipx_packet))
	{
		/* Frame is too small to contain all the necessary headers. */
		return false;
	}
	
	const ethernet_header *eth_h = frame_data;
	
	if(ntohs(eth_h->ethertype) != ETHERTYPE_IPX)
	{
		/* The ethertype field isn't IPX. */
		return false;
	}
	
	*packet     = (const novell_ipx_packet*)(eth_h + 1);
	*packet_len = frame_len - sizeof(ethernet_header);
	
	return true;
}

size_t novell_frame_size(size_t ipx_payload_len)
{
	static const size_t OVERHEAD
		= sizeof(ethernet_header)
		+ sizeof(novell_ipx_packet);
	
	if(ipx_payload_len > NOVELL_IPX_PACKET_MAX_PAYLOAD
		|| ipx_payload_len > (1500 - OVERHEAD))
	{
		return 0;
	}
	
	return OVERHEAD + ipx_payload_len;
}

void novell_frame_pack(void *frame_buffer,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dst_net, addr48_t dst_node, uint16_t dst_socket,
	const void *payload, size_t payload_len)
{
	ethernet_header *eth_h = frame_buffer;
	
	addr48_out(eth_h->dest_mac, dst_node);
	addr48_out(eth_h->src_mac,  src_node);
	eth_h->length = htons(sizeof(novell_ipx_packet) + payload_len);
	
	_pack_ipx_packet(eth_h + 1,
		type,
		src_net, src_node, src_socket,
		dst_net, dst_node, dst_socket,
		payload, payload_len);
}

bool novell_frame_unpack(const novell_ipx_packet **packet, size_t *packet_len, const void *frame_data, size_t frame_len)
{
	if(frame_len < sizeof(ethernet_header) + sizeof(novell_ipx_packet))
	{
		/* Frame is too small to contain all the necessary headers. */
		return false;
	}
	
	const ethernet_header *eth_h = frame_data;
	
	uint16_t payload_len = ntohs(eth_h->length);
	
	if(payload_len > 1500)
	{
		/* Payload too big, probably an Ethernet II frame. */
		return false;
	}
	else if(payload_len < sizeof(novell_ipx_packet))
	{
		/* Too small to hold an IPX header. */
		return false;
	}
	else if(payload_len > frame_len - sizeof(ethernet_header))
	{
		/* Payload length runs past the end of frame_len, was the frame
		 * somehow truncated?
		*/
		return false;
	}
	
	*packet     = (const novell_ipx_packet*)(eth_h + 1);
	*packet_len = payload_len;
	
	return true;
}

size_t llc_frame_size(size_t ipx_payload_len)
{
	static const size_t OVERHEAD
		= sizeof(ethernet_header)
		+ sizeof(llc_header)
		+ sizeof(novell_ipx_packet);
	
	if(ipx_payload_len > NOVELL_IPX_PACKET_MAX_PAYLOAD
		|| ipx_payload_len > (1500 - OVERHEAD))
	{
		return 0;
	}
	
	return OVERHEAD + ipx_payload_len;
}

void llc_frame_pack(void *frame_buffer,
	uint8_t type,
	addr32_t src_net,  addr48_t src_node,  uint16_t src_socket,
	addr32_t dst_net, addr48_t dst_node, uint16_t dst_socket,
	const void *payload, size_t payload_len)
{
	ethernet_header *eth_h = frame_buffer;
	
	addr48_out(eth_h->dest_mac, dst_node);
	addr48_out(eth_h->src_mac,  src_node);
	eth_h->length = htons(sizeof(llc_header) + sizeof(novell_ipx_packet) + payload_len);
	
	llc_header *llc_h = (llc_header*)(eth_h + 1);
	
	llc_h->dsap    = LLC_SAP_NETWARE;
	llc_h->ssap    = LLC_SAP_NETWARE;
	llc_h->control = 0x03;
	
	_pack_ipx_packet(llc_h + 1,
		type,
		src_net, src_node, src_socket,
		dst_net, dst_node, dst_socket,
		payload, payload_len);
}

bool llc_frame_unpack(const novell_ipx_packet **packet, size_t *packet_len, const void *frame_data, size_t frame_len)
{
	if(frame_len < (sizeof(ethernet_header) + sizeof(llc_header) + sizeof(novell_ipx_packet)))
	{
		/* Frame is too small to contain all the necessary headers. */
		return false;
	}
	
	const ethernet_header *eth_h = frame_data;
	const llc_header      *llc_h = (const llc_header*)(eth_h + 1);
	
	uint16_t payload_len = ntohs(eth_h->length);
	
	if(payload_len > 1500)
	{
		/* Payload length too big, probably an Ethernet II frame. */
		return false;
	}
	else if(payload_len < (sizeof(llc_header) + sizeof(novell_ipx_packet)))
	{
		/* Payload length too short to hold all the headers required
		 * for an IPX packet.
		*/
		return false;
	}
	else if(payload_len > (frame_len - sizeof(ethernet_header)))
	{
		/* Payload length runs past the end of frame_len, was the frame
		 * somehow truncated?
		*/
		return false;
	}
	else{
		/* Payload length looks good. */
	}
	
	if(llc_h->dsap != LLC_SAP_NETWARE)
	{
		/* Not addressed to the Netware SAP. */
		return false;
	}
	
	if(llc_h->control != 0x03)
	{
		/* Some link layer control message. Probably. */
		return false;
	}
	
	*packet     = (const novell_ipx_packet*)(llc_h + 1);
	*packet_len = payload_len - sizeof(llc_header);
	
	return true;
}
