/* IPXWrapper - Packet coalescing
 * Copyright (C) 2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef IPXWRAPPER_COALESCE_H
#define IPXWRAPPER_COALESCE_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "addr.h"

/* For each destination IPX address, track the timestamp of the past n send
 * operations, we use this to determine how spammy the application is being
 * with sendto() calls.
*/
#define IPXWRAPPER_COALESCE_PACKET_TRACK_COUNT 512

/* Start coalescing when the rate of send operations to a single IPX address
 * reaches IPXWRAPPER_COALESCE_PACKET_TRACK_COUNT packets over the past
 * IPXWRAPPER_COALESCE_PACKET_START_THRESH microseconds.
*/
#define IPXWRAPPER_COALESCE_PACKET_START_THRESH 2500000 /* 2.5s */

/* Stop coalescing when the rate of send operations to a single IPX address
 * falls back under IPXWRAPPER_COALESCE_PACKET_TRACK_COUNT over the past
 * IPXWRAPPER_COALESCE_PACKET_STOP_THRESH microseconds.
*/
#define IPXWRAPPER_COALESCE_PACKET_STOP_THRESH 10000000 /* 10s */

/* Delay the transmission of a packet for coalescing by no more than
 * IPXWRAPPER_COALESCE_PACKET_MAX_DELAY microseconds.
*/
#define IPXWRAPPER_COALESCE_PACKET_MAX_DELAY 20000 /* 20ms */

/* Combine outgoing IPX packets up to IPXWRAPPER_COALESCE_PACKET_MAX_SIZE
 * bytes of data in the UDP payload.
*/
#define IPXWRAPPER_COALESCE_PACKET_MAX_SIZE 1384

DWORD coalesce_send(const void *data, size_t data_size, addr32_t dest_net, addr48_t dest_node, uint16_t dest_socket);
void coalesce_flush_waiting(void);
void coalesce_cleanup(void);

#endif /* !IPXWRAPPER_COALESCE_H */
