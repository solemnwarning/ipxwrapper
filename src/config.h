/* ipxwrapper - Configuration header
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

#ifndef IPX_CONFIG_H
#define IPX_CONFIG_H

#define DEFAULT_PORT 54792
#define DEFAULT_ROUTER_PORT 54793
#define IFACE_TTL 10

#include "common.h"

typedef struct main_config {
	uint16_t udp_port;
	uint16_t router_port;
	
	bool w95_bug;
	bool bcast_all;
	bool src_filter;
} main_config_t;

struct v1_global_config {
	uint16_t udp_port;
	unsigned char w95_bug;
	unsigned char bcast_all;
	unsigned char filter;
} __attribute__((__packed__));

typedef struct iface_config {
	addr32_t netnum;
	addr48_t nodenum;
	
	bool enabled;
} iface_config_t;

struct v1_iface_config {
	unsigned char ipx_net[4];
	unsigned char ipx_node[6];
	unsigned char enabled;
	unsigned char primary;
} __attribute__((__packed__));

main_config_t get_main_config(void);

iface_config_t get_iface_config(addr48_t hwaddr);
addr48_t get_primary_iface();

#endif /* !IPX_CONFIG_H */
