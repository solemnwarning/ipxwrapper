/* IPXWrapper - Interface functions
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

#include <windows.h>
#include <iphlpapi.h>
#include <utlist.h>

#include "interface.h"
#include "common.h"
#include "config.h"

/* Fetch a list of network interfaces available on the system.
 *
 * Returns a linked list of IP_ADAPTER_INFO structures, all allocated within a
 * single memory block beginning at the first node.
*/
IP_ADAPTER_INFO *get_sys_interfaces(void)
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
	
	return ifroot;
}

/* Get virtual IPX interfaces
 * Select a single interface by setting ifnum >= 0
*/
ipx_interface_t *get_interfaces(int ifnum)
{
	IP_ADAPTER_INFO *ifroot = get_sys_interfaces(), *ifptr;
	
	addr48_t primary = get_primary_iface();
	
	ipx_interface_t *nics = NULL;
	
	for(ifptr = ifroot; ifptr; ifptr = ifptr->Next)
	{
		addr48_t hwaddr = addr48_in(ifptr->Address);
		
		iface_config_t config = get_iface_config(hwaddr);
		
		if(!config.enabled)
		{
			/* Interface has been disabled, don't add it */
			
			ifptr = ifptr->Next;
			continue;
		}
		
		ipx_interface_t *iface = malloc(sizeof(ipx_interface_t));
		if(!iface)
		{
			log_printf(LOG_ERROR, "Couldn't allocate ipx_interface!");
			
			free_ipx_interfaces(&nics);
			return NULL;
		}
		
		iface->ipaddr = NULL;
		
		IP_ADDR_STRING *ip_ptr = &(ifptr->IpAddressList);
		
		for(; ip_ptr; ip_ptr = ip_ptr->Next)
		{
			uint32_t ipaddr  = inet_addr(ip_ptr->IpAddress.String);
			uint32_t netmask = inet_addr(ip_ptr->IpMask.String);
			
			if(ipaddr == 0)
			{
				/* No IP address.
				 * Because an empty linked list would be silly.
				*/
				
				continue;
			}
			
			ipx_interface_ip_t *addr = malloc(sizeof(ipx_interface_ip_t));
			if(!addr)
			{
				log_printf(LOG_ERROR, "Couldn't allocate ipx_interface_ip!");
				
				free_ipx_interface(iface);
				free_ipx_interfaces(&nics);
				
				continue;
			}
			
			addr->ipaddr  = ipaddr;
			addr->netmask = netmask;
			addr->bcast   = ipaddr | (~netmask);
			
			DL_APPEND(iface->ipaddr, addr);
		}
		
		iface->hwaddr = hwaddr;
		
		iface->ipx_net  = config.netnum;
		iface->ipx_node = config.nodenum;
		
		/* Workaround for buggy versions of Hamachi that don't initialise
		 * the interface hardware address correctly.
		*/
		
		unsigned char hamachi_bug[] = {0x7A, 0x79, 0x00, 0x00, 0x00, 0x00};
		
		if(iface->ipx_node == addr48_in(hamachi_bug) && iface->ipaddr)
		{
			log_printf(LOG_WARNING, "Invalid Hamachi interface detected, correcting node number");
			
			addr32_out(hamachi_bug + 2, iface->ipaddr->ipaddr);
			iface->ipx_node = addr48_in(hamachi_bug);
		}
		
		if(iface->hwaddr == primary)
		{
			/* Primary interface, insert at the start of the list */
			DL_PREPEND(nics, iface);
		}
		else{
			DL_APPEND(nics, iface);
		}
	}
	
	free(ifroot);
	
	/* Delete every entry in the NIC list except the requested one.
	 *
	 * This is done here rather than when building the list as the primary
	 * interface may change the indexes if it isn't the first.
	*/
	
	if(ifnum >= 0)
	{
		int this_ifnum = 0;
		ipx_interface_t *iface, *tmp;
		
		DL_FOREACH_SAFE(nics, iface, tmp)
		{
			if(this_ifnum++ != ifnum)
			{
				DL_DELETE(nics, iface);
				free_ipx_interface(iface);
			}
		}
	}
	
	return nics;
}

/* Free an ipx_interface structure and any memory allocated within. */
void free_ipx_interface(ipx_interface_t *iface)
{
	ipx_interface_ip_t *a, *a_tmp;
	
	DL_FOREACH_SAFE(iface->ipaddr, a, a_tmp)
	{
		DL_DELETE(iface->ipaddr, a);
		free(a);
	}
	
	free(iface);
}

/* Free a list of ipx_interface structures */
void free_ipx_interfaces(ipx_interface_t **list)
{
	ipx_interface_t *iface, *tmp;
	
	DL_FOREACH_SAFE(*list, iface, tmp)
	{
		DL_DELETE(*list, iface);
		free_ipx_interface(iface);
	}
}
