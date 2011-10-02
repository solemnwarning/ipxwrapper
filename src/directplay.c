/* ipxwrapper - DirectPlay service provider
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

/* TODO: ASYNC I/O!! */

#define INITGUID

#include <windows.h>
#include <dplaysp.h>
#include <winsock2.h>
#include <wsipx.h>

#include "ipxwrapper.h"
#include "common.h"

struct sp_data {
	SOCKET sock;
	struct sockaddr_ipx addr;
	
	SOCKET ns_sock;			/* For RECEIVING discovery messages only, -1 when not hosting */
	struct sockaddr_ipx ns_addr;	/* sa_family is 0 when undefined */
	DPID ns_id;
	
	BOOL running;
	HANDLE worker_thread;
	WSAEVENT event;
	
	CRITICAL_SECTION lock;
};

#define DISCOVERY_SOCKET 42367
#define API_HEADER_SIZE sizeof(struct sockaddr_ipx)

#define CALL(n) if(log_calls) { log_printf("DirectPlay: %s", n); }

/* Lock the object mutex and return the data pointer */
static struct sp_data *get_sp_data(IDirectPlaySP *sp) {
	struct sp_data *data;
	DWORD size;
	
	HRESULT r = IDirectPlaySP_GetSPData(sp, (void**)&data, &size, DPGET_LOCAL);
	if(r != DP_OK) {
		log_printf("GetSPData: %d", (int)r);
		abort();
	}
	
	EnterCriticalSection(&(data->lock));
	
	return data;
}

/* Release the object mutex */
static void release_sp_data(struct sp_data *data) {
	LeaveCriticalSection(&(data->lock));
}

static BOOL recv_packet(int sockfd, char *buf, IDirectPlaySP *sp) {
	struct sockaddr_ipx addr;
	int addrlen = sizeof(addr);
	
	int r = recvfrom(sockfd, buf, MAX_DATA_SIZE, 0, (struct sockaddr*)&addr, &addrlen);
	if(r == -1) {
		if(WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAECONNRESET) {
			return TRUE;
		}
		
		log_printf("Read error (IPX): %s", w32_error(WSAGetLastError()));
		return FALSE;
	}
	
	HRESULT h = IDirectPlaySP_HandleMessage(sp, buf, r, &addr);
	if(h != DP_OK) {
		log_printf("HandleMessage error: %d", (int)h);
	}
	
	return TRUE;
}

static DWORD WINAPI worker_main(LPVOID sp) {
	struct sp_data *sp_data = get_sp_data((IDirectPlaySP*)sp);
	release_sp_data(sp_data);
	
	char *buf = malloc(MAX_DATA_SIZE);
	if(!buf) {
		abort();
	}
	
	while(1) {
		WaitForSingleObject(sp_data->event, INFINITE);
		
		get_sp_data((IDirectPlaySP*)sp);
		
		WSAResetEvent(sp_data->event);
		
		if(!sp_data->running) {
			release_sp_data(sp_data);
			return 0;
		}
		
		release_sp_data(sp_data);
		
		if(!recv_packet(sp_data->sock, buf, sp)) {
			return 1;
		}
		
		if(sp_data->ns_sock != -1 && !recv_packet(sp_data->ns_sock, buf, sp)) {
			log_printf("Closing ns_sock due to error");
			
			get_sp_data((IDirectPlaySP*)sp);
			
			closesocket(sp_data->ns_sock);
			sp_data->ns_sock = -1;
			
			release_sp_data(sp_data);
		}
	}
	
	return 0;
}

static BOOL init_worker(IDirectPlaySP *sp) {
	struct sp_data *sp_data = get_sp_data(sp);
	
	if(sp_data->worker_thread) {
		release_sp_data(sp_data);
		return TRUE;
	}
	
	sp_data->worker_thread = CreateThread(NULL, 0, &worker_main, sp, 0, NULL);
	if(!sp_data->worker_thread) {
		log_printf("Failed to create worker thread");
		
		release_sp_data(sp_data);
		return FALSE;
	}
	
	release_sp_data(sp_data);
	return TRUE;
}

