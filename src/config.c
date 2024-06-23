/* ipxwrapper - Configuration header
 * Copyright (C) 2011-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include "../inih/ini.h"

#include "config.h"
#include "common.h"
#include "interface.h"

static int process_ini_directive(void *context, const char *section, const char *name, const char *value, int lineno);

main_config_t get_main_config(bool ignore_ini)
{
	/* Defaults */
	
	main_config_t config;
	
	config.udp_port   = DEFAULT_PORT;
	config.w95_bug    = true;
	config.fw_except  = false;
	config.encap_type = ENCAP_TYPE_IPXWRAPPER;
	config.frame_type = FRAME_TYPE_ETH_II;
	config.log_level  = LOG_INFO;
	config.profile    = false;
	
	config.dosbox_server_addr = NULL;
	config.dosbox_server_port = 213;
	config.dosbox_coalesce = false;
	
	if(!ignore_ini)
	{
		wchar_t *ini_path = get_module_relative_path(NULL, L"ipxwrapper.ini");
		
		FILE *ini_file = _wfopen(ini_path, L"r");
		if(ini_file != NULL)
		{
			int ini_result = ini_parse_file(ini_file, &process_ini_directive, &config);
			
			if(ini_result > 0)
			{
				log_printf(LOG_ERROR, "Parse error in ipxwrapper.ini at line %d", ini_result);
			}
			
			/* Check log_level here because min_log_level isn't initialised yet. */
			if(config.log_level <= LOG_INFO)
			{
				log_printf(LOG_INFO, "Loaded configuration from %S", ini_path);
			}
			
			fclose(ini_file);
			free(ini_path);
			
			return config;
		}
		
		free(ini_path);
	}
	
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
	config.encap_type = reg_get_dword(reg, "use_pcap",   config.encap_type);
	config.frame_type = reg_get_dword(reg, "frame_type", config.frame_type);
	config.log_level  = reg_get_dword(reg, "log_level",  config.log_level);
	config.profile    = reg_get_dword(reg, "profile",    config.profile);
	
	config.dosbox_server_addr = reg_get_string(reg, "dosbox_server_addr", "");
	config.dosbox_server_port = reg_get_dword(reg, "dosbox_server_port", config.dosbox_server_port);
	config.dosbox_coalesce    = reg_get_dword(reg, "dosbox_coalesce", config.dosbox_coalesce);
	
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

static int process_ini_directive(void *context, const char *section, const char *name, const char *value, int lineno)
{
	main_config_t *config = (main_config_t*)(context);
	
	if(strcmp(section, "") != 0)
	{
		log_printf(LOG_ERROR, "Ignoring directive in unknown ipxwrapper.ini section \"%s\"", section);
		return 1;
	}
	
	if(strcmp(name, "dosbox server address") == 0)
	{
		free(config->dosbox_server_addr);
		config->dosbox_server_addr = NULL;
		
		config->dosbox_server_addr = strdup(value);
		
		if(config->dosbox_server_addr != NULL)
		{
			config->encap_type = ENCAP_TYPE_DOSBOX;
		}
	}
	else if(strcmp(name, "dosbox server port") == 0)
	{
		int port = atoi(value);
		
		if(port >= 1 && port <= 65535)
		{
			config->dosbox_server_port = port;
		}
		else{
			log_printf(LOG_ERROR, "Invalid \"dosbox server port\" (%s) specified in ipxwrapper.ini", value);
		}
	}
	else if(strcmp(name, "coalesce packets") == 0)
	{
		if(strcmp(value, "yes") == 0)
		{
			config->dosbox_coalesce = true;
		}
		else if(strcmp(value, "no") == 0)
		{
			config->dosbox_coalesce = false;
		}
		else{
			log_printf(LOG_ERROR, "Invalid \"coalesce packets\" (%s) specified in ipxwrapper.ini (expected \"yes\" or \"no\")", value);
		}
	}
	else if(strcmp(name, "firewall exception") == 0)
	{
		if(strcmp(value, "yes") == 0)
		{
			config->fw_except = true;
		}
		else if(strcmp(value, "no") == 0)
		{
			config->fw_except = false;
		}
		else{
			log_printf(LOG_ERROR, "Invalid \"firewall exception\" (%s) specified in ipxwrapper.ini (expected \"yes\" or \"no\")", value);
		}
	}
	else if(strcmp(name, "logging") == 0)
	{
		if(strcmp(value, "none") == 0)
		{
			config->log_level = LOG_DISABLED;
		}
		else if(strcmp(value, "info") == 0)
		{
			config->log_level = LOG_INFO;
		}
		else if(strcmp(value, "debug") == 0)
		{
			config->log_level = LOG_DEBUG;
		}
		else if(strcmp(value, "trace") == 0)
		{
			config->log_level = LOG_CALL;
		}
		else{
			log_printf(LOG_ERROR, "Invalid \"logging\" (%s) specified in ipxwrapper.ini (expected \"none\", \"info\", \"debug\" or \"trace\")", value);
		}
	}
	else{
		log_printf(LOG_ERROR, "Unknown directive \"%s\" in ipxwrapper.ini", name);
	}
	
	return 1;
}

bool set_main_config(const main_config_t *config)
{
	HKEY reg = reg_open_main(true);
	
	bool ok = reg_set_dword(reg,  "port",       config->udp_port)
		&& reg_set_dword(reg, "w95_bug",    config->w95_bug)
		&& reg_set_dword(reg, "fw_except",  config->fw_except)
		&& reg_set_dword(reg, "use_pcap",   config->encap_type)
		&& reg_set_dword(reg, "frame_type", config->frame_type)
		&& reg_set_dword(reg, "log_level",  config->log_level)
		&& reg_set_dword(reg, "profile",    config->profile)
		
		&& reg_set_string(reg, "dosbox_server_addr", config->dosbox_server_addr)
		&& reg_set_dword(reg,  "dosbox_server_port", config->dosbox_server_port)
		&& reg_set_dword(reg,  "dosbox_coalesce",    config->dosbox_coalesce);
	
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
