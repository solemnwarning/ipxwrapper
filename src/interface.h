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

struct ipx_interface {
	uint32_t ipaddr;
	uint32_t netmask;
	uint32_t bcast;
	
	unsigned char hwaddr[6];
	
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	
	struct ipx_interface *next;
};

struct ipx_interface *get_interfaces(int ifnum);
void free_interfaces(struct ipx_interface *iface);

#endif /* !IPXWRAPPER_INTERFACE_H */
