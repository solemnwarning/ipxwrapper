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

#include <stdio.h>

#include "config.h"
#include "common.h"

main_config_t get_main_config(void)
{
	/* Defaults */
	
	main_config_t config;
	
	config.udp_port       = DEFAULT_PORT;
	config.router_port    = DEFAULT_ROUTER_PORT;
	config.w95_bug        = true;
	config.bcast_all      = false;
	config.src_filter     = true;
	config.addr_cache_ttl = 30;
	config.iface_ttl      = 5;
	
	HKEY reg      = reg_open_main(false);
	DWORD version = reg_get_dword(reg, "config_version", 1);
	
	if(version == 1)
	{
		struct v1_global_config reg_config;
		
		if(reg_get_bin(reg, "global", &reg_config, sizeof(reg_config), NULL))
		{
			config.udp_port   = reg_config.udp_port;
			config.w95_bug    = reg_config.w95_bug;
			config.bcast_all  = reg_config.bcast_all;
			config.src_filter = reg_config.filter;
		}
	}
	else if(version == 2)
	{
		config.udp_port    = reg_get_dword(reg, "port", config.udp_port);
		config.router_port = reg_get_dword(reg, "router_port", config.router_port);
		config.w95_bug     = reg_get_dword(reg, "w95_bug", config.w95_bug);
		config.bcast_all   = reg_get_dword(reg, "bcast_all", config.bcast_all);
		config.src_filter  = reg_get_dword(reg, "src_filter", config.src_filter);
	}
	
	reg_close(reg);
	
	return config;
}

iface_config_t get_iface_config(addr48_t hwaddr)
{
	char id[18];
	addr48_string(id, hwaddr);
	
	addr32_t default_net = addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01});
	
	iface_config_t config;
	
	HKEY reg      = reg_open_main(false);
	DWORD version = reg_get_dword(reg, "config_version", 1);
	
	if(version == 1)
	{
		struct v1_iface_config reg_config;
		
		if(reg_get_bin(reg, id, &reg_config, sizeof(reg_config), NULL))
		{
			config.netnum  = addr32_in(reg_config.ipx_net);
			config.nodenum = addr48_in(reg_config.ipx_node);
			config.enabled = reg_config.enabled;
			
			reg_close(reg);
			
			return config;
		}
	}
	else if(version == 2)
	{
		HKEY iface_reg = reg_open_subkey(reg, id, false);
		
		config.netnum  = reg_get_addr32(iface_reg, "net", default_net);
		config.nodenum = reg_get_addr48(iface_reg, "node", hwaddr);
		config.enabled = reg_get_dword(iface_reg, "enabled", true);
		
		reg_close(iface_reg);
		reg_close(reg);
		
		return config;
	}
	
	config.netnum  = default_net;
	config.nodenum = hwaddr;
	config.enabled = true;
	
	reg_close(reg);
	
	return config;
}

addr48_t get_primary_iface(void)
{
	addr48_t primary = addr48_in((unsigned char[]){ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF });
	
	HKEY reg      = reg_open_main(false);
	DWORD version = reg_get_dword(reg, "config_version", 1);
	
	if(version == 1)
	{
		/* TODO: Iterate... */
	}
	else if(version == 2)
	{
		primary = reg_get_addr48(reg, "primary", primary);
	}
	
	reg_close(reg);
	
	return primary;
}
