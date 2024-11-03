/* ipxwrapper - DirectPlay service provider
 * Copyright (C) 2011-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define INITGUID
#define WINSOCK_API_LINKAGE

#include <winsock2.h>
#include <windows.h>
#include <dplaysp.h>
#include <wsipx.h>

#include "ipxwrapper.h"
#include "common.h"

struct sp_data {
	SOCKET sock;
	
	SOCKET ns_sock;			/* For RECEIVING discovery messages only, -1 when not hosting */
	struct sockaddr_ipx ns_addr;	/* sa_family is 0 when undefined */
	DPID ns_id;
	
	BOOL running;
	HANDLE worker_thread;
	WSAEVENT event;
	
	CRITICAL_SECTION lock;
};

#define DISCOVERY_SOCKET 42367

/* Do not change this value! We don't need any memory reserved for writing out
 * packet headers, but DirectPlay seems to corrupt itself internally and do
 * funky things if this goes below a certain threshold. Value taken from the DX5
 * service provider.
*/
#define API_HEADER_SIZE 20

#define CALL(func) log_printf(LOG_CALL, "directplay.c: " func);

/* Lock the object mutex and return the data pointer */
static struct sp_data *get_sp_data(IDirectPlaySP *sp) {
	struct sp_data *data;
	DWORD size;
	
	HRESULT r = IDirectPlaySP_GetSPData(sp, (void**)&data, &size, DPGET_LOCAL);
	if(r != DP_OK) {
		log_printf(LOG_ERROR, "GetSPData: %d", (int)r);
		abort();
	}
	
	EnterCriticalSection(&(data->lock));
	
	return data;
}

/* Release the object mutex */
static void release_sp_data(struct sp_data *data) {
	LeaveCriticalSection(&(data->lock));
}

static void recv_packet(SOCKET *sockfd, char *buf, IDirectPlaySP *sp)
{
	struct sp_data *sp_data = get_sp_data((IDirectPlaySP*)(sp));
	
	if(*sockfd == -1)
	{
		release_sp_data(sp_data);
		return;
	}
	
	struct sockaddr_ipx addr;
	int addrlen = sizeof(addr);
	
	int size = recvfrom(*sockfd, buf, MAX_DATA_SIZE, 0, (struct sockaddr*)(&addr), &addrlen);
	if(size == -1)
	{
		if(WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAECONNRESET)
		{
			/* WSAEWOULDBLOCK - No packets waiting on this socket.
			 * WSAECONNRESET  - We got an ICMP error on this port.
			*/
			release_sp_data(sp_data);
			return;
		}
		
		log_printf(LOG_ERROR, "DirectPlay read error: %s", w32_error(WSAGetLastError()));
		log_printf(LOG_DEBUG, "Closing socket %u", (unsigned int)(*sockfd));
		
		closesocket(*sockfd);
		*sockfd = -1;
		
		release_sp_data(sp_data);
		return;
	}
	
	release_sp_data(sp_data);
	
	/* Pass the message on to DirectPlay to be processed. */
	
	IPX_STRING_ADDR(str_addr, addr32_in(addr.sa_netnum), addr48_in(addr.sa_nodenum), addr.sa_socket);
	log_printf(LOG_DEBUG, "About to HandleMessage from %s", str_addr);
	
	HRESULT r = IDirectPlaySP_HandleMessage(sp, buf, size, &addr);
	
	log_printf(LOG_DEBUG, "HandleMessage returned %x", (unsigned int)(r));
}

static DWORD WINAPI worker_main(LPVOID sp) {
	struct sp_data *sp_data = get_sp_data((IDirectPlaySP*)(sp));
	release_sp_data(sp_data);
	
	char *buf = malloc(MAX_DATA_SIZE);
	if(!buf) {
		abort();
	}
	
	while(1) {
		WaitForSingleObject(sp_data->event, INFINITE);
		
		get_sp_data((IDirectPlaySP*)(sp));
		
		WSAResetEvent(sp_data->event);
		
		if(!sp_data->running)
		{
			break;
		}
		
		release_sp_data(sp_data);
		
		recv_packet(&(sp_data->sock),    buf, sp);
		recv_packet(&(sp_data->ns_sock), buf, sp);
	}
	
	free(buf);
	
	return 0;
}

static BOOL init_worker(IDirectPlaySP *sp, struct sp_data *sp_data)
{
	if(sp_data->worker_thread)
	{
		return TRUE;
	}
	
	DWORD worker_thread_id;
	sp_data->worker_thread = CreateThread(NULL, 0, &worker_main, sp, 0, &worker_thread_id);
	if(!sp_data->worker_thread)
	{
		log_printf(LOG_ERROR, "Failed to create worker thread");
		return FALSE;
	}
	
	return TRUE;
}

