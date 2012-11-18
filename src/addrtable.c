/* IPXWrapper - Address table
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

/* The address table is used to co-ordinate the IPX addresses in use accross all
 * IPXWrapper instances.
*/

#include <windows.h>
#include <winsock2.h>
#include <uthash.h>

#include "addrtable.h"
#include "ipxwrapper.h"

#define ADDR_TABLE_SIZE (sizeof(addr_table_header_t) + (ADDR_TABLE_MAX_ENTRIES * sizeof(addr_table_entry_t)))

static HANDLE addr_table_mutex = NULL;

static HANDLE addr_table_h = NULL;

static addr_table_header_t *addr_table_header = NULL;
static addr_table_entry_t *addr_table_base = NULL;

static void _init_fail(void)
{
	if(addr_table_mutex)
	{
		ReleaseMutex(addr_table_mutex);
	}
	
	log_printf(LOG_WARNING, "Multiple processes may have address conflicts!");
	addr_table_cleanup();
}

void addr_table_init(void)
{
	/* Mutex used to protect the address table. */
	if(!(addr_table_mutex = CreateMutex(NULL, FALSE, ADDR_TABLE_MUTEX)))
	{
		log_printf(LOG_ERROR, "Failed to create/open mutex: %s", w32_error(GetLastError()));
		_init_fail();
		
		return;
	}
	
	addr_table_lock();
	
	/* Allocate the address table "file" (shared memory). */
	
	if(!(addr_table_h = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, ADDR_TABLE_SIZE, ADDR_TABLE_NAME)))
	{
		log_printf(LOG_ERROR, "Failed to create/open address table: %s", w32_error(GetLastError()));
		_init_fail();
		
		return;
	}
	
	/* True if the table didn't exist before we created it. */
	bool new_table = (GetLastError() != ERROR_ALREADY_EXISTS);
	
	/* Map the address table. */
	if(!(addr_table_header = MapViewOfFile(addr_table_h, FILE_MAP_WRITE, 0, 0, ADDR_TABLE_SIZE)))
	{
		log_printf(LOG_ERROR, "Failed to map address table: %s", w32_error(GetLastError()));
		_init_fail();
		
		return;
	}
	
	addr_table_base = (addr_table_entry_t*)(addr_table_header + 1);
	
	if(new_table)
	{
		/* Initialise the address table. */
		
		memset(addr_table_header, 0, ADDR_TABLE_SIZE);
		
		addr_table_header->version = ADDR_TABLE_VERSION;
	}
	else if(addr_table_header->version != ADDR_TABLE_VERSION)
	{
		log_printf(LOG_ERROR, "Address table from incompatible IPXWrapper version present");
		_init_fail();
		
		return;
	}
	
	addr_table_unlock();
}

/* Release all handles to the address table and associated objects. */
void addr_table_cleanup(void)
{
	if(addr_table_header)
	{
		UnmapViewOfFile(addr_table_header);
	}
	
	addr_table_header = NULL;
	addr_table_base   = NULL;
	
	if(addr_table_h)
	{
		CloseHandle(addr_table_h);
		addr_table_h = NULL;
	}
	
	if(addr_table_mutex)
	{
		CloseHandle(addr_table_mutex);
		addr_table_mutex = NULL;
	}
}

void addr_table_lock(void)
{
	if(addr_table_mutex)
	{
		WaitForSingleObject(addr_table_mutex, INFINITE);
	}
}

void addr_table_unlock(void)
{
	if(addr_table_mutex)
	{
		ReleaseMutex(addr_table_mutex);
	}
}

/* Search the address table for any conflicting binds. Falls back to searching
 * the sockets table if the address table is unavailable.
 * 
 * Returns false if a conflict was confirmed, true otherwise.
*/
bool addr_table_check(const struct sockaddr_ipx *addr, bool reuse)
{
	if(addr_table_base)
	{
		addr_table_lock();
		
		addr_table_entry_t *entry = addr_table_base;
		addr_table_entry_t *end   = addr_table_base + ADDR_TABLE_MAX_ENTRIES;
		
		while(entry < end && (entry->flags & ADDR_TABLE_ENTRY_VALID))
		{
			if(addr->sa_socket == entry->socket && (!(entry->flags & ADDR_TABLE_ENTRY_REUSE) || !reuse))
			{
				/* A socket is already bound to this address and either
				* it or this one doesn't have SO_REUSEADDR set.
				*/
				
				addr_table_unlock();
				return false;
			}
			
			entry++;
		}
		
		addr_table_unlock();
	}
	else{
		/* Address table is unavailable, check the sockets table
		 * instead. This will not maintain address uniqueness between
		 * multiple processes!
		*/
		
		lock_sockets();
		
		ipx_socket *s, *tmp;
		
		HASH_ITER(hh, sockets, s, tmp)
		{
			if(memcmp(&(s->addr), addr, sizeof(struct sockaddr_ipx)) == 0 && (!(s->flags & IPX_REUSE) || !reuse))
			{
				unlock_sockets();
				return true;
			}
		}
		
		unlock_sockets();
	}
	
	return true;
}

