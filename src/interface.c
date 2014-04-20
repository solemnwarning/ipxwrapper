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
#include <pcap.h>

#include "interface.h"
#include "common.h"
#include "config.h"

#define INTERFACE_CACHE_TTL 5

BOOL ipx_use_pcap;

static CRITICAL_SECTION interface_cache_cs;

static ipx_interface_t *interface_cache = NULL;
static time_t interface_cache_ctime = 0;

/* Missing in MinGW... */
char *_strdup(const char *strSource);

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

/* Iterate over the addresses of an IP interface and append them to the IP list
 * of an IPX interface.
 * 
 * Returns false on memory allocation failure or other error.
*/
static bool _push_addr(ipx_interface_t *iface, IP_ADDR_STRING *ip)
{
	for(; ip; ip = ip->Next)
	{
		uint32_t ipaddr  = inet_addr(ip->IpAddress.String);
		uint32_t netmask = inet_addr(ip->IpMask.String);
		
		if(ipaddr == 0)
		{
			/* No IP address.
			 * Because an empty linked list would be silly.
			*/
			
			continue;
		}
		
		/* Workaround for point-to-point links.
		 * 
		 * A PPP interface has a netmask of 255.255.255.255 which is
		 * useless for calculating the correct broadcast address, so we
		 * instead iterate over the routing table looking for a route
		 * which encompasses the local IP address and copy the netmask
		 * from it.
		 * 
		 * The default route is ignored and the route with the smallest
		 * network is preferred.
		*/
		
		if(netmask == inet_addr("255.255.255.255"))
		{
			MIB_IPFORWARDTABLE *table = NULL;
			ULONG size = 2048;
			
			DWORD err = ERROR_INSUFFICIENT_BUFFER;
			
			while(err == ERROR_INSUFFICIENT_BUFFER)
			{
				if(!(table = realloc(table, size)))
				{
					log_printf(LOG_ERROR, "Couldn't allocate %u byte MIB_IPFORWARDTABLE buffer", (unsigned int)(size));
					return false;
				}
				
				err = GetIpForwardTable(table, &size, FALSE);
			}
			
			if(err != NO_ERROR)
			{
				log_printf(LOG_ERROR, "Error fetching routing table: %s", w32_error(err));
				free(table);
				
				return false;
			}
			
			DWORD n;
			for(n = 0; n < table->dwNumEntries; ++n)
			{
				uint32_t a_ip   = ntohl(ipaddr);
				uint32_t a_mask = ntohl(netmask);
				
				uint32_t r_net  = ntohl(table->table[n].dwForwardDest);
				uint32_t r_mask = ntohl(table->table[n].dwForwardMask);
				
				if(r_mask > 0 && r_mask < a_mask && (a_ip & r_mask) == r_net)
				{
					netmask = table->table[n].dwForwardMask;
				}
			}
			
			free(table);
		}
		
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
	}
	
	return true;
}