static BOOL init_main_socket(struct sp_data *sp_data)
{
	if(sp_data->sock == -1)
	{
		int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
		if(sock == -1)
		{
			log_printf(LOG_ERROR,
				"Error creating IPX socket: %s", w32_error(WSAGetLastError()));
			return FALSE;
		}
		
		struct sockaddr_ipx addr;
		memset(&addr, 0, sizeof(addr));
		addr.sa_family = AF_IPX;
		
		if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
		{
			log_printf(LOG_ERROR,
				"Error binding IPX socket: %s", w32_error(WSAGetLastError()));
			
			closesocket(sock);
			return FALSE;
		}
		
		BOOL bcast = TRUE;
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)(&bcast), sizeof(bcast));
		
		if(WSAEventSelect(sock, sp_data->event, FD_READ) == -1)
		{
			log_printf(LOG_ERROR,
				"WSAEventSelect: %s", w32_error(WSAGetLastError()));
			
			closesocket(sock);
			return FALSE;
		}
		
		sp_data->sock = sock;
	}
	
	return TRUE;
}

static BOOL init_disc_socket(struct sp_data *sp_data)
{
	if(sp_data->ns_sock == -1)
	{
		int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
		if(sock == -1)
		{
			log_printf(LOG_ERROR,
				"Error creating IPX socket: %s", w32_error(WSAGetLastError()));
			
			return FALSE;
		}
		
		BOOL t_bool = TRUE;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&t_bool, sizeof(BOOL));
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&t_bool, sizeof(BOOL));
		
		/* Get the address of the main socket, use it to bind
		 * to the discovery port on the right net/node.
		*/
		
		struct sockaddr_ipx addr;
		int addrlen = sizeof(addr);
		
		if(getsockname(sp_data->sock, (struct sockaddr*)(&addr), &addrlen) == -1)
		{
			log_printf(LOG_ERROR,
				"getsockname: %s", w32_error(WSAGetLastError()));
			
			closesocket(sock);
			return FALSE;
		}
		
		addr.sa_socket = htons(DISCOVERY_SOCKET);
		
		if(bind(sock, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
		{
			log_printf(LOG_ERROR,
				"Cannot bind DP discovery socket: %s", w32_error(WSAGetLastError()));
			
			closesocket(sock);
			return FALSE;
		}
		
		if(WSAEventSelect(sock, sp_data->event, FD_READ) == -1)
		{
			log_printf(LOG_ERROR,
				"WSAEventSelect: %s", w32_error(WSAGetLastError()));
			
			closesocket(sock);
			return FALSE;
		}
		
		sp_data->ns_sock = sock;
	}
	
	return TRUE;
}

