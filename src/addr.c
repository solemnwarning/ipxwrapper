/* ipxwrapper - Address manipulation functions
 * Copyright (C) 2012 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "addr.h"
#include "common.h"

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
	
	sprintf(
		buf,
		
		"%02X:%02X:%02X:%02X",
		
		(unsigned int)(c[0]),
		(unsigned int)(c[1]),
		(unsigned int)(c[2]),
		(unsigned int)(c[3])
	);
	
	return buf;
}

/* Read a 32-bit network address from the registry.
 * Returns default_value upon failure.
*/
addr32_t reg_get_addr32(HKEY key, const char *name, addr32_t default_value)
{
	unsigned char buf[4];
	reg_get_bin(key, name, buf, 4, &default_value);
	
	return addr32_in(buf);
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
	
	sprintf(
		buf,
		
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

/* Read a 48-bit network address from the registry.
 * Returns default_value upon failure.
*/
addr48_t reg_get_addr48(HKEY key, const char *name, addr48_t default_value)
{
	unsigned char buf[6];
	reg_get_bin(key, name, buf, 6, &default_value);
	
	return addr48_in(buf);
}

/* Format an IPX address as a string.
 *
 * The socket number should be in network byte order and the supplied buffer
 * must be at least IPX_SADDR_SIZE bytes long.
*/
void ipx_to_string(char *buf, addr32_t net, addr48_t node, uint16_t sock)
{
	addr32_string(buf, net);
	buf[17] = '/';
	
	addr48_string(buf + 18, node);
	buf[29] = '/';
	
	sprintf(buf + 30, "%hu", ntohs(sock));
}
