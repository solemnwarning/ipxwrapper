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
#include <stdio.h>
#include <assert.h>
#include <wsipx.h>

#include "test.h"

int main()
{
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(2,2), &wsaData) == 0);
	}
	
	/* Setup sock1... */
	
	int sock1 = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock1 != -1);
	
	struct sockaddr_ipx addr1;
	memset(&addr1, 0, sizeof(addr1));
	addr1.sa_family = AF_IPX;
	
	assert(bind(sock1, (struct sockaddr*)(&addr1), sizeof(addr1)) == 0);
	
	{
		int addrlen = sizeof(addr1);
		assert(getsockname(sock1, (struct sockaddr*)(&addr1), &addrlen) == 0);
	}
	
	{
		unsigned long nonblock = 1;
		assert(ioctlsocket(sock1, FIONBIO, &nonblock) == 0);
	}
	
	/* Setup sock2... */
	
	int sock2 = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock2);
	
	struct sockaddr_ipx addr2;
	memset(&addr2, 0, sizeof(addr2));
	addr2.sa_family = AF_IPX;
	
	assert(bind(sock2, (struct sockaddr*)(&addr2), sizeof(addr2)) == 0);
	
	{
		int addrlen = sizeof(addr2);
		assert(getsockname(sock2, (struct sockaddr*)(&addr2), &addrlen) == 0);
	}
	
	{
		unsigned long nonblock = 1;
		assert(ioctlsocket(sock2, FIONBIO, &nonblock) == 0);
	}
	
	/* Send a single packet between the sockets using sendto/recvfrom. */
	
	assert(sendto(sock1, test_data_1, sizeof(test_data_1), 0, (struct sockaddr*)(&addr2), sizeof(addr2)) == sizeof(test_data_1));
	
	Sleep(50);
	
	EXPECT_PACKET_FROM(sock2, test_data_1, addr1);
	EXPECT_NO_PACKETS();
	
	/* Send two packets, then read them out seperately using sendto/recv. */
	
	assert(sendto(sock1, test_data_2, sizeof(test_data_2), 0, (struct sockaddr*)(&addr2), sizeof(addr2)) == sizeof(test_data_2));
	assert(sendto(sock1, test_data_2, sizeof(test_data_2), 0, (struct sockaddr*)(&addr2), sizeof(addr2)) == sizeof(test_data_2));
	
	Sleep(50);
	
	EXPECT_PACKET(sock2, test_data_2);
	EXPECT_PACKET(sock2, test_data_2);
	EXPECT_NO_PACKETS();
	
	/* The socket is currently disconnected... */
	
	/* ...getpeername should fail */
	
	{
		struct sockaddr_ipx remote_addr;
		int addrlen = sizeof(remote_addr);
		
		assert(getpeername(sock1, (struct sockaddr*)(&remote_addr), &addrlen) == -1);
		assert(WSAGetLastError() == WSAENOTCONN);
	}
	
	/* ...send should fail */
	
	assert(send(sock1, test_data_1, sizeof(test_data_1), 0) == -1);
	assert(WSAGetLastError() == WSAENOTCONN);
	
	/* Connect the socket... */
	
	assert(connect(sock1, (struct sockaddr*)(&addr2), sizeof(addr2)) == 0);
	
	/* ...getpeername should work now */
	
	{
		struct sockaddr_ipx remote_addr;
		int addrlen = sizeof(remote_addr);
		
		assert(getpeername(sock1, (struct sockaddr*)(&remote_addr), &addrlen) == 0);
		assert(memcmp(&remote_addr, &addr2, sizeof(addr2)) == 0);
	}
	
	/* ...send should succeed now */
	
	assert(send(sock1, test_data_1, sizeof(test_data_1), 0) == sizeof(test_data_1));
	
	Sleep(100);
	
	EXPECT_PACKET_FROM(sock2, test_data_1, addr1);
	EXPECT_NO_PACKETS();
	
	/* Call connect with an address full of zeroes to "disconnect" it... */
	
	{
		struct sockaddr_ipx zero_addr;
		memset(&zero_addr, 0, sizeof(zero_addr));
		zero_addr.sa_family = AF_IPX;
		
		assert(connect(sock1, (struct sockaddr*)(&zero_addr), sizeof(zero_addr)) == 0);
	}
	
	/* ...getpeername should fail once more */
	
	{
		struct sockaddr_ipx remote_addr;
		int addrlen = sizeof(remote_addr);
		
		assert(getpeername(sock1, (struct sockaddr*)(&remote_addr), &addrlen) == -1);
		assert(WSAGetLastError() == WSAENOTCONN);
	}
	
	/* ...and so should send */
	
	assert(send(sock1, test_data_1, sizeof(test_data_1), 0) == -1);
	assert(WSAGetLastError() == WSAENOTCONN);
	
	/* Clean up */
	
	closesocket(sock2);
	closesocket(sock1);
	
	return 0;
}
