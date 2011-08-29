/* IPXWrapper - Common functions
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

#include "common.h"
#include "config.h"

HKEY regkey = NULL;

/* Convert a windows error number to an error message */
const char *w32_error(DWORD errnum) {
	static char buf[1024] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, 1023, NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
}

BOOL reg_open(REGSAM access) {
	int err = RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\IPXWrapper", 0, access, &regkey);
	
	if(err != ERROR_SUCCESS) {
		log_printf("Could not open registry: %s", w32_error(err));
		regkey = NULL;
		
		return FALSE;
	}
	
	return TRUE;
}

void reg_close(void) {
	if(regkey) {
		RegCloseKey(regkey);
		regkey = NULL;
	}
}

char reg_get_char(const char *val_name, char default_val) {
	if(!regkey) {
		return default_val;
	}
	
	char buf;
	DWORD size = 1;
	
	int err = RegQueryValueEx(regkey, val_name, NULL, NULL, (BYTE*)&buf, &size);
	
	if(err != ERROR_SUCCESS) {
		log_printf("Error reading registry value: %s", w32_error(err));
		return default_val;
	}
	
	return size == 1 ? buf : default_val;
}

DWORD reg_get_bin(const char *val_name, void *buf, DWORD size) {
	if(!regkey) {
		return 0;
	}
	
	int err = RegQueryValueEx(regkey, val_name, NULL, NULL, (BYTE*)buf, &size);
	
	if(err != ERROR_SUCCESS) {
		log_printf("Error reading registry value: %s", w32_error(err));
		return 0;
	}
	
	return size;
}

/* Load a system DLL */
HMODULE load_sysdll(const char *name) {
	char path[1024];
	
	GetSystemDirectory(path, sizeof(path));
	
	if(strlen(path) + strlen(name) + 2 > sizeof(path)) {
		log_printf("Path buffer too small, cannot load %s", name);
		return NULL;
	}
	
	strcat(path, "\\");
	strcat(path, name);
	
	HMODULE dll = LoadLibrary(path);
	if(!dll) {
		log_printf("Error loading %s: %s", path, w32_error(GetLastError()));
	}
	
	return dll;
}

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
