/* IPXWrapper - Address cache
 * Copyright (C) 2008-2012 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <uthash.h>

#include "addrcache.h"
#include "common.h"
#include "ipxwrapper.h"

#define ADDR_CACHE_TTL 30

struct host_table_key {
	addr32_t netnum;
	addr48_t nodenum;
};

struct host_table {
	UT_hash_handle hh;
	
	struct host_table_key key;
	time_t time;
	
	SOCKADDR_STORAGE addr;
	size_t addrlen;
};

typedef struct host_table host_table_t;
typedef struct host_table_key host_table_key_t;

static host_table_t *host_table = NULL;
static CRITICAL_SECTION host_table_cs;

/* Lock the host table */
static void host_table_lock(void)
{
	EnterCriticalSection(&host_table_cs);
}

/* Unlock the host table */
static void host_table_unlock(void)
{
	LeaveCriticalSection(&host_table_cs);
}

/* Search the host table for a node with the given net/node pair.
 * Returns NULL on failure.
*/
static host_table_t *host_table_find(addr32_t net, addr48_t node)
{
	host_table_key_t key;
	memset(&key, 0, sizeof(key));
	
	key.netnum  = net;
	key.nodenum = node;

	host_table_t *host;
	
	HASH_FIND(hh, host_table, &key, sizeof(key), host);
	
	return host;
}

/* Delete a node from the host table */
static void host_table_delete(host_table_t *host)
{
	HASH_DEL(host_table, host);
	
	free(host);
}

/* Initialise the address cache */
void addr_cache_init(void)
{
	if(!InitializeCriticalSectionAndSpinCount(&host_table_cs, 0x80000000))
	{
		log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
		abort();
	}
}

/* Free all resources used by the address cache */
void addr_cache_cleanup(void)
{
	/* Delete all nodes in the host table */
	
	host_table_t *host, *tmp;
	
	HASH_ITER(hh, host_table, host, tmp)
	{
		host_table_delete(host);
	}
	
	/* Delete the host table lock */
	
	DeleteCriticalSection(&host_table_cs);
}

/* Search the address cache for the best address to send a packet to.
 *
 * Writes a sockaddr structure and addrlen to the provided pointers. Returns
 * true if a cached address was found, false otherwise.
*/
int addr_cache_get(SOCKADDR_STORAGE *addr, size_t *addrlen, addr32_t net, addr48_t node, uint16_t sock)
{
	host_table_lock();
	
	host_table_t *host = host_table_find(net, node);
	
	if(host && time(NULL) < host->time + ADDR_CACHE_TTL)
	{
		memcpy(addr, &(host->addr), host->addrlen);
		*addrlen = host->addrlen;
		
		host_table_unlock();
		return 1;
	}
	
	
	host_table_unlock();
	return 0;
}

/* Update the address cache.
 *
 * The given address will be treated as the host's defaut (i.e router port) if
 * sock is zero, otherwise it will be for the given socket number only.
 *
 * The given sockaddr structure will be copied and may be deallocated as soon as
 * this function returns.
*/
void addr_cache_set(const struct sockaddr *addr, size_t addrlen, addr32_t net, addr48_t node, uint16_t sock)
{
	host_table_lock();
	
	host_table_t *host = host_table_find(net, node);
	
	if(!host)
	{
		/* The net/node pair doesn't exist in the address cache.
		 * Initialise an entry with no data and insert it.
		*/
		
		if(!(host = malloc(sizeof(host_table_t))))
		{
			log_printf(LOG_ERROR, "Cannot allocate memory for host_table_t!");
			
			host_table_unlock();
			return;
		}
		
		memset(host, 0, sizeof(host_table_t));
		
		host->key.netnum  = net;
		host->key.nodenum = node;
		
		HASH_ADD(hh, host_table, key, sizeof(host->key), host);
	}
	
	memcpy(&(host->addr), addr, addrlen);
	host->addrlen = addrlen;
	
	host->time = time(NULL);
	
	host_table_unlock();
}
