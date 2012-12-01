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
#include <time.h>

#include "interface.h"
#include "common.h"
#include "config.h"

#define INTERFACE_CACHE_TTL 5

static CRITICAL_SECTION interface_cache_cs;

static ipx_interface_t *interface_cache = NULL;
static time_t interface_cache_ctime = 0;

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
	
	return ifroot;
}

/* Allocate and initialise a new ipx_interface structure.
 * Returns NULL on malloc failure.
*/
static ipx_interface_t *_new_iface(addr32_t net, addr48_t node)
{
	ipx_interface_t *iface = malloc(sizeof(ipx_interface_t));
	if(!iface)
	{
		log_printf(LOG_ERROR, "Cannot allocate ipx_interface!");
	}
	
	memset(iface, 0, sizeof(*iface));
	
	iface->ipx_net  = net;
	iface->ipx_node = node;
	
	return iface;
}

/* Add an IP address to an ipx_interface structure.
 * Returns false on malloc failure.
*/
static bool _push_addr(ipx_interface_t *iface, uint32_t ipaddr, uint32_t netmask)
{
	ipx_interface_ip_t *addr = malloc(sizeof(ipx_interface_ip_t));
	if(!addr)
	{
		log_printf(LOG_ERROR, "Couldn't allocate ipx_interface_ip!");
		return false;
	}
	
	addr->ipaddr  = ipaddr;
	addr->netmask = netmask;
	addr->bcast   = ipaddr | (~netmask);
	
	DL_APPEND(iface->ipaddr, addr);
	
	return true;
}

/* Load a list of virtual IPX interfaces. */
ipx_interface_t *load_ipx_interfaces(void)
{
	IP_ADAPTER_INFO *ifroot = load_sys_interfaces(), *ifptr;
	
	addr48_t primary = get_primary_iface();
	
	ipx_interface_t *nics = NULL;
	
	iface_config_t wc_config = get_iface_config(WILDCARD_IFACE_HWADDR);
	
	if(wc_config.enabled)
	{
		/* Initialise wildcard interface. */
		
		ipx_interface_t *wc_iface = _new_iface(wc_config.netnum, wc_config.nodenum);
		if(!wc_iface)
		{
			return NULL;
		}
		
		/* Use 0.0.0.0/0 as the IP/network of the wildcard interface
		 * to broadcast to 255.255.255.255 and match packets from any
		 * address.
		*/
		
		if(!_push_addr(wc_iface, 0, 0))
		{
			free_ipx_interface(wc_iface);
			return NULL;
		}
		
		DL_APPEND(nics, wc_iface);
	}
	
	for(ifptr = ifroot; ifptr; ifptr = ifptr->Next)
	{
		addr48_t hwaddr = addr48_in(ifptr->Address);
		
		iface_config_t config = get_iface_config(hwaddr);
		
		if(!config.enabled)
		{
			/* Interface has been disabled, don't add it */
			continue;
		}
		
		ipx_interface_t *iface = _new_iface(config.netnum, config.nodenum);
		if(!iface)
		{
			free_ipx_interface_list(&nics);
			return NULL;
		}
		
		/* Iterate over the interface IP address list and add them to
		 * the ipx_interface structure.
		*/
		
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
			
			if(!_push_addr(iface, ipaddr, netmask))
			{
				free_ipx_interface(iface);
				free_ipx_interface_list(&nics);
				
				return NULL;
			}
		}
		
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
		
		if(hwaddr == primary)
		{
			/* Primary interface, insert at the start of the list */
			DL_PREPEND(nics, iface);
		}
		else{
			DL_APPEND(nics, iface);
		}
	}
	
	free(ifroot);
	
	return nics;
}

