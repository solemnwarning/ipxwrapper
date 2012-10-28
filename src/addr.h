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

#ifndef IPXWRAPPER_ADDR_H
#define IPXWRAPPER_ADDR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t addr32_t;
typedef uint64_t addr48_t;

#define ADDR32_STRING_SIZE 12

addr32_t addr32_in(const void *src);
void *addr32_out(void *dest, addr32_t src);

char *addr32_string(char *buf, addr32_t addr);
bool addr32_from_string(addr32_t *dest, const char *src);

addr32_t reg_get_addr32(HKEY key, const char *name, addr32_t default_value);
bool reg_set_addr32(HKEY key, const char *name, addr32_t value);

#define ADDR48_STRING_SIZE 18

addr48_t addr48_in(const void *src);
void *addr48_out(void *dest, addr48_t src);

char *addr48_string(char *buf, addr48_t addr);
bool addr48_from_string(addr48_t *dest, const char *src);

addr48_t reg_get_addr48(HKEY key, const char *name, addr48_t default_value);
bool reg_set_addr48(HKEY key, const char *name, addr48_t value);

#define IPX_SADDR_SIZE 36

#define IPX_STRING_ADDR(var, net, node, sock) \
	char var[IPX_SADDR_SIZE]; \
	ipx_to_string(var, net, node, sock);

void ipx_to_string(char *buf, addr32_t net, addr48_t node, uint16_t sock);

addr48_t gen_random_mac();

#ifdef __cplusplus
}
#endif

#endif /* !IPXWRAPPER_ADDR_H */
