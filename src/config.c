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
#include "interface.h"

main_config_t get_main_config(void)
{
	/* Defaults */
	
	main_config_t config;
	
	config.udp_port       = DEFAULT_PORT;
	config.w95_bug        = true;
	
	HKEY reg      = reg_open_main(false);
	DWORD version = reg_get_dword(reg, "config_version", 1);
	
	if(version == 1)
	{
		struct v1_global_config reg_config;
		
		if(reg_get_bin(reg, "global", &reg_config, sizeof(reg_config), NULL))
		{
			config.udp_port   = reg_config.udp_port;
			config.w95_bug    = reg_config.w95_bug;
		}
		
		config.log_level   = reg_get_dword(reg, "min_log_level", LOG_INFO);
	}
	else if(version == 2)
	{
		config.udp_port = reg_get_dword(reg, "port", config.udp_port);
		config.w95_bug  = reg_get_dword(reg, "w95_bug", config.w95_bug);
		
		config.log_level   = reg_get_dword(reg, "log_level", LOG_INFO);
	}
	
	reg_close(reg);
	
	return config;
}

bool set_main_config(const main_config_t *config)
{
	HKEY reg = reg_open_main(true);
	
	bool ok = reg_set_dword(reg, "port", config->udp_port)
		&& reg_set_dword(reg, "w95_bug", config->w95_bug)
		
		&& reg_set_dword(reg, "log_level", config->log_level)
		
		&& reg_set_dword(reg, "version", 2);
	
	reg_close(reg);
	
	return ok;
}

iface_config_t get_iface_config(addr48_t hwaddr)
{
	char id[18];
	addr48_string(id, hwaddr);
	
	iface_config_t config = {
		.netnum  = addr32_in((unsigned char[]){0x00, 0x00, 0x00, 0x01}),
		.nodenum = hwaddr,
		
		.enabled = true
	};
	
	HKEY reg   = reg_open_main(false);
	HKEY ifreg = reg_open_subkey(reg, id, false);
	
	if(ifreg)
	{
		config.netnum  = reg_get_addr32(ifreg, "net", config.netnum);
		config.nodenum = reg_get_addr48(ifreg, "node", config.nodenum);
		config.enabled = reg_get_dword(ifreg, "enabled", config.enabled);
	}
	else{
		struct v1_iface_config reg_config;
		
		if(reg_get_bin(reg, id, &reg_config, sizeof(reg_config), NULL))
		{
			config.netnum  = addr32_in(reg_config.ipx_net);
			config.nodenum = addr48_in(reg_config.ipx_node);
			config.enabled = reg_config.enabled;
		}
	}
	
	if(hwaddr == WILDCARD_IFACE_HWADDR && config.nodenum == hwaddr)
	{
		/* Generate a random node number for the wildcard interface and
		 * store it in the registry for persistence.
		*/
		
		config.nodenum = gen_random_mac();
		
		set_iface_config(hwaddr, &config);
	}
	
	reg_close(ifreg);
	reg_close(reg);
	
	return config;
}

bool set_iface_config(addr48_t hwaddr, const iface_config_t *config)
{
	char id[ADDR48_STRING_SIZE];
	addr48_string(id, hwaddr);
	
	HKEY reg   = reg_open_main(false);
	HKEY ifreg = reg_open_subkey(reg, id, true);
	
	bool ok = reg_set_addr32(ifreg, "net", config->netnum)
		&& reg_set_addr48(ifreg, "node", config->nodenum)
		&& reg_set_dword(ifreg, "enabled", config->enabled);
	
	reg_close(ifreg);
	reg_close(reg);
	
	return ok;
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
