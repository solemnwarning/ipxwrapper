/* ipxwrapper - Configuration header
 * Copyright (C) 2011-2021 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum main_config_encap_type
{
	ENCAP_TYPE_IPXWRAPPER = 0,
	ENCAP_TYPE_PCAP = 1,
	ENCAP_TYPE_DOSBOX = 2,
};

enum main_config_frame_type
{
	FRAME_TYPE_ETH_II = 1,
	FRAME_TYPE_NOVELL = 2,
	FRAME_TYPE_LLC    = 3,
};

typedef struct main_config {
	uint16_t udp_port;
	
	bool w95_bug;
	bool fw_except;
	enum main_config_encap_type encap_type;
	enum main_config_frame_type frame_type;
	
	char *dosbox_server_addr;
	uint16_t dosbox_server_port;
	bool dosbox_coalesce;
	
	enum ipx_log_level log_level;
	bool profile;
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

main_config_t get_main_config(bool ignore_ini);
bool set_main_config(const main_config_t *config);

iface_config_t get_iface_config(addr48_t hwaddr);
bool set_iface_config(addr48_t hwaddr, const iface_config_t *config);

addr48_t get_primary_iface();
bool set_primary_iface(addr48_t primary);

#ifdef __cplusplus
}
#endif

#endif /* !IPX_CONFIG_H */
