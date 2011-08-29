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

#include "interface.h"
#include "common.h"
#include "config.h"

/* Get virtual IPX interfaces
 * Select a single interface by setting ifnum >= 0
*/
struct ipx_interface *get_interfaces(int ifnum) {
	IP_ADAPTER_INFO *ifroot, tbuf;
	ULONG bufsize = sizeof(IP_ADAPTER_INFO);
	
	int err = GetAdaptersInfo(&tbuf, &bufsize);
	if(err == ERROR_NO_DATA) {
		log_printf("No network interfaces detected!");
		return NULL;
	}else if(err != ERROR_SUCCESS && err != ERROR_BUFFER_OVERFLOW) {
		log_printf("Error fetching network interfaces: %s", w32_error(err));
		return NULL;
	}
	
	if(!(ifroot = malloc(bufsize))) {
		log_printf("Out of memory! (Tried to allocate %u bytes)", (unsigned int)bufsize);
		return NULL;
	}
	
	err = GetAdaptersInfo(ifroot, &bufsize);
	if(err != ERROR_SUCCESS) {
		log_printf("Error fetching network interfaces: %s", w32_error(err));
		
		free(ifroot);
		return NULL;
	}
	
	struct ipx_interface *nics = NULL, *enic = NULL;
	
	IP_ADAPTER_INFO *ifptr = ifroot;
	int this_ifnum = 0;
	
	while(ifptr) {
		if(ifnum >= 0 && this_ifnum++ != ifnum) {
			ifptr = ifptr->Next;
			continue;
		}
		
		struct reg_value rv;
		int got_rv = 0;
		
		char vname[18];
		NODE_TO_STRING(vname, ifptr->Address);
		
		if(reg_get_bin(vname, &rv, sizeof(rv)) == sizeof(rv)) {
			got_rv = 1;
		}
		
		if(got_rv && !rv.enabled) {
			/* Interface has been disabled, don't add it */
			ifptr = ifptr->Next;
			continue;
		}
		
		struct ipx_interface *nnic = malloc(sizeof(struct ipx_interface));
		if(!nnic) {
			log_printf("Out of memory! (Tried to allocate %u bytes)", (unsigned int)sizeof(struct ipx_interface));
			
			free_interfaces(nics);
			return NULL;
		}
		
		nnic->ipaddr = inet_addr(ifptr->IpAddressList.IpAddress.String);
		nnic->netmask = inet_addr(ifptr->IpAddressList.IpMask.String);
		nnic->bcast = nnic->ipaddr | ~nnic->netmask;
		
		memcpy(nnic->hwaddr, ifptr->Address, 6);
		
		if(got_rv) {
			memcpy(nnic->ipx_net, rv.ipx_net, 4);
			memcpy(nnic->ipx_node, rv.ipx_node, 6);
		}else{
			unsigned char net[] = {0,0,0,1};
			
			memcpy(nnic->ipx_net, net, 4);
			memcpy(nnic->ipx_node, nnic->hwaddr, 6);
		}
		
		nnic->next = NULL;
		
		if(got_rv && rv.primary) {
			/* Force primary flag set, insert at start of NIC list */
			nnic->next = nics;
			nics = nnic;
			
			if(!enic) {
				enic = nnic;
			}
		}else if(enic) {
			enic->next = nnic;
			enic = nnic;
		}else{
			enic = nics = nnic;
		}
		
		ifptr = ifptr->Next;
	}
	
	free(ifroot);
	
	return nics;
}

void free_interfaces(struct ipx_interface *iface) {
	while(iface) {
		struct ipx_interface *del = iface;
		iface = iface->next;
		
		free(del);
	}
}