/* Load a list of virtual IPX interfaces. */
ipx_interface_t *load_ipx_interfaces(void)
{
	IP_ADAPTER_INFO *ifroot = load_sys_interfaces(), *ifptr;
	
	addr48_t primary = get_primary_iface();
	
	ipx_interface_t *nics = NULL;
	
	iface_config_t wc_config  = get_iface_config(WILDCARD_IFACE_HWADDR);
	ipx_interface_t *wc_iface = NULL;
	
	if(wc_config.enabled)
	{
		/* Initialise wildcard interface. */
		
		if(!(wc_iface = _new_iface(wc_config.netnum, wc_config.nodenum)))
		{
			return NULL;
		}
		
		DL_APPEND(nics, wc_iface);
	}
	
	for(ifptr = ifroot; ifptr; ifptr = ifptr->Next)
	{
		addr48_t hwaddr = addr48_in(ifptr->Address);
		
		iface_config_t config = get_iface_config(hwaddr);
		
		/* Append addresses to the wildcard interface... */
		
		if(wc_iface && !_push_addr(wc_iface, &(ifptr->IpAddressList)))
		{
			free_ipx_interface_list(&nics);
			return NULL;
		}
		
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
		
		if(hwaddr == primary)
		{
			/* Primary interface, insert at the start of the list */
			DL_PREPEND(nics, iface);
		}
		else{
			DL_APPEND(nics, iface);
		}
		
		/* Populate the virtual interface IP list. */
		
		if(!_push_addr(iface, &(ifptr->IpAddressList)))
		{
			free_ipx_interface_list(&nics);
			return NULL;
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
	dest->prev   = dest;
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

static void _init_pcap_interfaces(void)
{
	ipx_pcap_interface_t *pcap_interfaces = ipx_get_pcap_interfaces();
	
	log_printf(LOG_INFO, "Listing WinPcap interfaces:");
	log_printf(LOG_INFO, "--");
	
	for(ipx_pcap_interface_t *i = pcap_interfaces; i; i = i->next)
	{
		char hwaddr[ADDR48_STRING_SIZE];
		addr48_string(hwaddr, i->mac_addr);
		
		log_printf(LOG_INFO, "Name:        %s", i->name);
		log_printf(LOG_INFO, "Description: %s", i->desc);
		log_printf(LOG_INFO, "MAC Address: %s", hwaddr);
		log_printf(LOG_INFO, "--");
	}
	
	addr48_t primary = get_primary_iface();
	
	for(ipx_pcap_interface_t *i = pcap_interfaces; i; i = i->next)
	{
		iface_config_t config = get_iface_config(i->mac_addr);
		
		if(!config.enabled)
		{
			/* Interface has been disabled, don't add it */
			continue;
		}
		
		char errbuf[PCAP_ERRBUF_SIZE];
		pcap_t *pcap = pcap_open(i->name, ETHERNET_MTU, PCAP_OPENFLAG_MAX_RESPONSIVENESS, 0, NULL, errbuf);
		if(!pcap)
		{
			log_printf(LOG_ERROR, "Could not open WinPcap interface '%s': %s", i->name, errbuf);
			log_printf(LOG_WARNING, "This interface will not be available for IPX use");
			
			continue;
		}
		
		ipx_interface_t *iface = _new_iface(config.netnum, i->mac_addr);
		if(!iface)
		{
			pcap_close(pcap);
			continue;
		}
		
		iface->mac_addr = i->mac_addr;
		iface->pcap     = pcap;
		
		if(i->mac_addr == primary)
		{
			/* Primary interface, insert at the start of the list */
			DL_PREPEND(interface_cache, iface);
		}
		else{
			DL_APPEND(interface_cache, iface);
		}
	}
	
	ipx_free_pcap_interfaces(&pcap_interfaces);
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
	
	if(ipx_use_pcap)
	{
		_init_pcap_interfaces();
	}
	else{
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
	}
	
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
	
	if(ipx_use_pcap)
	{
		for(ipx_interface_t *i = interface_cache; i; i = i->next)
		{
			pcap_close(i->pcap);
		}
	}
	
	free_ipx_interface_list(&interface_cache);
}

/* Check the age of the IPX interface cache and reload it if necessary.
 * Ensure you hold interface_cache_cs before calling.
*/
static void renew_interface_cache(void)
{
	if(ipx_use_pcap)
	{
		/* interface_cache is initialised during init when pcap is in
		 * use and survives for the lifetime of the program.
		*/
		return;
	}
	
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

/* Search for an IPX interface by associated IP subnet.
 * Returns NULL if no interfaces match or on malloc failure.
*/
ipx_interface_t *ipx_interface_by_subnet(uint32_t ipaddr)
{
	EnterCriticalSection(&interface_cache_cs);
	
	renew_interface_cache();
	
	ipx_interface_t *iface;
	
	DL_FOREACH(interface_cache, iface)
	{
		ipx_interface_ip_t *ip;
		DL_FOREACH(iface->ipaddr, ip)
		{
			if((ip->ipaddr & ip->netmask) == (ipaddr & ip->netmask))
			{
				iface = copy_ipx_interface(iface);
				goto DONE;
			}
		}
	}
	
	DONE:
	
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

#define PCAP_NAME_PREFIX "rpcap://\\Device\\NPF_"

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
		if(strncmp(pcap_if->name, PCAP_NAME_PREFIX, strlen(PCAP_NAME_PREFIX)) == 0)
		{
			char *ifname = pcap_if->name + strlen(PCAP_NAME_PREFIX);
			
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
		else{
			log_printf(LOG_WARNING, "WinPcap interface with unexpected name format: '%s'", pcap_if->name);
			log_printf(LOG_WARNING, "This interface will not be available for IPX use");
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
