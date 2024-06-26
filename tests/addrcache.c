/* IPXWrapper test suite
 * Copyright (C) 2017-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../src/addr.h"
#include "../src/addrcache.h"
#include "../src/common.h"
#include "tap/basic.h"

/* Mock time() so we can test timing out of address cache records */

static time_t now = 0;

static time_t mock_time(void)
{
	return now;
}

/* Need to implement log_printf() for addrcache.c */

void log_printf(enum ipx_log_level level, const char *fmt, ...)
{
	va_list argv;
	
	va_start(argv, fmt);
	vfprintf(stderr, fmt, argv);
	va_end(argv);
	
	fprintf(stderr, "\n");
}

int main()
{
	extern time_t (*addrcache_time)(void);
	addrcache_time = &mock_time;
	
	plan_lazy();
	
	{
		addr_cache_init();
		
		SOCKADDR_STORAGE addr;
		size_t addrlen;
		
		ok(!addr_cache_get(&addr, &addrlen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1),
			"addr_cache_get() returns false when no addresses are known");
		
		addr_cache_cleanup();
	}
	
	{
		addr_cache_init();
		
		struct sockaddr_in addr_in;
		memset(&addr_in, 0xAB, sizeof(addr_in));
		
		addr_cache_set((struct sockaddr*)(&addr_in), sizeof(addr_in),
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1);
		
		SOCKADDR_STORAGE addr_out;
		size_t aolen;
		
		if(ok(addr_cache_get(&addr_out, &aolen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1),
			"addr_cache_get() returns true when address is known"))
		{
			is_int(sizeof(addr_in), aolen, "addr_cache_get() returns correct address length");
			is_blob(&addr_in, &addr_out, sizeof(addr_in), "addr_cache_get() returns correct address data");
		}
		
		addr_cache_cleanup();
	}
	
	{
		addr_cache_init();
		
		struct sockaddr_in addr_in;
		memset(&addr_in, 0xAB, sizeof(addr_in));
		
		addr_cache_set((struct sockaddr*)(&addr_in), sizeof(addr_in),
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1);
		
		SOCKADDR_STORAGE addr_out;
		size_t aolen;
		
		now += 29;
		
		if(ok(addr_cache_get(&addr_out, &aolen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1),
			"addr_cache_get() returns true when address is about to expire"))
		{
			is_int(sizeof(addr_in), aolen, "addr_cache_get() returns correct address length");
			is_blob(&addr_in, &addr_out, sizeof(addr_in), "addr_cache_get() returns correct address data");
		}
		
		now += 1;
		
		ok(!addr_cache_get(&addr_out, &aolen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1),
			"addr_cache_get() returns false when address has expired");
		
		addr_cache_cleanup();
	}
	
	{
		addr_cache_init();
		
		struct sockaddr_in addr_in;
		memset(&addr_in, 0xAB, sizeof(addr_in));
		
		addr_cache_set((struct sockaddr*)(&addr_in), sizeof(addr_in),
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1);
		
		SOCKADDR_STORAGE addr_out;
		size_t aolen;
		
		ok(!addr_cache_get(&addr_out, &aolen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x02}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1),
			"addr_cache_get() returns false when network number differs");
		
		addr_cache_cleanup();
	}
	
	{
		addr_cache_init();
		
		struct sockaddr_in addr_in;
		memset(&addr_in, 0xAB, sizeof(addr_in));
		
		addr_cache_set((struct sockaddr*)(&addr_in), sizeof(addr_in),
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1);
		
		SOCKADDR_STORAGE addr_out;
		size_t aolen;
		
		ok(!addr_cache_get(&addr_out, &aolen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x02}),
			1),
			"addr_cache_get() returns false when node number differs");
		
		addr_cache_cleanup();
	}
	
	{
		addr_cache_init();
		
		struct sockaddr_in addr_in;
		memset(&addr_in, 0xAB, sizeof(addr_in));
		
		addr_cache_set((struct sockaddr*)(&addr_in), sizeof(addr_in),
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			1);
		
		SOCKADDR_STORAGE addr_out;
		size_t aolen;
		
		ok(!addr_cache_get(&addr_out, &aolen,
			addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
			addr48_in((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
			2),
			"addr_cache_get() returns false when socket number differs");
		
		addr_cache_cleanup();
	}
	
	return 0;
}