/* Deep copy an ipx_interface structure.
 * Returns NULL on malloc failure.
*/
ipx_interface_t *copy_ipx_interface(const ipx_interface_t *src)
{
	ipx_interface_t *dest = malloc(sizeof(ipx_interface_t));
	if(!dest)
	{
		log_printf(LOG_ERROR, "Cannot allocate ipx_interface!");
		return NULL;
	}
	
	*dest = *src;
	
	dest->ipaddr = NULL;
	dest->prev   = NULL;
	dest->next   = NULL;
	
	ipx_interface_ip_t *ip;
	
	DL_FOREACH(src->ipaddr, ip)
	{
		ipx_interface_ip_t *new_ip = malloc(sizeof(ipx_interface_ip_t));
		if(!new_ip)
		{
			log_printf(LOG_ERROR, "Cannot allocate ipx_interface_ip!");
			
			free_ipx_interface(dest);
			return NULL;
		}
		
		*new_ip = *ip;
		
		DL_APPEND(dest->ipaddr, new_ip);
	}
	
	return dest;
}

/* Free an ipx_interface structure and any memory allocated within. */
void free_ipx_interface(ipx_interface_t *iface)
{
	if(iface == NULL)
	{
		return;
	}
	
	ipx_interface_ip_t *a, *a_tmp;
	
	DL_FOREACH_SAFE(iface->ipaddr, a, a_tmp)
	{
		DL_DELETE(iface->ipaddr, a);
		free(a);
	}
	
	free(iface);
}

/* Deep copy an entire list of ipx_interface structures.
 * Returns NULL on malloc failure.
*/
ipx_interface_t *copy_ipx_interface_list(const ipx_interface_t *src)
{
	ipx_interface_t *dest = NULL;
	
	const ipx_interface_t *s;
	
	DL_FOREACH(src, s)
	{
		ipx_interface_t *d = copy_ipx_interface(s);
		if(!d)
		{
			free_ipx_interface_list(&dest);
			return NULL;
		}
		
		DL_APPEND(dest, d);
	}
	
	return dest;
}

/* Free a list of ipx_interface structures */
void free_ipx_interface_list(ipx_interface_t **list)
{
	ipx_interface_t *iface, *tmp;
	
	DL_FOREACH_SAFE(*list, iface, tmp)
	{
		DL_DELETE(*list, iface);
		free_ipx_interface(iface);
	}
}

/* Initialise the IPX interface cache. */
void ipx_interfaces_init(void)
{
	interface_cache       = NULL;
	interface_cache_ctime = 0;
	
	if(!InitializeCriticalSectionAndSpinCount(&interface_cache_cs, 0x80000000))
	{
		log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
		abort();
	}
	
	/* Dump the interface lists for debugging... */
	
	log_printf(LOG_INFO, "--");
	
	/* IP interfaces... */
	
	IP_ADAPTER_INFO *ip_ifaces = load_sys_interfaces(), *ip;
	
	log_printf(LOG_INFO, "Listing IP interfaces:");
	log_printf(LOG_INFO, "--");
	
	if(!ip_ifaces)
	{
		log_printf(LOG_INFO, "No IP interfaces detected!");
		log_printf(LOG_INFO, "--");
	}
	
	for(ip = ip_ifaces; ip; ip = ip->Next)
	{
		log_printf(LOG_INFO, "AdapterName:   %s", ip->AdapterName);
		log_printf(LOG_INFO, "Description:   %s", ip->Description);
		log_printf(LOG_INFO, "AddressLength: %u", (unsigned int)(ip->AddressLength));
		
		if(ip->AddressLength == 6)
		{
			char hwaddr[ADDR48_STRING_SIZE];
			addr48_string(hwaddr, addr48_in(ip->Address));
			
			log_printf(LOG_INFO, "Address:       %s", hwaddr);
		}
		
		log_printf(LOG_INFO, "Index:         %u", (unsigned int)(ip->Index));
		log_printf(LOG_INFO, "Type:          %u", (unsigned int)(ip->Type));
		log_printf(LOG_INFO, "DhcpEnabled:   %u", (unsigned int)(ip->DhcpEnabled));
		
		IP_ADDR_STRING *addr = &(ip->IpAddressList);
		
		for(; addr; addr = addr->Next)
		{
			log_printf(LOG_INFO, "IpAddress:     %s", addr->IpAddress.String);
			log_printf(LOG_INFO, "IpMask:        %s", addr->IpMask.String);
		}
		
		log_printf(LOG_INFO, "--");
	}
	
	free(ip_ifaces);
	
	/* Virtual IPX interfaces... */
	
	log_printf(LOG_INFO, "Listing IPX interfaces:");
	log_printf(LOG_INFO, "--");
	
	ipx_interface_t *ipx_root = get_ipx_interfaces(), *ipx;
	
	if(!ipx_root)
	{
		log_printf(LOG_INFO, "No IPX interfaces detected!");
		log_printf(LOG_INFO, "--");
	}
	
	DL_FOREACH(ipx_root, ipx)
	{
		char net[ADDR32_STRING_SIZE];
		addr32_string(net, ipx->ipx_net);
		
		char node[ADDR48_STRING_SIZE];
		addr48_string(node, ipx->ipx_node);
		
		log_printf(LOG_INFO, "Network:    %s", net);
		log_printf(LOG_INFO, "Node:       %s", node);
		
		ipx_interface_ip_t *ip;
		
		DL_FOREACH(ipx->ipaddr, ip)
		{
			log_printf(LOG_INFO, "IP address: %s", inet_ntoa(*((struct in_addr*)&(ip->ipaddr))));
			log_printf(LOG_INFO, "Netmask:    %s", inet_ntoa(*((struct in_addr*)&(ip->netmask))));
			log_printf(LOG_INFO, "Broadcast:  %s", inet_ntoa(*((struct in_addr*)&(ip->bcast))));
		}
		
		log_printf(LOG_INFO, "--");
	}
	
	free_ipx_interface_list(&ipx_root);
}

