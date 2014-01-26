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
#include <assert.h>
#include <wsipx.h>
#include <wsnwlink.h>

#include "test.h"

static struct sockaddr_ipx get_iface_addr(int ifnum)
{
	int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock != -1);
	
	IPX_ADDRESS_DATA iface_info;
	iface_info.adapternum = ifnum;
	
	int optlen = sizeof(iface_info);
	assert(getsockopt(sock, NSPROTO_IPX, IPX_ADDRESS, (char*)(&iface_info), &optlen) == 0);
	
	closesocket(sock);
	
	struct sockaddr_ipx addr;
	
	addr.sa_family = AF_IPX;
	memcpy(addr.sa_netnum,  iface_info.netnum,  4);
	memcpy(addr.sa_nodenum, iface_info.nodenum, 6);
	
	return addr;
}

int main()
{
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	struct sockaddr_ipx iface1_addr = get_iface_addr(0);
	struct sockaddr_ipx iface2_addr = get_iface_addr(1);
	
	/* Bind lsock1 to socket 2000 on the first interface */
	
	int lsock1 = socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
	assert(lsock1 != -1);
	
	{
		unsigned long nonblock = 1;
		assert(ioctlsocket(lsock1, FIONBIO, &nonblock) == 0);
	}
	
	struct sockaddr_ipx ls1_addr = iface1_addr;
	ls1_addr.sa_socket = htons(2000);
	
	assert(bind(lsock1, (struct sockaddr*)(&ls1_addr), sizeof(ls1_addr)) == 0);
	
	assert(listen(lsock1, 8) == 0);
	
	/* Bind lsock2 to a random socket on the second interface */
	
	int lsock2 = socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
	assert(lsock2 != -1);
	
	{
		unsigned long nonblock = 1;
		assert(ioctlsocket(lsock2, FIONBIO, &nonblock) == 0);
	}
	
	struct sockaddr_ipx ls2_addr = iface2_addr;
	ls2_addr.sa_socket = 0;
	
	assert(bind(lsock2, (struct sockaddr*)(&ls2_addr), sizeof(ls2_addr)) == 0);
	assert(listen(lsock2, 8) == 0);
	
	{
		int addrlen = sizeof(ls2_addr);
		assert(getsockname(lsock2, (struct sockaddr*)(&ls2_addr), &addrlen) == 0);
	}
	
	/* Unbound client should implicitly be bound to a random address. */
	
	{
		EXPECT_NO_ACCEPT(lsock1);
		
		int sock = socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		assert(sock != -1);
		
		MUST_CONNECT_TO(sock, ls1_addr);
		
		/* Work around race condition; connect returns before listening
		 * socket has necessarily been notified.
		*/
		
		Sleep(100);
		
		closesocket(EXPECT_ACCEPT(lsock1));
		
		closesocket(sock);
	}
	
	/* Explicitly bound client should retain address. */
	
	{
		EXPECT_NO_ACCEPT(lsock1);
		
		int sock = socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		assert(sock != -1);
		
		struct sockaddr_ipx my_addr = iface1_addr;
		my_addr.sa_socket = htons(84);
		
		assert(bind(sock, (struct sockaddr*)(&my_addr), sizeof(my_addr)) == 0);
		EXPECT_LOCAL_ADDR(sock, my_addr);
		
		MUST_CONNECT_TO(sock, ls1_addr);
		
		/* Work around race condition; connect returns before listening
		 * socket has necessarily been notified.
		*/
		
		Sleep(100);
		
		closesocket(EXPECT_ACCEPT_FROM(lsock1, my_addr));
		
		closesocket(sock);
	}
	
	/* Try to connect to the other socket. */
	
	{
		EXPECT_NO_ACCEPT(lsock2);
		
		int sock = socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		assert(sock != -1);
		
		MUST_CONNECT_TO(sock, ls2_addr);
		
		/* Work around race condition; connect returns before listening
		 * socket has necessarily been notified.
		*/
		
		Sleep(100);
		
		closesocket(EXPECT_ACCEPT(lsock2));
		
		closesocket(sock);
	}
	
	closesocket(lsock2);
	closesocket(lsock1);
	
	WSACleanup();
	
	return 0;
}
