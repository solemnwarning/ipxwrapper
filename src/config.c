/* ipxwrapper - Configuration header
 * Copyright (C) 2011-2013 Daniel Collins <solemnwarning@solemnwarning.net>
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
	
	config.udp_port   = DEFAULT_PORT;
	config.w95_bug    = true;
	config.fw_except  = false;
	config.use_pcap   = false;
	config.frame_type = FRAME_TYPE_ETH_II;
	config.log_level  = LOG_INFO;
	
	HKEY reg = reg_open_main(false);
	
	/* Load pre-0.4.x "global" config structure and values. */
	
	struct v1_global_config reg_config;
	
	if(reg_get_bin(reg, "global", &reg_config, sizeof(reg_config), NULL))
	{
		config.udp_port = reg_config.udp_port;
		config.w95_bug  = reg_config.w95_bug;
	}
	
	config.log_level = reg_get_dword(reg, "min_log_level", config.log_level);
	
	/* Overlay with any 0.4.x config values. */
	
	config.udp_port   = reg_get_dword(reg, "port",       config.udp_port);
	config.w95_bug    = reg_get_dword(reg, "w95_bug",    config.w95_bug);
	config.fw_except  = reg_get_dword(reg, "fw_except",  config.fw_except);
	config.use_pcap   = reg_get_dword(reg, "use_pcap",   config.use_pcap);
	config.frame_type = reg_get_dword(reg, "frame_type", config.frame_type);
	config.log_level  = reg_get_dword(reg, "log_level",  config.log_level);
	
	/* Check for valid frame_type */
	
	if(        config.frame_type != FRAME_TYPE_ETH_II
		&& config.frame_type != FRAME_TYPE_NOVELL
		&& config.frame_type != FRAME_TYPE_LLC)
	{
		
		log_printf(LOG_WARNING, "Ignoring unknown frame_type %u",
			(unsigned int)(config.frame_type));
		
		config.frame_type = FRAME_TYPE_ETH_II;
	}
	
	reg_close(reg);
	
	return config;
}

bool set_main_config(const main_config_t *config)
{
	HKEY reg = reg_open_main(true);
	
	bool ok = reg_set_dword(reg,  "port",       config->udp_port)
		&& reg_set_dword(reg, "w95_bug",    config->w95_bug)
		&& reg_set_dword(reg, "fw_except",  config->fw_except)
		&& reg_set_dword(reg, "use_pcap",   config->use_pcap)
		&& reg_set_dword(reg, "frame_type", config->frame_type)
		&& reg_set_dword(reg, "log_level",  config->log_level);
	
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
	
	HKEY reg   = reg_open_main(true);
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
	
	HKEY reg = reg_open_main(false);
	
	if(reg_check_value(reg, "primary"))
	{
		/* Post-0.4.x */
		
		primary = reg_get_addr48(reg, "primary", primary);
	}
	else if(reg)
	{
		/* Iterate over any pre-0.4.x interface config values and return
		 * the node number of the first one with the primary flag set.
		*/
		
		int index = 0;
		
		while(1)
		{
			char name[24];
			DWORD name_size = sizeof(name);
			
			struct v1_iface_config config;
			DWORD config_size = sizeof(config);
			
			int err = RegEnumValue(reg, index++, name, &name_size, NULL, NULL, (BYTE*)&config, &config_size);
			
			if(err == ERROR_SUCCESS)
			{
				addr48_t tmp;
				
				if(
					config_size == sizeof(config)
					&& config.primary
					&& addr48_from_string(&tmp, name)
				) {
					primary = addr48_in(config.ipx_node);
					break;
				}
			}
			else if(err == ERROR_NO_MORE_ITEMS)
			{
				break;
			}
			else{
				log_printf(LOG_ERROR, "Error enumerating registry value: %s", w32_error(err));
			}
		}
	}
	
	reg_close(reg);
	
	return primary;
}

bool set_primary_iface(addr48_t primary)
{
	HKEY reg = reg_open_main(true);
	
	bool ok = reg_set_addr48(reg, "primary", primary);
	
	reg_close(reg);
	
	return ok;
}