/* Release any resources used by the IPX interface cache. */
void ipx_interfaces_cleanup(void)
{
	DeleteCriticalSection(&interface_cache_cs);
	
	free_ipx_interface_list(&interface_cache);
}

/* Check the age of the IPX interface cache and reload it if necessary.
 * Ensure you hold interface_cache_cs before calling.
*/
static void renew_interface_cache(void)
{
	if(time(NULL) - interface_cache_ctime > INTERFACE_CACHE_TTL)
	{
		free_ipx_interface_list(&interface_cache);
		
		interface_cache       = load_ipx_interfaces();
		interface_cache_ctime = time(NULL);
	}
}

/* Return a copy of the IPX interface cache. The cache will be reloaded before
 * copying if too old.
*/
ipx_interface_t *get_ipx_interfaces(void)
{
	EnterCriticalSection(&interface_cache_cs);
	
	renew_interface_cache();
	
	ipx_interface_t *copy = copy_ipx_interface_list(interface_cache);
	
	LeaveCriticalSection(&interface_cache_cs);
	
	return copy;
}

/* Search for an IPX interface by address.
 * Returns NULL if the interface doesn't exist or malloc failure.
*/
ipx_interface_t *ipx_interface_by_addr(addr32_t net, addr48_t node)
{
	EnterCriticalSection(&interface_cache_cs);
	
	renew_interface_cache();
	
	ipx_interface_t *iface;
	
	DL_FOREACH(interface_cache, iface)
	{
		if(iface->ipx_net == net && iface->ipx_node == node)
		{
			iface = copy_ipx_interface(iface);
			break;
		}
	}
	
	LeaveCriticalSection(&interface_cache_cs);
	
	return iface;
}

/* Search for an IPX interface by index.
 * Returns NULL if the interface doesn't exist or malloc failure.
*/
ipx_interface_t *ipx_interface_by_index(int index)
{
	EnterCriticalSection(&interface_cache_cs);
	
	renew_interface_cache();
	
	int iface_index = 0;
	ipx_interface_t *iface;
	
	DL_FOREACH(interface_cache, iface)
	{
		if(iface_index++ == index)
		{
			iface = copy_ipx_interface(iface);
			break;
		}
	}
	
	LeaveCriticalSection(&interface_cache_cs);
	
	return iface;
}

/* Returns the number of IPX interfaces. */
int ipx_interface_count(void)
{
	EnterCriticalSection(&interface_cache_cs);
	
	renew_interface_cache();
	
	int count = 0;
	ipx_interface_t *iface;
	
	DL_FOREACH(interface_cache, iface)
	{
		count++;
	}
	
	LeaveCriticalSection(&interface_cache_cs);
	
	return count;
}
