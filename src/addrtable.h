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

#ifndef ADDRTABLE_H
#define ADDRTABLE_H

#include <wsipx.h>
#include <stdint.h>
#include <time.h>

#include "addr.h"

#define ADDR_TABLE_MAX_ENTRIES   512
#define ADDR_TABLE_ENTRY_TIMEOUT 10

#define ADDR_TABLE_MUTEX "IPXWrapper_addr_table_mutex"
#define ADDR_TABLE_NAME  "IPXWrapper_addr_table"

#define ADDR_TABLE_VERSION 1

typedef struct addr_table_header addr_table_header_t;

struct addr_table_header
{
	int version;
};

#define ADDR_TABLE_ENTRY_VALID ((int)(1<<0))
#define ADDR_TABLE_ENTRY_REUSE ((int)(1<<1))

typedef struct addr_table_entry addr_table_entry_t;

struct addr_table_entry
{
	addr32_t netnum;
	addr48_t nodenum;
	uint16_t socket;
	
	int flags;
	uint16_t port;
	time_t time;
};

void addr_table_init(void);
void addr_table_cleanup(void);

void addr_table_lock(void);
void addr_table_unlock(void);

bool addr_table_check(const struct sockaddr_ipx *addr, bool reuse);
uint16_t addr_table_auto_socket(void);

void addr_table_add(const struct sockaddr_ipx *addr, uint16_t port, bool reuse);
void addr_table_remove(uint16_t port);

void addr_table_update(void);

#endif /* !ADDRTABLE_H */
