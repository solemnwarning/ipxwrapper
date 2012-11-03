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

/* IFACE_MODE_ALL
 * 
 * Packets are sent/received on all interfaces and no source address filtering
 * is performed. A single IPX interface is presented.
 * 
 * IFACE_MODE_SINGLE
 * 
 * Packets are sent/received on user-chosen interfaces. A single IPX interface
 * is presented.
 * 
 * IFACE_MODE_MULTI
 * 
 * An IPX interface is presented for each (enabled) real interface and packets
 * are sent or received on the single underlying interface of the IPX one.
*/

#define IFACE_MODE_ALL    1
#define IFACE_MODE_SINGLE 2
#define IFACE_MODE_MULTI  3

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct main_config {
	uint16_t udp_port;
	uint16_t router_port;
	
	bool w95_bug;
	bool bcast_all;
	bool src_filter;
	
	enum ipx_log_level log_level;
	
	int iface_mode;
	
	addr32_t single_netnum;
	addr48_t single_nodenum;
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
bool set_main_config(const main_config_t *config);

iface_config_t get_iface_config(addr48_t hwaddr);
bool set_iface_config(addr48_t hwaddr, const iface_config_t *config);

addr48_t get_primary_iface();

#ifdef __cplusplus
}
#endif

#endif /* !IPX_CONFIG_H */
