/* IPXWrapper - Packet coalescing
 * Copyright (C) 2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <uthash.h>
#include <utlist.h>
#include <winsock2.h>
#include <windows.h>

#include "coalesce.h"
#include "ethernet.h"
#include "interface.h"
#include "ipxwrapper.h"

struct coalesce_table_key
{
	addr32_t netnum;
	addr48_t nodenum;
	uint16_t socket;
};

typedef struct coalesce_table_key coalesce_table_key;

struct coalesce_dest
{
	UT_hash_handle hh;
	
	struct coalesce_dest *prev;
	struct coalesce_dest *next;
	
	coalesce_table_key dest;
	bool active;
	
	uint64_t send_timestamps[IPXWRAPPER_COALESCE_PACKET_TRACK_COUNT];
	
	uint64_t payload_timestamp;
	unsigned char payload[IPXWRAPPER_COALESCE_PACKET_MAX_SIZE];
	int payload_used;
};

typedef struct coalesce_dest coalesce_dest;

/* coalesce_table provides access to all coalesce_dest structures and should
 * be iterated using the 'hh' member.
*/
static coalesce_dest *coalesce_table = NULL;

/* coalesce_pending provides access to all active coalesce_dest structures
 * which have data waiting to be transmitted, ordered by insertion date from
 * oldest to newest.
*/
static coalesce_dest *coalesce_pending = NULL;

coalesce_dest *get_coalesce_by_dest(addr32_t netnum, addr48_t nodenum, uint16_t socket)
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_get_coalesce_by_dest]));
	
	if(!main_config.dosbox_coalesce)
	{
		/* Skip coalescing if disabled. */
		return NULL;
	}
	
	coalesce_table_key dest = { netnum, nodenum, socket };
	
	coalesce_dest *node;
	HASH_FIND(hh, coalesce_table, &dest, sizeof(dest), node);
	
	if(node == NULL)
	{
		FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_get_coalesce_by_dest_new]));
		
		/* TODO: Limit maximum number of nodes, recycle old ones. */
		
		node = malloc(sizeof(coalesce_dest));
		if(node == NULL)
		{
			return NULL;
		}
		
		memset(node, 0, sizeof(*node));
		node->dest = dest;
		
		HASH_ADD(hh, coalesce_table, dest, sizeof(node->dest), node);
	}
	
	return node;
}

bool coalesce_register_send(coalesce_dest *node, uint64_t timestamp)
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_register_send]));
	
	memmove(node->send_timestamps, node->send_timestamps + 1, sizeof(node->send_timestamps) - sizeof(*(node->send_timestamps)));
	node->send_timestamps[IPXWRAPPER_COALESCE_PACKET_TRACK_COUNT - 1] = timestamp;
	
	if((node->send_timestamps[0] + IPXWRAPPER_COALESCE_PACKET_START_THRESH) >= timestamp)
	{
		return true;
	}
	else if(node->active && (node->send_timestamps[0] + IPXWRAPPER_COALESCE_PACKET_STOP_THRESH) < timestamp)
	{
		return false;
	}
	else{
		return node->active;
	}
}

bool coalesce_add_data(coalesce_dest *cd, const void *data, int size, uint64_t now)
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_add_data]));
	
	if(cd->payload_used == 0)
	{
		FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_add_data_init]));
		
		novell_ipx_packet header;
		
		header.checksum = 0xFFFF;
		header.hops = 0;
		header.type = IPX_MAGIC_COALESCED;
		
		addr32_out(header.dest_net, cd->dest.netnum);
		addr48_out(header.dest_node, cd->dest.nodenum);
		header.dest_socket = 0;
		
		addr32_out(header.src_net, dosbox_local_netnum);
		addr48_out(header.src_node, dosbox_local_nodenum);
		header.src_socket = 0;
		
		if((sizeof(header) + size) > IPXWRAPPER_COALESCE_PACKET_MAX_SIZE)
		{
			return false;
		}
		
		cd->payload_timestamp = now;
		DL_APPEND(coalesce_pending, cd);
		
		memcpy(cd->payload, &header, sizeof(header));
		cd->payload_used = sizeof(header);
	}
	
	if((cd->payload_used + size) <= IPXWRAPPER_COALESCE_PACKET_MAX_SIZE)
	{
		memcpy((cd->payload + cd->payload_used), data, size);
		cd->payload_used += size;
		
		return true;
	}
	else{
		return false;
	}
}

