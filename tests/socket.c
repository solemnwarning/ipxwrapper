/* IPXWrapper - Unit tests
 * Copyright (C) 2014 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <winsock2.h>
#include <wsipx.h>
#include <wsnwlink.h>
#include <stdio.h>
#include <assert.h>

#define FAIL(fmt, ...) \
{ \
	fprintf(stderr, "Failure at %s:%d: " fmt "\n", __FILE__, __LINE__, ## __VA_ARGS__); \
	exit(1); \
}

#define TRY_IPX_SOCKET(family, type, proto, expect_ptype) \
{ \
	int sock = socket(family, type, proto); \
	if(sock == -1) \
	{ \
		FAIL("Couldn't create IPX socket, WSAGetLastError() = %d", (int)(WSAGetLastError())); \
	} \
	int got_ptype = 0, len = sizeof(int); \
	if(getsockopt(sock, NSPROTO_IPX, IPX_PTYPE, (char*)(&got_ptype), &len) != 0) \
	{ \
		FAIL("Couldn't get IPX_PTYPE of socket, WSAGetLastError() = %d", (int)(WSAGetLastError())); \
	} \
	if(got_ptype != expect_ptype) \
	{ \
		FAIL("Created socket with IPX_PTYPE %d (expected %d)", got_ptype, (int)(expect_ptype)); \
	} \
	closesocket(sock); \
}

#define TRY_SPX_SOCKET(family, type, proto) \
{ \
	int sock = socket(family, type, proto); \
	if(sock == -1) \
	{ \
		FAIL("Couldn't create SPX socket, WSAGetLastError() = %d", (int)(WSAGetLastError())); \
	} \
	closesocket(sock); \
}

int main()
{
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	/* Windows 95/98 allow creating IPX/SPX sockets using family 0. Nothing
	 * seems to rely on this.
	*/
	
	TRY_IPX_SOCKET(AF_IPX, SOCK_DGRAM, NSPROTO_IPX,       0);
	TRY_IPX_SOCKET(AF_IPX, SOCK_DGRAM, NSPROTO_IPX + 1,   1);
	TRY_IPX_SOCKET(AF_IPX, SOCK_DGRAM, NSPROTO_IPX + 100, 100);
	TRY_IPX_SOCKET(AF_IPX, SOCK_DGRAM, NSPROTO_IPX + 255, 255);
	
	TRY_SPX_SOCKET(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
	TRY_SPX_SOCKET(AF_IPX, SOCK_STREAM, NSPROTO_SPXII);
	
	WSACleanup();
	
	return 0;
}
