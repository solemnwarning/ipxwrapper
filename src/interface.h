/* IPXWrapper - Interface header
 * Copyright (C) 2011 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef IPXWRAPPER_INTERFACE_H
#define IPXWRAPPER_INTERFACE_H

#include <stdint.h>
#include <utlist.h>

#include "common.h"

typedef struct ipx_interface ipx_interface_t;

struct ipx_interface {
	uint32_t ipaddr;
	uint32_t netmask;
	uint32_t bcast;
	
	addr48_t hwaddr;
	
	addr32_t ipx_net;
	addr48_t ipx_node;
	
	ipx_interface_t *prev;
	ipx_interface_t *next;
};

ipx_interface_t *get_interfaces(int ifnum);
void free_interfaces(ipx_interface_t *iface);

#endif /* !IPXWRAPPER_INTERFACE_H */