void coalesce_flush(coalesce_dest *cd)
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_flush]));
	
	assert(cd->payload_used > 0);
	
	novell_ipx_packet *header = (novell_ipx_packet*)(cd->payload);
	header->length = htons(cd->payload_used);
	
	log_printf(LOG_DEBUG, "Sending coalesced packet (%d bytes)", cd->payload_used);
	
	if(r_sendto(private_socket, (const void*)(cd->payload), cd->payload_used, 0, (struct sockaddr*)(&dosbox_server_addr), sizeof(dosbox_server_addr)) < 0)
	{
		log_printf(LOG_ERROR, "Error sending DOSBox IPX packet: %s", w32_error(WSAGetLastError()));
	}
	else{
		__atomic_add_fetch(&send_packets_udp, 1, __ATOMIC_RELAXED);
		__atomic_add_fetch(&send_bytes_udp, cd->payload_used, __ATOMIC_RELAXED);
	}
	
	cd->payload_used = 0;
	DL_DELETE(coalesce_pending, cd);
}

DWORD coalesce_send(const void *data, size_t data_size, addr32_t dest_net, addr48_t dest_node, uint16_t dest_socket)
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_send]));
	
	/* We should always be called with an IPX header, even if the
	 * application is sending zero-byte payloads.
	*/
	assert(data_size > 0);
	
	uint64_t now = get_uticks();
	bool queued = false;
	
	coalesce_dest *cd = get_coalesce_by_dest(dest_net, dest_node, dest_socket);
	if(cd != NULL)
	{
		FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_send_cd]));
		
		bool should_coalesce = coalesce_register_send(cd, now);
		
		if(should_coalesce && !cd->active)
		{
			IPX_STRING_ADDR(dest_addr, dest_net, dest_node, dest_socket);
			log_printf(LOG_WARNING, "High send rate to %s detected, coalescing future packets\n", dest_addr);
			
			cd->active = true;
		}
		else if(!should_coalesce && cd->active)
		{
			IPX_STRING_ADDR(dest_addr, dest_net, dest_node, dest_socket);
			log_printf(LOG_INFO, "Send rate to %s has dropped, no longer coalescing packets\n", dest_addr);
			
			cd->active = false;
		}
		
		if(
			should_coalesce
			&& (cd->payload_used + data_size) > IPXWRAPPER_COALESCE_PACKET_MAX_SIZE
			&& data_size < (IPXWRAPPER_COALESCE_PACKET_MAX_SIZE / 2))
		{
			coalesce_flush(cd);
		}
		
		if(should_coalesce && coalesce_add_data(cd, data, data_size, now))
		{
			queued = true;
		}
		
		if(cd->payload_used > 0 && (cd->payload_timestamp + IPXWRAPPER_COALESCE_PACKET_MAX_DELAY) <= now)
		{
			coalesce_flush(cd);
		}
	}
	
	if(!queued)
	{
		FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_coalesce_send_immediate]));
		
		if(r_sendto(private_socket, (const void*)(data), data_size, 0, (struct sockaddr*)(&dosbox_server_addr), sizeof(dosbox_server_addr)) < 0)
		{
			DWORD error = WSAGetLastError();
			log_printf(LOG_ERROR, "Error sending DOSBox IPX packet: %s", w32_error(error));
			
			return error;
		}
		else{
			__atomic_add_fetch(&send_packets_udp, 1, __ATOMIC_RELAXED);
			__atomic_add_fetch(&send_bytes_udp, data_size, __ATOMIC_RELAXED);
		}
	}
	
	return ERROR_SUCCESS;
}

void coalesce_flush_waiting(void)
{
	uint64_t now = get_uticks();
	
	while(coalesce_pending != NULL
		&& (coalesce_pending->payload_timestamp + IPXWRAPPER_COALESCE_PACKET_MAX_DELAY) <= now)
	{
		coalesce_flush(coalesce_pending);
	}
}

void coalesce_cleanup(void)
{
	while(coalesce_pending != NULL)
	{
		coalesce_flush(coalesce_pending);
	}
	
	while(coalesce_table != NULL)
	{
		coalesce_dest *cd = coalesce_table;
		
		HASH_DEL(coalesce_table, cd);
		free(cd);
	}
}
