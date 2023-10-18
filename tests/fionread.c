/* IPXWrapper test suite
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

#include <assert.h>
#include <stdio.h>

#include <winsock2.h>
#include <windows.h>
#include <wsipx.h>

#include "tap/basic.h"
#include "../tools/tools.h"

static char buf[4096];

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		fprintf(stderr, "Usage: %s <netnum> <nodenum>\n", argv[0]);
		return 1;
	}
	
	plan_lazy();
	
	struct sockaddr_ipx addr1 = read_sockaddr(argv[1], argv[2], "1234");
	struct sockaddr_ipx addr2 = read_sockaddr(argv[1], argv[2], "1235");
	
	WSADATA data;
	WSAStartup(MAKEWORD(1, 1), &data);
	
	int sock1 = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock1 != SOCKET_ERROR);
	
	assert(bind(sock1, (struct sockaddr*)(&addr1), sizeof(addr1)) == 0);
	
	int sock2 = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock2 != SOCKET_ERROR);
	
	assert(bind(sock2, (struct sockaddr*)(&addr2), sizeof(addr2)) == 0);
	
	fd_set readfds;
	struct timeval timeout = { 0, 0 };
	
	unsigned long x = 123456789;
	int r = ioctlsocket(sock1, FIONREAD, &x);
	
	is_int(0, r, "ioctlsocket(FIONREAD) succeeds when no packets are waiting");
	is_int(0, x, "ioctlsocket(FIONREAD) returns zero bytes when no packets are waiting");
	
	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	is_int(select(-1, &readfds, NULL, NULL, &timeout), 0, "select() initially indicates socket is not ready to read");
	ok(!FD_ISSET(sock1, &readfds), "select() initially indicates socket is not ready to read");
	
	assert(sendto(sock2, buf, 128, 0, (struct sockaddr*)(&addr1), sizeof(addr1)) == 128);
	
	/* Just in case there's any async going on */
	Sleep(100);
	
	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	is_int(select(-1, &readfds, NULL, NULL, &timeout), 1, "select() indicates socket is ready to read before call to ioctlsocket(FIONREAD)");
	ok(FD_ISSET(sock1, &readfds), "select() indicates socket is ready to read before call to ioctlsocket(FIONREAD)");
	
	x = 123456789;
	r = ioctlsocket(sock1, FIONREAD, &x);
	
	is_int(0, r, "ioctlsocket(FIONREAD) succeeds when packets are waiting");
	is_int(128, x, "ioctlsocket(FIONREAD) returns payload size when one packet is waiting");
	
	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	is_int(select(-1, &readfds, NULL, NULL, &timeout), 1, "select() indicates socket is ready to read after call to ioctlsocket(FIONREAD)");
	ok(FD_ISSET(sock1, &readfds), "select() indicates socket is ready to read after call to ioctlsocket(FIONREAD)");
	
	assert(sendto(sock2, buf, 256, 0, (struct sockaddr*)(&addr1), sizeof(addr1)) == 256);
	
	/* Just in case there's any async going on */
	Sleep(100);
	
	x = 123456789;
	r = ioctlsocket(sock1, FIONREAD, &x);
	
	is_int(0, r, "ioctlsocket(FIONREAD) succeeds when packets are waiting");
	is_int(384, x, "ioctlsocket(FIONREAD) returns combined payload sizes when multiple packets are waiting");
	
	assert(recv(sock1, buf, 4096, 0) == 128);
	
	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	is_int(select(-1, &readfds, NULL, NULL, &timeout), 1, "select() indicates socket is ready to read after reading first packet");
	ok(FD_ISSET(sock1, &readfds), "select() indicates socket is ready to read after reading first packet");
	
	x = 123456789;
	r = ioctlsocket(sock1, FIONREAD, &x);
	
	is_int(0, r, "ioctlsocket(FIONREAD) succeeds when packets are waiting");
	is_int(256, x, "ioctlsocket(FIONREAD) returns payload sizes when one packet is waiting");
	
	assert(recv(sock1, buf, 4096, 0) == 256);
	
	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	is_int(select(-1, &readfds, NULL, NULL, &timeout), 0, "select() indicates socket is not ready to read after reading second packet");
	ok(!FD_ISSET(sock1, &readfds), "select() indicates socket is ready to read after reading second packet");
	
	x = 123456789;
	r = ioctlsocket(sock1, FIONREAD, &x);
	
	is_int(0, r, "ioctlsocket(FIONREAD) succeeds when no packets are waiting");
	is_int(0, x, "ioctlsocket(FIONREAD) returns zero bytes when no packets are waiting");
	
	closesocket(sock2);
	closesocket(sock1);
	
	WSACleanup();
	
	return 0;
}
