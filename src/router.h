/* IPXWrapper - Router header
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

#ifndef IPXWRAPPER_ROUTER_H
#define IPXWRAPPER_ROUTER_H

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <stdint.h>

extern SOCKET shared_socket;
extern SOCKET private_socket;

extern struct sockaddr_in dosbox_server_addr;

void router_init(void);
void router_cleanup(void);

#endif /* !IPXWRAPPER_ROUTER_H */
