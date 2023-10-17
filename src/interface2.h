/* IPXWrapper - Interface header
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

#ifndef IPXWRAPPER_INTERFACE2_H
#define IPXWRAPPER_INTERFACE2_H

#include <iphlpapi.h>
#include <stdint.h>
#include <utlist.h>
#include <pcap.h>

#include "config.h"
#include "common.h"

#define WILDCARD_IFACE_HWADDR ({ \
	const unsigned char x[] = {0x00,0x00,0x00,0x00,0x00,0x00}; \
	addr48_in(x); \
})

typedef struct ipx_pcap_interface ipx_pcap_interface_t;

struct ipx_pcap_interface {
	char *name;
	char *desc;
	
	addr48_t mac_addr;
	
	ipx_pcap_interface_t *prev;
	ipx_pcap_interface_t *next;
};

#ifdef __cplusplus
extern "C" {
#endif

IP_ADAPTER_INFO *load_sys_interfaces(void);

ipx_pcap_interface_t *ipx_get_pcap_interfaces(void);
void ipx_free_pcap_interfaces(ipx_pcap_interface_t **interfaces);

#ifdef __cplusplus
}
#endif

#endif /* !IPXWRAPPER_INTERFACE2_H */
