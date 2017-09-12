/* IPXWrapper test suite
 * Copyright (C) 2017 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <winsock2.h>

#include <stdio.h>
#include <string.h>

#include "tests/tap/basic.h"
#include "src/ethernet.h"

#define CHECK_FRAME_SIZE(func, input, output) \
	is_int((output), func(input), #func "(" #input ") returns " #output)

#define UNPACK_GOOD_FRAME(func, desc, expect_ipx_off, expect_ipx_len, frame_len, ...) \
{ \
	const unsigned char FRAME[frame_len] = { __VA_ARGS__ }; \
	\
	const novell_ipx_packet *ipx; \
	size_t ipx_len; \
	\
	ok(func(&ipx, &ipx_len, FRAME, frame_len), #func "(<" desc ">) succeeds"); \
	ok((ipx == (FRAME + expect_ipx_off)),      #func "(<" desc ">) returns the correct payload address"); \
	is_int(expect_ipx_len, ipx_len,            #func "(<" desc ">) returns the correct payload length"); \
}

#define UNPACK_BAD_FRAME(func, desc, frame_len, ...) \
{ \
	const unsigned char FRAME[frame_len] = { __VA_ARGS__ }; \
	\
	const novell_ipx_packet *ipx; \
	size_t ipx_len; \
	\
	ok(!func(&ipx, &ipx_len, FRAME, frame_len), #func "(<" desc ">) fails"); \
}

int main()
{
	plan_lazy();
	
	/* +------------------+
	 * | ethII_frame_size |
	 * +------------------+
	*/
	
	CHECK_FRAME_SIZE(ethII_frame_size, 0,    44);
	CHECK_FRAME_SIZE(ethII_frame_size, 50,   94);
	CHECK_FRAME_SIZE(ethII_frame_size, 2000, 2044);
	
	/* +------------------+
	 * | ethII_frame_pack |
	 * +------------------+
	*/
	
	{
		uint8_t ptype = 0x42;
		
		addr32_t src_net    = addr32_in((unsigned char[]){ 0xDE, 0xAD, 0xBE, 0xEF });
		addr48_t src_node   = addr48_in((unsigned char[]){ 0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D });
		uint16_t src_socket = htons(1234);
		
		addr32_t dst_net    = addr32_in((unsigned char[]){ 0xBE, 0xEF, 0x0D, 0xAD });
		addr48_t dst_node   = addr48_in((unsigned char[]){ 0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00 });
		uint16_t dst_socket = htons(9876);
		
		static const char payload[] = { 0x00, 0xFF, 0x12, 0x34 };
		
		unsigned char buf[1024];
		ethII_frame_pack(&buf,
			ptype,
			src_net, src_node, src_socket,
			dst_net, dst_node, dst_socket,
			payload, sizeof(payload));
		
		static const unsigned char expect[] = {
			/* Ethernet header */
			0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
			0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
			0x81, 0x37,                         /* Ethertype */
			
			/* IPX header */
			0xFF, 0xFF,                         /* Checksum */
			0x00, 0x22,                         /* Length */
			0x00,                               /* Hops */
			0x42,                               /* Type */
			
			0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
			0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
			0x26, 0x94,                         /* Destination socket */
			
			0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
			0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
			0x04, 0xD2,                         /* Source socket */
			
			/* Payload */
			0x00, 0xFF, 0x12, 0x34,
		};
		
		is_blob(expect, buf, sizeof(expect), "ethII_frame_pack() serialises correctly");
	}
	
	/* +--------------------+
	 * | ethII_frame_unpack |
	 * +--------------------+
	*/
	
	/* Frame with smallest possible IPX packet (30 bytes) */
	UNPACK_GOOD_FRAME(ethII_frame_unpack,
		"frame with 30 byte packet",
		
		14, /* Offset of IPX packet */
		30, /* Length of IPX packet */
		
		/* Frame length */
		44,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x81, 0x37,                         /* Ethertype */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame with wrong Ethertype (valid raw frame) */
	UNPACK_BAD_FRAME(ethII_frame_unpack,
		"frame with wrong Ethertype",
		
		/* Frame length */
		44,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x1E,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame with 2000 byte IPX packet */
	UNPACK_GOOD_FRAME(ethII_frame_unpack,
		"frame with 2000 byte packet",
		
		14,   /* Offset of IPX packet */
		2000, /* Length of IPX packet */
		
		/* Frame length */
		2014,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x81, 0x37,                         /* Ethertype */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x07, 0xD0,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
		
		/* IPX payload (uninitialised) */
	);
	
	/* Frame too short to hold all the headers */
	UNPACK_BAD_FRAME(ethII_frame_unpack,
		"truncated frame - too short",
		
		/* Frame length */
		43,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x81, 0x37,                         /* Ethertype */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1D,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04,                               /* Source socket (truncated) */
	);
	
	/* +-------------------+
	 * | novell_frame_size |
	 * +-------------------+
	*/
	
	CHECK_FRAME_SIZE(novell_frame_size, 0,    44);
	CHECK_FRAME_SIZE(novell_frame_size, 50,   94);
	CHECK_FRAME_SIZE(novell_frame_size, 1470, 1514);
	CHECK_FRAME_SIZE(novell_frame_size, 1471, 0);
	
	/* +===================+
	 * | novell_frame_pack |
	 * +===================+
	*/
	
	{
		uint8_t ptype = 0x42;
		
		addr32_t src_net    = addr32_in((unsigned char[]){ 0xDE, 0xAD, 0xBE, 0xEF });
		addr48_t src_node   = addr48_in((unsigned char[]){ 0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D });
		uint16_t src_socket = htons(1234);
		
		addr32_t dst_net    = addr32_in((unsigned char[]){ 0xBE, 0xEF, 0x0D, 0xAD });
		addr48_t dst_node   = addr48_in((unsigned char[]){ 0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00 });
		uint16_t dst_socket = htons(9876);
		
		static const char payload[] = { 0x00, 0xFF, 0x12, 0x34 };
		
		unsigned char buf[1024];
		novell_frame_pack(&buf,
			ptype,
			src_net, src_node, src_socket,
			dst_net, dst_node, dst_socket,
			payload, sizeof(payload));
		
		static const unsigned char expect[] = {
			/* Ethernet header */
			0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
			0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
			0x00, 0x22,                         /* Payload length */
			
			/* IPX header */
			0xFF, 0xFF,                         /* Checksum */
			0x00, 0x22,                         /* Length */
			0x00,                               /* Hops */
			0x42,                               /* Type */
			
			0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
			0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
			0x26, 0x94,                         /* Destination socket */
			
			0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
			0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
			0x04, 0xD2,                         /* Source socket */
			
			/* Payload */
			0x00, 0xFF, 0x12, 0x34,
		};
		
		is_blob(expect, buf, sizeof(expect), "novell_frame_pack() serialises correctly");
	}
	
	/* +---------------------+
	 * | novell_frame_unpack |
	 * +---------------------+
	*/
	
	/* Frame with smallest possible IPX packet (30 bytes) */
	UNPACK_GOOD_FRAME(novell_frame_unpack,
		"frame with 30 byte packet",
		
		14, /* Offset of IPX packet */
		30, /* Length of IPX packet */
		
		/* Frame length */
		44,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x1E,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame with an Ethernet II Ethertype rather than a length */
	UNPACK_BAD_FRAME(novell_frame_unpack,
		"frame with 30 byte packet and an Ethertype",
		
		/* Frame length */
		44,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x81, 0x37,                         /* Ethertype */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame with largest allowable IPX packet (1500 bytes) */
	UNPACK_GOOD_FRAME(novell_frame_unpack,
		"frame with 1500 byte packet",
		
		14,   /* Offset of IPX packet */
		1500, /* Length of IPX packet */
		
		/* Frame length */
		1514,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x05, 0xDC,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x05, 0xDC,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
		
		/* IPX payload (uninitialised) */
	);
	
	/* Frame with 1501 length header (undefined behaviour) */
	UNPACK_BAD_FRAME(novell_frame_unpack,
		"frame with 1501 byte packet",
		
		/* Frame length */
		1515,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x05, 0xDD,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x05, 0xDD,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
		
		/* IPX payload (uninitialised) */
	);
	
	/* Valid IPX packet within, but Ethernet payload length is too short */
	UNPACK_BAD_FRAME(novell_frame_unpack,
		"frame with valid packet but 29 bytes in length header",
		
		/* Frame length */
		48,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x1D,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x22,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
		
		/* IPX payload */
		0x00, 0xFF, 0x12, 0x34,
	);
	
	/* Frame too short to hold all the headers */
	UNPACK_BAD_FRAME(novell_frame_unpack,
		"truncated frame - too short",
		
		/* Frame length */
		43,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x1D,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1D,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04,                               /* Source socket (truncated) */
	);
	
	/* Length runs past frame end */
	UNPACK_BAD_FRAME(novell_frame_unpack,
		"truncated frame - length runs past end",
		
		/* Frame length */
		44,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x1F,                         /* Payload length */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1F,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* +----------------+
	 * | llc_frame_size |
	 * +----------------+
	*/
	
	CHECK_FRAME_SIZE(llc_frame_size, 0,    47);
	CHECK_FRAME_SIZE(llc_frame_size, 50,   97);
	CHECK_FRAME_SIZE(llc_frame_size, 1467, 1514);
	CHECK_FRAME_SIZE(llc_frame_size, 1468, 0);
	
	/* +================+
	 * | llc_frame_pack |
	 * +================+
	*/
	
	{
		uint8_t ptype = 0x42;
		
		addr32_t src_net    = addr32_in((unsigned char[]){ 0xDE, 0xAD, 0xBE, 0xEF });
		addr48_t src_node   = addr48_in((unsigned char[]){ 0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D });
		uint16_t src_socket = htons(1234);
		
		addr32_t dst_net    = addr32_in((unsigned char[]){ 0xBE, 0xEF, 0x0D, 0xAD });
		addr48_t dst_node   = addr48_in((unsigned char[]){ 0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00 });
		uint16_t dst_socket = htons(9876);
		
		static const char payload[] = { 0x00, 0xFF, 0x12, 0x34 };
		
		unsigned char buf[1024];
		llc_frame_pack(&buf,
			ptype,
			src_net, src_node, src_socket,
			dst_net, dst_node, dst_socket,
			payload, sizeof(payload));
		
		static const unsigned char expect[] = {
			/* Ethernet header */
			0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
			0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
			0x00, 0x25,                         /* Payload length */
			
			/* LLC header */
			0xE0,                               /* DSAP */
			0xE0,                               /* SSAP */
			0x03,                               /* Control */
			
			/* IPX header */
			0xFF, 0xFF,                         /* Checksum */
			0x00, 0x22,                         /* Length */
			0x00,                               /* Hops */
			0x42,                               /* Type */
			
			0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
			0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
			0x26, 0x94,                         /* Destination socket */
			
			0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
			0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
			0x04, 0xD2,                         /* Source socket */
			
			/* Payload */
			0x00, 0xFF, 0x12, 0x34,
		};
		
		is_blob(expect, buf, sizeof(expect), "llc_frame_pack() serialises correctly");
	}
	
	/* +------------------+
	 * | llc_frame_unpack |
	 * +------------------+
	*/
	
	/* Frame with smallest possible IPX packet (30 bytes) */
	UNPACK_GOOD_FRAME(llc_frame_unpack,
		"frame with 30 byte packet",
		
		17, /* Offset of IPX packet */
		30, /* Length of IPX packet */
		
		/* Frame length */
		47,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x21,                         /* Payload length */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame with an Ethernet II Ethertype rather than a length */
	UNPACK_BAD_FRAME(llc_frame_unpack,
		"frame with 30 byte packet and an Ethertype",
		
		/* Frame length */
		47,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x81, 0x37,                         /* Ethertype */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame with largest allowable IPX packet (1497 bytes) */
	UNPACK_GOOD_FRAME(llc_frame_unpack,
		"frame with 1497 byte packet",
		
		17,   /* Offset of IPX packet */
		1497, /* Length of IPX packet */
		
		/* Frame length */
		1514,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x05, 0xDC,                         /* Payload length */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x05, 0xD9,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
		
		/* IPX payload (uninitialised) */
	);
	
	/* Frame with 1501 length header (undefined behaviour) */
	UNPACK_BAD_FRAME(llc_frame_unpack,
		"frame with 1498 byte packet",
		
		/* Frame length */
		1515,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x05, 0xDD,                         /* Payload length */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x05, 0xDA,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
		
		/* IPX payload (uninitialised) */
	);
	
	/* Valid IPX packet within, but Ethernet payload length is too short */
	UNPACK_BAD_FRAME(llc_frame_unpack,
		"frame with valid packet but 32 bytes in length header",
		
		/* Frame length */
		47,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x20,                         /* Payload length */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Frame too short to hold all the headers */
	UNPACK_BAD_FRAME(llc_frame_unpack,
		"truncated frame - too short",
		
		/* Frame length */
		46,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x20,                         /* Payload length */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1D,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04,                               /* Source socket (truncated) */
	);
	
	/* Length runs past frame end */
	UNPACK_BAD_FRAME(llc_frame_unpack,
		"truncated frame - length runs past end",
		
		/* Frame length */
		47,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x22,                         /* Payload length */
		
		/* LLC header */
		0xE0,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1F,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	/* Wrong DSAP */
	UNPACK_BAD_FRAME(llc_frame_unpack,
		"frame with wrong DSAP",
		
		/* Frame length */
		47,
		
		/* Ethernet header */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination MAC */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source MAC */
		0x00, 0x21,                         /* Payload length */
		
		/* LLC header */
		0xE1,                               /* DSAP */
		0xE0,                               /* SSAP */
		0x03,                               /* Control */
		
		/* IPX header */
		0xFF, 0xFF,                         /* Checksum */
		0x00, 0x1E,                         /* Length */
		0x00,                               /* Hops */
		0x42,                               /* Type */
		
		0xBE, 0xEF, 0x0D, 0xAD,             /* Destination network */
		0x99, 0xB0, 0x77, 0x1E, 0x50, 0x00, /* Destination node */
		0x26, 0x94,                         /* Destination socket */
		
		0xDE, 0xAD, 0xBE, 0xEF,             /* Source network */
		0x0B, 0xAD, 0x0B, 0xEE, 0xF0, 0x0D, /* Source node */
		0x04, 0xD2,                         /* Source socket */
	);
	
	return 0;
}