/* Return an unused socket number in network byte order for automatic allocation,
 * zero if none are available.
*/
uint16_t addr_table_auto_socket(void)
{
	/* Automatic socket allocations start at 1024, I have no idea if this is
	 * normal IPX behaviour, but IP does it and it doesn't seem to interfere
	 * with any IPX software I've tested.
	*/
	
	uint16_t sock = 1024;
	
	if(addr_table_base)
	{
		addr_table_lock();
		
		addr_table_entry_t *entry = addr_table_base;
		addr_table_entry_t *end   = addr_table_base + ADDR_TABLE_MAX_ENTRIES;
		
		while(entry < end)
		{
			if(ntohs(sock) == entry->socket)
			{
				if(sock == 65535)
				{
					addr_table_unlock();
					return 0;
				}
				
				sock++;
				
				entry = addr_table_base;
				continue;
			}
			
			entry++;
		}
		
		addr_table_unlock();
	}
	else{
		lock_sockets();
		
		ipx_socket *s, *tmp;
		
		HASH_ITER(hh, sockets, s, tmp)
		{
			if((s->flags & IPX_BOUND) && ntohs(sock) == s->addr.sa_socket)
			{
				if(sock == 65535)
				{
					unlock_sockets();
					return 0;
				}
				
				sock++;
				
				s = sockets;
				continue;
			}
		}
		
		unlock_sockets();
	}
	
	return htons(sock);
}

/* Insert an entry into the address table.
 * 
 * Performs no conflict checking. Take the address table lock in the caller and
 * use addr_table_check() before calling this.
*/
void addr_table_add(const struct sockaddr_ipx *addr, uint16_t port, bool reuse)
{
	if(!addr_table_base)
	{
		return;
	}
	
	addr_table_lock();
	
	/* Iterate over the address table to find the last entry. */
	
	addr_table_entry_t *entry = addr_table_base;
	addr_table_entry_t *end   = addr_table_base + ADDR_TABLE_MAX_ENTRIES;
	
	while(entry < end && (entry->flags & ADDR_TABLE_ENTRY_VALID))
	{
		entry++;
	}
	
	/* Append the new address to the address table. */
	
	if(entry < end)
	{
		entry->netnum  = addr32_in(addr->sa_netnum);
		entry->nodenum = addr48_in(addr->sa_nodenum);
		entry->socket  = addr->sa_socket;
		
		entry->flags = ADDR_TABLE_ENTRY_VALID;
		
		if(reuse)
		{
			entry->flags |= ADDR_TABLE_ENTRY_REUSE;
		}
		
		entry->port = port;
		entry->time = time(NULL);
	}
	else{
		log_printf(LOG_ERROR, "Out of address table slots, not appending!");
	}
	
	addr_table_unlock();
}

/* Remove an entry from the address table. */
void addr_table_remove(uint16_t port)
{
	if(!addr_table_base)
	{
		return;
	}
	
	addr_table_lock();
	
	/* Iterate over the address table until we find the correct entry... */
	
	addr_table_entry_t *entry = addr_table_base;
	addr_table_entry_t *end   = addr_table_base + ADDR_TABLE_MAX_ENTRIES;
	
	while(entry < end && (entry->flags & ADDR_TABLE_ENTRY_VALID) && entry->port != port)
	{
		entry++;
	}
	
	/* Continue iteration until we find the last entry... */
	
	addr_table_entry_t *last = entry;
	
	while(last < end && (last->flags & ADDR_TABLE_ENTRY_VALID))
	{
		last++;
	}
	
	/* Replace entry with the last entry and mark the latter as invalid. */
	
	if(entry < end)
	{
		*entry = *last;
		
		last->flags &= ~ADDR_TABLE_ENTRY_VALID;
	}
	
	addr_table_unlock();
}

/* Update the time field for any of our entries in the address table and remove
 * any that have expired (most likely a crashed process).
*/
void addr_table_update(void)
{
	if(!addr_table_base)
	{
		return;
	}
	
	lock_sockets();
	
	addr_table_lock();
	
	/* Remove any expired entries. */
	
	addr_table_entry_t *entry = addr_table_base;
	addr_table_entry_t *last  = addr_table_base;
	addr_table_entry_t *end   = addr_table_base + ADDR_TABLE_MAX_ENTRIES;
	
	while(last < end && (last->flags & ADDR_TABLE_ENTRY_VALID))
	{
		last++;
	}
	
	for(; entry < end && (entry->flags & ADDR_TABLE_ENTRY_VALID); entry++)
	{
		if(entry->time + ADDR_TABLE_ENTRY_TIMEOUT <= time(NULL))
		{
			*end = *last;
			
			last->flags &= ~ADDR_TABLE_ENTRY_VALID;
			last--;
		}
	}
	
	/* This is really, really efficient. */
	
	ipx_socket *sock, *tmp;
	
	HASH_ITER(hh, sockets, sock, tmp)
	{
		if(sock->flags & IPX_BOUND)
		{
			/* Search the address table... */
			
			for(entry = addr_table_base; entry < end && (entry->flags & ADDR_TABLE_ENTRY_VALID); entry++)
			{
				if(entry->port == sock->port)
				{
					entry->time = time(NULL);
					break;
				}
			}
		}
	}
	
	addr_table_unlock();
	
	unlock_sockets();
}
