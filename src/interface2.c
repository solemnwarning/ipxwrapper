/* IPXWrapper - Interface functions
 * Copyright (C) 2011-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define WINSOCK_API_LINKAGE

#include <windows.h>
#include <iphlpapi.h>
#include <utlist.h>
#include <time.h>
#include <pcap.h>

#include "interface2.h"
#include "ipxwrapper.h"
#include "common.h"
#include "config.h"

/* Fetch a list of network interfaces available on the system.
 *
 * Returns a linked list of IP_ADAPTER_INFO structures, all allocated within a
 * single memory block beginning at the first node.
*/
IP_ADAPTER_INFO *load_sys_interfaces(void)
{
	IP_ADAPTER_INFO *ifroot = NULL, *ifptr;
	ULONG bufsize = sizeof(IP_ADAPTER_INFO) * 8;
	
	int err = ERROR_BUFFER_OVERFLOW;
	
	while(err == ERROR_BUFFER_OVERFLOW)
	{
		if(!(ifptr = realloc(ifroot, bufsize)))
		{
			log_printf(LOG_ERROR, "Couldn't allocate IP_ADAPTER_INFO structures!");
			break;
		}
		
		ifroot = ifptr;
		
		err = GetAdaptersInfo(ifroot, &bufsize);
		
		if(err == ERROR_NO_DATA)
		{
			log_printf(LOG_WARNING, "No network interfaces detected!");
			break;
		}
		else if(err != ERROR_SUCCESS && err != ERROR_BUFFER_OVERFLOW)
		{
			log_printf(LOG_ERROR, "Error fetching network interfaces: %s", w32_error(err));
			break;
		}
	}
	
	if(err != ERROR_SUCCESS)
	{
		free(ifroot);
		return NULL;
	}
	
	for(ifptr = ifroot; ifptr; ifptr = ifptr->Next)
	{
		/* Workaround for buggy versions of Hamachi that don't initialise
		 * the interface hardware address correctly.
		*/
		
		unsigned char hamachi_bug[] = {0x7A, 0x79, 0x00, 0x00, 0x00, 0x00};
		
		if(ifptr->AddressLength == 6 && memcmp(ifptr->Address, hamachi_bug, 6) == 0)
		{
			uint32_t ipaddr = inet_addr(ifptr->IpAddressList.IpAddress.String);
			
			if(ipaddr)
			{
				log_printf(LOG_WARNING, "Invalid Hamachi interface detected, correcting node number");
				memcpy(ifptr->Address + 2, &ipaddr, sizeof(ipaddr));
			}
		}
	}
	
	return ifroot;
}

#define PCAP_NAME_PREFIX_OLD "rpcap://\\Device\\NPF_"
#define PCAP_NAME_PREFIX_NEW "rpcap://"

ipx_pcap_interface_t *ipx_get_pcap_interfaces(void)
{
	ipx_pcap_interface_t *ret_interfaces = NULL;
	
	IP_ADAPTER_INFO *ip_interfaces = load_sys_interfaces();
	
	pcap_if_t *pcap_interfaces;
	char errbuf[PCAP_ERRBUF_SIZE];
	if(pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &pcap_interfaces, errbuf) == -1)
	{
		log_printf(LOG_ERROR, "Could not obtain list of WinPcap interfaces: %s", errbuf);
		
		free(ip_interfaces);
		return NULL;
	}
	
	for(pcap_if_t *pcap_if = pcap_interfaces; pcap_if; pcap_if = pcap_if->next)
	{
		const char *ifname = NULL;
		
		if(strncmp(pcap_if->name, PCAP_NAME_PREFIX_OLD, strlen(PCAP_NAME_PREFIX_OLD)) == 0)
		{
			ifname = pcap_if->name + strlen(PCAP_NAME_PREFIX_OLD);
		}
		else if(strncmp(pcap_if->name, PCAP_NAME_PREFIX_NEW, strlen(PCAP_NAME_PREFIX_NEW)) == 0)
		{
			ifname = pcap_if->name + strlen(PCAP_NAME_PREFIX_NEW);
		}
		else{
			log_printf(LOG_WARNING, "WinPcap interface with unexpected name format: '%s'", pcap_if->name);
			log_printf(LOG_WARNING, "This interface will not be available for IPX use");
		}
		
		if(ifname != NULL)
		{
			IP_ADAPTER_INFO *ip_if = ip_interfaces;
			while(ip_if && strcmp(ip_if->AdapterName, ifname))
			{
				ip_if = ip_if->Next;
			}
			
			if(ip_if && ip_if->AddressLength == 6)
			{
				ipx_pcap_interface_t *new_if = malloc(sizeof(ipx_pcap_interface_t));
				if(!new_if)
				{
					log_printf(LOG_ERROR, "Could not allocate memory!");
					continue;
				}
				
				new_if->name = strdup(pcap_if->name);
				new_if->desc = strdup(pcap_if->description
					? pcap_if->description
					: pcap_if->name);
				
				if(!new_if->name || !new_if->desc)
				{
					free(new_if->name);
					free(new_if->desc);
					free(new_if);
					
					log_printf(LOG_ERROR, "Could not allocate memory!");
					continue;
				}
				
				new_if->mac_addr = addr48_in(ip_if->Address);
				
				DL_APPEND(ret_interfaces, new_if);
			}
			else{
				log_printf(LOG_WARNING, "Could not determine MAC address of WinPcap interface '%s'", pcap_if->name);
				log_printf(LOG_WARNING, "This interface will not be available for IPX use");
			}
		}
	}
	
	pcap_freealldevs(pcap_interfaces);
	free(ip_interfaces);
	
	return ret_interfaces;
}

void ipx_free_pcap_interfaces(ipx_pcap_interface_t **interfaces)
{
	ipx_pcap_interface_t *p, *p_tmp;
	DL_FOREACH_SAFE(*interfaces, p, p_tmp)
	{
		DL_DELETE(*interfaces, p);
		
		free(p->name);
		free(p->desc);
		free(p);
	}
}
