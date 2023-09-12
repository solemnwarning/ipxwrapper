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

#ifndef IPXWRAPPER_INTERFACE_H
#define IPXWRAPPER_INTERFACE_H

#include <iphlpapi.h>
#include <stdint.h>
#include <utlist.h>
#include <pcap.h>

#include "config.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: Dynamic MTU, per interface. */
#define ETHERNET_MTU 1500

#define WILDCARD_IFACE_HWADDR ({ \
	const unsigned char x[] = {0x00,0x00,0x00,0x00,0x00,0x00}; \
	addr48_in(x); \
})

typedef struct ipx_interface_ip ipx_interface_ip_t;

struct ipx_interface_ip {
	uint32_t ipaddr;
	uint32_t netmask;
	uint32_t bcast;
	
	ipx_interface_ip_t *prev;
	ipx_interface_ip_t *next;
};

typedef struct ipx_interface ipx_interface_t;

struct ipx_interface {
	addr32_t ipx_net;
	addr48_t ipx_node;
	
	ipx_interface_ip_t *ipaddr;
	
	addr48_t mac_addr;
	pcap_t *pcap;
	
	ipx_interface_t *prev;
	ipx_interface_t *next;
};

typedef struct ipx_pcap_interface ipx_pcap_interface_t;

struct ipx_pcap_interface {
	char *name;
	char *desc;
	
	addr48_t mac_addr;
	
	ipx_pcap_interface_t *prev;
	ipx_pcap_interface_t *next;
};

extern enum main_config_encap_type ipx_encap_type;

enum dosbox_state
{
	DOSBOX_DISCONNECTED,
	/* DOSBOX_RESOLVING, */
	DOSBOX_REGISTERING,
	DOSBOX_CONNECTED,
};

extern enum dosbox_state dosbox_state;
extern addr32_t dosbox_local_netnum;
extern addr48_t dosbox_local_nodenum;

IP_ADAPTER_INFO *load_sys_interfaces(void);
ipx_interface_t *load_ipx_interfaces(void);

ipx_interface_t *copy_ipx_interface(const ipx_interface_t *src);
void free_ipx_interface(ipx_interface_t *iface);

ipx_interface_t *copy_ipx_interface_list(const ipx_interface_t *src);
void free_ipx_interface_list(ipx_interface_t **list);

void ipx_interfaces_init(void);
void ipx_interfaces_cleanup(void);
void ipx_interfaces_reload(void);

ipx_interface_t *get_ipx_interfaces(void);
ipx_interface_t *ipx_interface_by_addr(addr32_t net, addr48_t node);
ipx_interface_t *ipx_interface_by_subnet(uint32_t ipaddr);
ipx_interface_t *ipx_interface_by_index(int index);
int ipx_interface_count(void);

ipx_pcap_interface_t *ipx_get_pcap_interfaces(void);
void ipx_free_pcap_interfaces(ipx_pcap_interface_t **interfaces);

ipx_interface_t *load_dosbox_interfaces(void);

#ifdef __cplusplus
}
#endif

#endif /* !IPXWRAPPER_INTERFACE_H */