static HRESULT WINAPI IPX_EnumSessions(LPDPSP_ENUMSESSIONSDATA data) {
	CALL("SP_EnumSessions");
	
	if(!init_worker(data->lpISP)) {
		return DPERR_GENERIC;
	}
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	struct sockaddr_ipx addr;
	
	memcpy(&addr, &(sp_data->addr), sizeof(addr));
	
	memset(addr.sa_nodenum, 0xFF, 6);
	addr.sa_socket = htons(DISCOVERY_SOCKET);
	
	if(sendto(sp_data->sock, data->lpMessage + API_HEADER_SIZE, data->dwMessageSize - API_HEADER_SIZE, 0, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		log_printf("sendto failed: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_Send(LPDPSP_SENDDATA data) {
	CALL("SP_Send");
	
	struct sockaddr_ipx *addr = NULL;
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(data->idPlayerTo) {
		DWORD size;
		
		HRESULT r = IDirectPlaySP_GetSPPlayerData(data->lpISP, data->idPlayerTo, (void**)&addr, &size, DPGET_LOCAL);
		if(r != DP_OK) {
			log_printf("GetSPPlayerData: %d", (int)r);
			addr = NULL;
		}
	}else if(sp_data->ns_addr.sa_family) {
		addr = &(sp_data->ns_addr);
	}
	
	if(!addr) {
		log_printf("No known address for player ID %u, dropping packet", data->idPlayerTo);
		
		release_sp_data(sp_data);
		return DP_OK;
	}
	
	if(sendto(sp_data->sock, data->lpMessage + API_HEADER_SIZE, data->dwMessageSize - API_HEADER_SIZE, 0, (struct sockaddr*)addr, sizeof(*addr)) == -1) {
		log_printf("sendto failed: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_SendEx(LPDPSP_SENDEXDATA data) {
	CALL("SP_SendEx");
	
	DPSP_SENDDATA s_data;
	
	char *buf = malloc(data->dwMessageSize);
	size_t off = 0, i;
	
	for(i = 0; i < data->cBuffers; i++) {
		if(off + data->lpSendBuffers[i].len > data->dwMessageSize) {
			log_printf("dwMessageSize too small, aborting");
			return DPERR_GENERIC;
		}
		
		memcpy(buf + off, data->lpSendBuffers[i].pData, data->lpSendBuffers[i].len);
		off += data->lpSendBuffers[i].len;
	}
	
	s_data.dwFlags = data->dwFlags;
	s_data.idPlayerTo = data->idPlayerTo;
	s_data.idPlayerFrom = data->idPlayerFrom;
	s_data.lpMessage = buf;
	s_data.dwMessageSize = data->dwMessageSize;
	s_data.bSystemMessage = data->bSystemMessage;
	s_data.lpISP = data->lpISP;
	
	HRESULT ret = IPX_Send(&s_data);
	
	free(buf);
	
	return ret;
}

static HRESULT WINAPI IPX_Reply(LPDPSP_REPLYDATA data) {
	CALL("SP_Reply");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(sp_data->ns_id != data->idNameServer) {
		struct sockaddr_ipx *addr_p;
		DWORD size;
		
		HRESULT r = IDirectPlaySP_GetSPPlayerData(data->lpISP, data->idNameServer, (void**)&addr_p, &size, DPGET_LOCAL);
		if(r != DP_OK) {
			log_printf("GetSPPlayerData: %d", (int)r);
		}else if(addr_p) {
			memcpy(&(sp_data->ns_addr), addr_p, sizeof(struct sockaddr_ipx));
			sp_data->ns_id = data->idNameServer;
		}
	}
	
	/* Do the actual sending */
	
	if(sendto(sp_data->sock, data->lpMessage + API_HEADER_SIZE, data->dwMessageSize - API_HEADER_SIZE, 0, (struct sockaddr*)data->lpSPMessageHeader, sizeof(struct sockaddr_ipx)) == -1) {
		log_printf("sendto failed: %s", w32_error(WSAGetLastError()));
		
		release_sp_data(sp_data);
		return DPERR_GENERIC;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_CreatePlayer(LPDPSP_CREATEPLAYERDATA data) {
	CALL("SP_CreatePlayer");
	
	if(data->lpSPMessageHeader) {
		HRESULT r = IDirectPlaySP_SetSPPlayerData(data->lpISP, data->idPlayer, data->lpSPMessageHeader, sizeof(struct sockaddr_ipx), DPSET_LOCAL);
		if(r != DP_OK) {
			log_printf("SetSPPlayerData: %d", (int)r);
			return DPERR_GENERIC;
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
	*/
	
	data->lpCaps->dwFlags = 0;
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
	
	if(!init_worker(data->lpISP)) {
		return DPERR_GENERIC;
	}
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(data->bCreate) {
		if(sp_data->ns_sock == -1) {
			if((sp_data->ns_sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX)) == -1) {
				release_sp_data(sp_data);
				
				log_printf("Cannot create ns_sock: %s", w32_error(WSAGetLastError()));
				return DPERR_CANNOTCREATESERVER;
			}
			
			BOOL t_bool = TRUE;
			setsockopt(sp_data->ns_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&t_bool, sizeof(BOOL));
			setsockopt(sp_data->ns_sock, SOL_SOCKET, SO_BROADCAST, (char*)&t_bool, sizeof(BOOL));
			
			struct sockaddr_ipx addr;
			
			memcpy(&addr, &(sp_data->addr), sizeof(addr));
			addr.sa_socket = htons(DISCOVERY_SOCKET);
			
			if(bind(sp_data->ns_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
				closesocket(sp_data->ns_sock);
				sp_data->ns_sock = -1;
				
				release_sp_data(sp_data);
				
				log_printf("Cannot bind ns_sock: %s", w32_error(WSAGetLastError()));
				return DPERR_CANNOTCREATESERVER;
			}
			
			if(WSAEventSelect(sp_data->ns_sock, sp_data->event, FD_READ) == -1) {
				closesocket(sp_data->ns_sock);
				sp_data->ns_sock = -1;
				
				release_sp_data(sp_data);
				
				log_printf("WSAEventSelect failed: %s", w32_error(WSAGetLastError()));
				return DPERR_CANNOTCREATESERVER;
			}
		}
	}else if(data->lpSPMessageHeader) {
		memcpy(&(sp_data->ns_addr), data->lpSPMessageHeader, sizeof(struct sockaddr_ipx));
		sp_data->ns_id = 0;
	}
	
	release_sp_data(sp_data);
	return DP_OK;
}

static HRESULT WINAPI IPX_CloseEx(LPDPSP_CLOSEDATA data) {
	CALL("SP_CloseEx");
	
	struct sp_data *sp_data = get_sp_data(data->lpISP);
	
	if(sp_data->ns_sock != -1) {
		closesocket(sp_data->ns_sock);
		sp_data->ns_sock = -1;
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
			log_printf("DirectPlay worker didn't exit in 3 seconds, killing");
			TerminateThread(sp_data->worker_thread, 0);
		}
		
		CloseHandle(sp_data->worker_thread);
		sp_data->worker_thread = NULL;
	}
	
	if(sp_data->ns_sock != -1) {
		closesocket(sp_data->ns_sock);
	}
	
	closesocket(sp_data->sock);
	WSACloseEvent(sp_data->event);
	DeleteCriticalSection(&(sp_data->lock));
	
	return DP_OK;
}

HRESULT WINAPI r_SPInit(LPSPINITDATA);

HRESULT WINAPI SPInit(LPSPINITDATA data) {
	if(!IsEqualGUID(data->lpGuid, &DPSPGUID_IPX)) {
		return r_SPInit(data);
	}
	
	log_printf("SPInit: %p (lpAddress = %p, dwAddressSize = %u)", data->lpISP, data->lpAddress, (unsigned int)(data->dwAddressSize));
	
	{
		struct sp_data *sp_data;
		DWORD size;
		
		HRESULT r = IDirectPlaySP_GetSPData(data->lpISP, (void**)&sp_data, &size, DPGET_LOCAL);
		if(r != DP_OK) {
			log_printf("SPInit: GetSPData: %d", r);
			return DPERR_UNAVAILABLE;
		}
		
		if(sp_data) {
			log_printf("SPInit: Already initialised, returning DP_OK");
			return DP_OK;
		}
	}
	
	struct sp_data *sp_data = malloc(sizeof(struct sp_data));
	if(!sp_data) {
		return DPERR_UNAVAILABLE;
	}
	
	if(!InitializeCriticalSectionAndSpinCount(&(sp_data->lock), 0x80000000)) {
		log_printf("Error creating critical section: %s", w32_error(GetLastError()));
		
		free(sp_data);
		return DPERR_UNAVAILABLE;
	}
	
	if((sp_data->event = WSACreateEvent()) == WSA_INVALID_EVENT) {
		DeleteCriticalSection(&(sp_data->lock));
		free(sp_data);
		
		return DPERR_UNAVAILABLE;
	}
	
	if((sp_data->sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX)) == -1) {
		WSACloseEvent(sp_data->event);
		DeleteCriticalSection(&(sp_data->lock));
		free(sp_data);
		
		return DPERR_UNAVAILABLE;
	}
	
	struct sockaddr_ipx addr;
	memset(&addr, 0, sizeof(addr));
	addr.sa_family = AF_IPX;
	addr.sa_socket = 0;
	
	if(bind(sp_data->sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		closesocket(sp_data->sock);
		WSACloseEvent(sp_data->event);
		DeleteCriticalSection(&(sp_data->lock));
		free(sp_data);
		
		return DPERR_UNAVAILABLE;
	}
	
	int addrlen = sizeof(sp_data->addr);
	
	if(getsockname(sp_data->sock, (struct sockaddr*)&(sp_data->addr), &addrlen) == -1) {
		log_printf("getsockname failed: %s", w32_error(WSAGetLastError()));
		
		closesocket(sp_data->sock);
		WSACloseEvent(sp_data->event);
		DeleteCriticalSection(&(sp_data->lock));
		free(sp_data);
		
		return DPERR_UNAVAILABLE;
	}
	
	sp_data->ns_sock = -1;
	sp_data->ns_addr.sa_family = 0;
	sp_data->running = TRUE;
	sp_data->worker_thread = NULL;
	
	BOOL bcast = TRUE;
	setsockopt(sp_data->sock, SOL_SOCKET, SO_BROADCAST, (char*)&bcast, sizeof(BOOL));
	
	if(WSAEventSelect(sp_data->sock, sp_data->event, FD_READ) == -1) {
		log_printf("WSAEventSelect failed: %s", w32_error(WSAGetLastError()));
		
		closesocket(sp_data->sock);
		WSACloseEvent(sp_data->event);
		DeleteCriticalSection(&(sp_data->lock));
		free(sp_data);
		
		return DPERR_UNAVAILABLE;
	}
	
	HRESULT r = IDirectPlaySP_SetSPData(data->lpISP, sp_data, sizeof(*sp_data), DPSET_LOCAL);
	if(r != DP_OK) {
		log_printf("SetSPData: %d", (int)r);
		
		closesocket(sp_data->sock);
		WSACloseEvent(sp_data->event);
		DeleteCriticalSection(&(sp_data->lock));
		free(sp_data);
		
		return DPERR_UNAVAILABLE;
	}
	
	data->lpCB->EnumSessions = &IPX_EnumSessions;
	data->lpCB->Send = &IPX_Send;
	data->lpCB->SendEx = &IPX_SendEx;
	data->lpCB->Reply = &IPX_Reply;
	data->lpCB->CreatePlayer = &IPX_CreatePlayer;
	data->lpCB->GetCaps = &IPX_GetCaps;
	data->lpCB->Open = &IPX_Open;
	data->lpCB->CloseEx = &IPX_CloseEx;
	data->lpCB->ShutdownEx = &IPX_ShutdownEx;
	
	data->dwSPHeaderSize = API_HEADER_SIZE;
	data->dwSPVersion = DPSP_MAJORVERSIONMASK & DPSP_MAJORVERSION;
	
	return DP_OK;
}

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	if(why == DLL_PROCESS_ATTACH) {
		log_open("ipxwrapper.log");
		
		reg_open(KEY_QUERY_VALUE);
		
		log_calls = reg_get_char("log_calls", 0);
		
		reg_close();
	}else if(why == DLL_PROCESS_DETACH) {
		unload_dlls();
		log_close();
	}
	
	return TRUE;
}