static HRESULT WINAPI IPX_EnumSessions(LPDPSP_ENUMSESSIONSDATA data) {
	CALL("SP_EnumSessions");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(!init_worker(data->lpISP, sp_data) || !init_main_socket(sp_data))
	{
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	/* Get the address of our main socket. */
	
	struct sockaddr_ipx my_addr;
	int addrlen = sizeof(my_addr);
	
	if(getsockname(sp_data->sock, (struct sockaddr*)(&my_addr), &addrlen) == -1)
	{
		log_printf(LOG_ERROR,
			"getsockname: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	/* Broadcast to the discovery socket on the current network. */
	
	struct sockaddr_ipx to_addr = my_addr;
	
	memset(to_addr.sa_nodenum, 0xFF, 6);
	to_addr.sa_socket = htons(DISCOVERY_SOCKET);
	
	/* Send probe packet. */
	
	if(sendto(sp_data->sock,
		data->lpMessage + API_HEADER_SIZE, data->dwMessageSize - API_HEADER_SIZE, 0,
		(struct sockaddr*)(&to_addr), sizeof(to_addr)) == -1)
	{
		log_printf(LOG_ERROR,
			"IPX_EnumSessions: sendto failed: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_Send(LPDPSP_SENDDATA data) {
	CALL("SP_Send");
	
	struct sockaddr_ipx to_addr;
	
	if(data->idPlayerTo)
	{
		struct sockaddr_ipx *addr_p;
		DWORD addr_size;
		
		HRESULT r = IDirectPlaySP_GetSPPlayerData(
			data->lpISP, data->idPlayerTo, (void**)(&addr_p), &addr_size, 0);
		if(r != DP_OK)
		{
			log_printf(LOG_ERROR, "GetSPPlayerData: %x", (unsigned int)(r));
			return r;
		}
		
		if(addr_p && addr_size == sizeof(to_addr))
		{
			to_addr = *addr_p;
		}
		else{
			log_printf(LOG_ERROR,
				"Attempted SP_Send to an idPlayerTo (%u) with no player data",
				(unsigned int)(data->idPlayerTo));
			return DPERR_GENERIC;
		}
	}
	else{
		struct sp_data *sp_data = get_sp_data(data->lpISP);
		to_addr = sp_data->ns_addr;
		release_sp_data(sp_data);
		
		if(!to_addr.sa_family) {
			log_printf(LOG_ERROR,
				"Attempted SP_Send with idPlayerTo 0, but no name server address known");
			return DPERR_GENERIC;
		}
	}
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(sendto(sp_data->sock,
		data->lpMessage + API_HEADER_SIZE, data->dwMessageSize - API_HEADER_SIZE, 0,
		(struct sockaddr*)(&to_addr), sizeof(to_addr)) == -1)
	{
		log_printf(LOG_ERROR, "IPX_Send: sendto failed: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_Reply(LPDPSP_REPLYDATA data) {
	CALL("SP_Reply");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	/* Stash the address of the name server if the name server ID has
	 * changed.
	 * 
	 * TODO: Store the ID and resolve the address at each xmit, probably
	 * more reliable than assuming the address is known at this point.
	*/
	
	if(sp_data->ns_id != data->idNameServer)
	{
		log_printf(LOG_DEBUG, "IPX_Reply: Name server update (%u -> %u)",
			(unsigned int)(sp_data->ns_id), (unsigned int)(data->idNameServer));
		
		struct sockaddr_ipx *addr_p;
		DWORD size;
		
		HRESULT r = IDirectPlaySP_GetSPPlayerData(data->lpISP, data->idNameServer, (void**)&addr_p, &size, 0);
		if(r != DP_OK)
		{
			log_printf(LOG_DEBUG, "IPX_Reply: GetSPPlayerData: %x", (unsigned int)(r));
		}
		else if(addr_p == NULL)
		{
			log_printf(LOG_DEBUG, "IPX_Reply: Cannot update name server, no shared data");
		}
		else if(size != sizeof(struct sockaddr_ipx))
		{
			log_printf(LOG_DEBUG,
				"IPX_Reply: Cannot update name server, shared data is %u bytes (expected %u)",
				(unsigned int)(size), (unsigned int)(sizeof(struct sockaddr_ipx)));
		}
		else{
			memcpy(&(sp_data->ns_addr), addr_p, sizeof(struct sockaddr_ipx));
			sp_data->ns_id = data->idNameServer;
			
			IPX_STRING_ADDR(str_addr,
				addr32_in(sp_data->ns_addr.sa_netnum),
				addr48_in(sp_data->ns_addr.sa_nodenum),
				sp_data->ns_addr.sa_socket);
			
			log_printf(LOG_DEBUG, "IPX_Reply: New name server address is %s", str_addr);
		}
	}
	
	/* Check we are being called in a context where we actually have an
	 * address to send the reply to.
	*/
	
	struct sockaddr_ipx *to_addr = (struct sockaddr_ipx*)(data->lpSPMessageHeader);
	
	if(to_addr == NULL)
	{
		log_printf(LOG_DEBUG, "Attempted SP_Reply with NULL lpSPMessageHeader");
		return DPERR_GENERIC;
	}
	
	/* Send the message. */
	
	if(sendto(sp_data->sock,
		data->lpMessage + API_HEADER_SIZE, data->dwMessageSize - API_HEADER_SIZE, 0,
		(struct sockaddr*)(to_addr), sizeof(*to_addr)) == -1)
	{
		log_printf(LOG_ERROR, "IPX_Reply: sendto failed: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

/* CreatePlayer dwFlags bits. Meanings guessed from examination of official
 * implementation.
*/
#define CREATEPLAYER_FIRST 1 /* The first player ID of a node */
#define CREATEPLAYER_NS    2 /* This instance is becomming the name server */
#define CREATEPLAYER_SELF  8 /* This is a local player ID */

static HRESULT WINAPI IPX_CreatePlayer(LPDPSP_CREATEPLAYERDATA data) {
	CALL("SP_CreatePlayer");
	
	if(data->lpSPMessageHeader)
	{
		struct sockaddr_ipx *addr = data->lpSPMessageHeader;
		
		IPX_STRING_ADDR(str_addr, addr32_in(addr->sa_netnum), addr48_in(addr->sa_nodenum), addr->sa_socket);
		log_printf(LOG_DEBUG, "IPX_CreatePlayer: idPlayer = %u, addr = %s, dwFlags = %u",
			(unsigned int)(data->idPlayer), str_addr, (unsigned int)(data->dwFlags));
	}
	else{
		log_printf(LOG_DEBUG, "IPX_CreatePlayer: idPlayer = %u, dwFlags = %u",
			(unsigned int)(data->idPlayer), (unsigned int)(data->dwFlags));
	}
	
	if(data->dwFlags & CREATEPLAYER_SELF)
	{
		if(data->dwFlags & CREATEPLAYER_NS)
		{
			/* We are becoming the name server, initialise the name
			 * server socket used to receive discovery packets.
			*/
			
			struct sp_data *sp_data = get_sp_data(data->lpISP);
			init_disc_socket(sp_data);
			release_sp_data(sp_data);
		}
		
		/* This is a local player ID, initialise the shared player data
		 * with our socket address.
		*/
		
		struct sockaddr_ipx my_addr;
		int addrlen = sizeof(my_addr);
		
		{
			struct sp_data *sp_data = get_sp_data(data->lpISP);
			
			if(getsockname(sp_data->sock, (struct sockaddr*)(&my_addr), &addrlen) == -1)
			{
				log_printf(LOG_ERROR,
					"getsockname: %s", w32_error(WSAGetLastError()));
				
				release_sp_data(sp_data);
				return DPERR_GENERIC;
			}
			
			release_sp_data(sp_data);
		}
		
		HRESULT r = IDirectPlaySP_SetSPPlayerData(
			data->lpISP, data->idPlayer, &my_addr, sizeof(my_addr), 0);
		if(r != DP_OK)
		{
			log_printf(LOG_ERROR, "IPX_CreatePlayer: SetSPPlayerData: %x", (unsigned int)(r));
			return r;
		}
	}
	else{
		/* This is a remote player ID, verify the shared player data
		 * already contains an address.
		*/
		
		struct sockaddr_ipx *addr;
		DWORD addr_size;
		
		HRESULT r = IDirectPlaySP_GetSPPlayerData(
			data->lpISP, data->idPlayer, (void**)(&addr), &addr_size, 0);
		if(r != DP_OK) {
			log_printf(LOG_ERROR, "IPX_CreatePlayer: GetSPPlayerData: %x", (unsigned int)(r));
			return r;
		}
		
		if(addr == NULL)
		{
			log_printf(LOG_WARNING,
				"IPX_CreatePlayer: Remote player %u has no shared data",
				(unsigned int)(data->idPlayer));
		}
		else if(addr_size != sizeof(*addr))
		{
			log_printf(LOG_WARNING,
				"IPX_CreatePlayer: Remote player %u shared data is %u bytes (expected %u)",
				(unsigned int)(data->idPlayer),
				(unsigned int)(addr_size), (unsigned int)(sizeof(*addr)));
		}
	}
	
	return DP_OK;
}

static HRESULT WINAPI IPX_GetCaps(LPDPSP_GETCAPSDATA data) {
	CALL("SP_GetCaps");
	
	if(data->lpCaps->dwSize < sizeof(DPCAPS)) {
		/* It's either this or DPERR_INVALIDOBJECT according to DirectX 7.0 */
		return DPERR_INVALIDPARAMS;
	}
	
	/* Most values are incorrect/inaccurate, copied from the MS implementation
	 * for compatibility.
	 *
	 * No async support is implemented at this time, DirectPlay itself seems
	 * to handle it.
	*/
	
	data->lpCaps->dwFlags = DPCAPS_ASYNCSUPPORTED;
	data->lpCaps->dwMaxBufferSize = 1024;
	data->lpCaps->dwMaxQueueSize = 0;
	data->lpCaps->dwMaxPlayers = 65536;
	data->lpCaps->dwHundredBaud = 0;
	data->lpCaps->dwLatency = 50;
	data->lpCaps->dwMaxLocalPlayers = 65536;
	data->lpCaps->dwHeaderLength = API_HEADER_SIZE;
	data->lpCaps->dwTimeout = 500;
	
	return DP_OK;
}

static HRESULT WINAPI IPX_Open(LPDPSP_OPENDATA data) {
	CALL("SP_Open");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(!init_worker(data->lpISP, sp_data) || !init_main_socket(sp_data))
	{
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	if(data->bCreate)
	{
		/* Don't initialise the name server socket here - it gets done
		 * when SP_CreatePlayer is called with CREATEPLAYER_NS instead.
		*/
	}
	else if(data->lpSPMessageHeader)
	{
		memcpy(&(sp_data->ns_addr), data->lpSPMessageHeader, sizeof(struct sockaddr_ipx));
		sp_data->ns_id = 0;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_CloseEx(LPDPSP_CLOSEDATA data) {
	CALL("SP_CloseEx");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(sp_data->ns_sock != -1)
	{
		closesocket(sp_data->ns_sock);
		sp_data->ns_sock = -1;
	}
	
	if(sp_data->sock == -1)
	{
		closesocket(sp_data->sock);
		sp_data->sock = -1;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_ShutdownEx(LPDPSP_SHUTDOWNDATA data) {
	CALL("SP_ShutdownEx");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	sp_data->running = FALSE;
	WSASetEvent(sp_data->event);
	
	release_sp_data(sp_data);
	
	if(sp_data->worker_thread) {
		if(WaitForSingleObject(sp_data->worker_thread, 3000) == WAIT_TIMEOUT) {
			log_printf(LOG_WARNING, "DirectPlay worker didn't exit in 3 seconds, killing");
			TerminateThread(sp_data->worker_thread, 0);
		}
		
		CloseHandle(sp_data->worker_thread);
		sp_data->worker_thread = NULL;
	}
	
	if(sp_data->ns_sock != -1)
	{
		closesocket(sp_data->ns_sock);
		sp_data->ns_sock = -1;
	}
	
	if(sp_data->sock == -1)
	{
		closesocket(sp_data->sock);
		sp_data->sock = -1;
	}
	
	WSACloseEvent(sp_data->event);
	DeleteCriticalSection(&(sp_data->lock));
	
	return DP_OK;
}

HRESULT WINAPI r_SPInit(LPSPINITDATA);

HRESULT WINAPI SPInit(LPSPINITDATA data) {
	if(!IsEqualGUID(data->lpGuid, &DPSPGUID_IPX)) {
		return r_SPInit(data);
	}
	
	log_printf(LOG_DEBUG, "SPInit: %p (lpAddress = %p, dwAddressSize = %u)", data->lpISP, data->lpAddress, (unsigned int)(data->dwAddressSize));
	
	struct sp_data sp_data;
	
	init_critical_section(&(sp_data.lock));
	
	if((sp_data.event = WSACreateEvent()) == WSA_INVALID_EVENT) {
		log_printf(LOG_ERROR, "Error creating WSA event object: %s", w32_error(WSAGetLastError()));
		goto FAIL3;
	}
	
	sp_data.sock = -1;
	sp_data.ns_sock = -1;
	sp_data.ns_addr.sa_family = 0;
	sp_data.running = TRUE;
	sp_data.worker_thread = NULL;
	
	HRESULT r = IDirectPlaySP_SetSPData(data->lpISP, &sp_data, sizeof(sp_data), DPSET_LOCAL);
	if(r != DP_OK) {
		log_printf(LOG_ERROR, "SetSPData: %d", (int)r);
		goto FAIL5;
	}
	
	data->lpCB->EnumSessions = &IPX_EnumSessions;
	data->lpCB->Send = &IPX_Send;
	data->lpCB->Reply = &IPX_Reply;
	data->lpCB->CreatePlayer = &IPX_CreatePlayer;
	data->lpCB->GetCaps = &IPX_GetCaps;
	data->lpCB->Open = &IPX_Open;
	data->lpCB->CloseEx = &IPX_CloseEx;
	data->lpCB->ShutdownEx = &IPX_ShutdownEx;
	
	data->dwSPHeaderSize = API_HEADER_SIZE;
	data->dwSPVersion = DPSP_MAJORVERSIONMASK & DPSP_MAJORVERSION;
	
	return DP_OK;
	
	FAIL5:
	WSACloseEvent(sp_data.event);
	
	FAIL3:
	DeleteCriticalSection(&(sp_data.lock));
	
	FAIL2:
	return DPERR_UNAVAILABLE;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		fprof_init(stub_fstats, NUM_STUBS);
		
		log_init();
		
		main_config_t config = get_main_config(false);
		
		min_log_level = config.log_level;
		
		log_connect(config.log_server_addr, config.log_server_port);
	}
	else if(fdwReason == DLL_PROCESS_DETACH)
	{
		/* When the "lpvReserved" parameter is non-NULL, the process is terminating rather
		 * than the DLL being unloaded dynamically and any threads will have been terminated
		 * at unknown points, meaning any global data may be in an inconsistent state and we
		 * cannot (safely) clean up. MSDN states we should do nothing.
		*/
		if(lpvReserved != NULL)
		{
			return TRUE;
		}
		
		unload_dlls();
		log_close();
		
		fprof_cleanup(stub_fstats, NUM_STUBS);
	}
	
	return TRUE;
}
