/* ipxwrapper - Address manipulation functions
 * Copyright (C) 2012-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <snprintf.h>
#include <ctype.h>

#include "addr.h"
#include "common.h"

static bool _addr_from_string(unsigned char *dest, const char *src, int size)
{
	int i;
	
	for(i = 0; i < size; i++)
	{
		char term = (i + 1 == size ? '\0' : ':');
		
		if(isxdigit(src[0]) && (src[1] == term || (isxdigit(src[1]) && src[2] == term)))
		{
			dest[i] = strtoul(src, NULL, 16);
			
			src += strcspn(src, ":");
			src += strspn(src, ":");
		}
		else{
			return false;
		}
	}
	
	return true;
}

addr32_t addr32_in(const void *src)
{
	addr32_t buf = 0;
	memcpy(&buf, src, 4);
	
	return buf;
}

/* Write out a 32-bit address in network byte order. */
void *addr32_out(void *dest, addr32_t src)
{
	return memcpy(dest, &src, 4);
}

/* Convert a 32-bit address to a string in the format XX:XX:XX:XX */
char *addr32_string(char *buf, addr32_t addr)
{
	unsigned char c[6];
	addr32_out(c, addr);
	
	mirtoto_snprintf(
		buf,
		ADDR32_STRING_SIZE,
		
		"%02X:%02X:%02X:%02X",
		
		(unsigned int)(c[0]),
		(unsigned int)(c[1]),
		(unsigned int)(c[2]),
		(unsigned int)(c[3])
	);
	
	return buf;
}

/* Parse a string-formatted 32-bit address and store it in an addr32_t value.
 * Returns true on success, false on parse error.
*/
bool addr32_from_string(addr32_t *dest, const char *src)
{
	return _addr_from_string((unsigned char*)(dest), src, 4);
}

addr48_t addr48_in(const void *src)
{
	addr48_t buf = 0;
	memcpy(((char*)&buf) + 2, src, 6);
	
	return buf;
}

/* Write out a 48-bit address in network byte order. */
void *addr48_out(void *dest, addr48_t src)
{
	return memcpy(dest, ((char*)&src) + 2, 6);
}

/* Convert a 48-bit address to a string in the format XX:XX:XX:XX:XX:XX */
char *addr48_string(char *buf, addr48_t addr)
{
	unsigned char c[6];
	addr48_out(c, addr);
	
	mirtoto_snprintf(
		buf,
		ADDR48_STRING_SIZE,
		
		"%02X:%02X:%02X:%02X:%02X:%02X",
		
		(unsigned int)(c[0]),
		(unsigned int)(c[1]),
		(unsigned int)(c[2]),
		(unsigned int)(c[3]),
		(unsigned int)(c[4]),
		(unsigned int)(c[5])
	);
	
	return buf;
}

/* Parse a string-formatted 48-bit address and store it in an addr48_t value.
 * Returns true on success, false on parse error.
*/
bool addr48_from_string(addr48_t *dest, const char *src)
{
	return _addr_from_string((unsigned char*)(dest) + 2, src, 6);
}

/* Format an IPX address as a string.
 *
 * The socket number should be in network byte order and the supplied buffer
 * must be at least IPX_SADDR_SIZE bytes long.
*/
void ipx_to_string(char *buf, addr32_t net, addr48_t node, uint16_t sock)
{
	addr32_string(buf, net);
	buf[11] = '/';
	
	addr48_string(buf + 12, node);
	buf[29] = '/';
	
	mirtoto_snprintf(buf + 30, IPX_SADDR_SIZE - 30, "%hu", ntohs(sock));
}

/* Generate a (probably) unique locally-administered MAC address. */
addr48_t gen_random_mac()
{
	static bool seeded = false;
	
	if(!seeded)
	{
		srand(GetTickCount());
		seeded = true;
	}
	
	unsigned char addr[6];
	unsigned int i;
	
	for(i = 0; i < 6; i++)
	{
		addr[i] = (rand() % 0xFF);
	}
	
	addr[0] |= 0x02;
	
	return addr48_in(addr);
}
