/* IPXWrapper test tools
 * Copyright (C) 2015 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <dplay.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

DEFINE_GUID(TEST_APP_GUID, 0x38bef9eb, 0x6ad3, 0x4fc9, 0x97, 0xff, 0xfc, 0x3c, 0x7a, 0x55, 0x3b, 0x29);

static HANDLE exit_event = NULL;

#define assert_dp_call(name, expr) \
{ \
	HRESULT err = (expr); \
	if(err != DP_OK) \
	{ \
		fprintf(stderr, \
			__FILE__ ":%u: " name ": %u\n", \
			(unsigned int)(__LINE__), \
			(unsigned int)(err)); \
		exit(1); \
	} \
}

static void lock_printf(const char *fmt, ...)
{
	static CRITICAL_SECTION cs;
	static bool cs_init = false;
	
	if(!cs_init)
	{
		InitializeCriticalSection(&cs);
		cs_init = true;
	}
	
	EnterCriticalSection(&cs);
	
	va_list argv;
	va_start(argv, fmt);
	vprintf(fmt, argv);
	va_end(argv);
	
	LeaveCriticalSection(&cs);
}

static BOOL FAR PASCAL copy_ipx_conn(
	LPCGUID *lpguidSP,
	LPVOID lpConnection,
	DWORD dwConnectionSize,
	LPCDPNAME lpName,
	DWORD dwFlags,
	LPVOID lpContext)
{
	if(memcmp(lpguidSP, &DPSPGUID_IPX, sizeof(DPSPGUID_IPX)) == 0)
	{
		assert(*(void**)(lpContext) = malloc(dwConnectionSize));
		memcpy(*(void**)(lpContext), lpConnection, dwConnectionSize);
	}
	
	return TRUE;
}

static BOOL FAR PASCAL list_sessions_cb(
	LPCDPSESSIONDESC2 lpThisSD,
	LPDWORD lpdwTimeout,
	DWORD dwFlags,
	LPVOID lpContext)
{
	if(lpThisSD)
	{
		unsigned char *uuid_str;
		assert(UuidToString((UUID*)&(lpThisSD->guidInstance), &uuid_str) == RPC_S_OK);
		
		lock_printf("session %s %s\n", uuid_str, lpThisSD->lpszSessionNameA);
		
		RpcStringFree(&uuid_str);
		
		return TRUE;
	}
	else{
		/* Last session detected in this pass, return FALSE to make the
		 * invoking IDirectPlayX_EnumConnections call return rather than
		 * searching forever.
		*/
		return FALSE;
	}
}

static BOOL FAR PASCAL list_players_cb(
	DPID dpId,
	DWORD dwPlayerType,
	LPCDPNAME lpName,
	DWORD dwFlags,
	LPVOID lpContext)
{
	lock_printf("player %u %s\n", (unsigned int)(dpId), lpName->lpszLongName);
	return TRUE;
}

static DWORD WINAPI recv_thread_main(LPVOID lpParameter)
{
	IDirectPlay4 *dp = (IDirectPlay4*)(lpParameter);
	
	DWORD bufsize = 0;
	char *buf = NULL;
	
	while(1)
	{
		DPID player_from, player_to;
		
		HRESULT err = IDirectPlayX_Receive(dp, &player_from, &player_to, 0, buf, &bufsize);
		
		if(WaitForSingleObject(exit_event, 0) == WAIT_OBJECT_0)
		{
			/* We are exiting */
			return 0;
		}
		
		if(err == DPERR_BUFFERTOOSMALL)
		{
			assert(buf = realloc(buf, bufsize));
			continue;
		}
		else if(err == DPERR_NOMESSAGES)
		{
			/* non-blocking I/O... ugh */
			Sleep(50);
			continue;
		}
		else if(err != DP_OK)
		{
			fprintf(stderr, "IDirectPlay4::Receive: %u\n", (unsigned int)(err));
			exit(1);
		}
		
		if(player_from != DPID_SYSMSG)
		{
			lock_printf("message %u %u %s\n",
				(unsigned int)(player_from),
				(unsigned int)(player_to),
				buf);
		}
	}
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	exit_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	assert(exit_event);
	
	{
		HRESULT err = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if(err != S_OK)
		{
			fprintf(stderr, "CoInitializeEx: %u\n", (unsigned int)(err));
			return 1;
		}
	}
	
	IDirectPlay4 *dp;
	assert_dp_call("CoCreateInstance",
		CoCreateInstance(
			&CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectPlay4A, (void**)(&dp)));
	
	{
		void *conn = NULL;
		
		assert_dp_call("IDirectPlay4::EnumConnections",
			IDirectPlayX_EnumConnections(
				dp, NULL, (LPDPENUMCONNECTIONSCALLBACK)(&copy_ipx_conn), &conn, 0));
		
		assert_dp_call("IDirectPlay4::InitializeConnection",
			IDirectPlayX_InitializeConnection(dp, conn, 0));
		
		free(conn);
	}
	
	HANDLE recv_thread = CreateThread(NULL, 0, &recv_thread_main, dp, 0, NULL);
	assert(recv_thread);
	
	char line[1024];
	while(lock_printf("ready\n"), fgets(line, sizeof(line), stdin))
	{
		char *cmd = strtok(line, " \n");
		
		if(strcmp(cmd, "create_session") == 0)
		{
			char *session_name = strtok(NULL, " \n");
			
			DPSESSIONDESC2 session;
			memset(&session, 0, sizeof(session));
			
			session.dwSize           = sizeof(session);
			session.guidApplication  = TEST_APP_GUID;
			session.lpszSessionNameA = session_name;
			
			assert_dp_call("IDirectPlay4::Open",
				       IDirectPlayX_Open(dp, &session, DPOPEN_CREATE));
			
			DWORD bufsize = 0;
			
			/* Need to call GetSessionDesc() to get the session
			 * GUID as Open() doesn't fill it in.
			*/
			
			assert(IDirectPlayX_GetSessionDesc(dp, NULL, &bufsize) == DPERR_BUFFERTOOSMALL);
			
			DPSESSIONDESC2 *sd = malloc(bufsize);
			assert(sd);
			
			assert_dp_call("IDirectPlay4::GetSessionDesc",
				IDirectPlayX_GetSessionDesc(dp, sd, &bufsize));
			
			unsigned char *uuid_str;
			assert(UuidToString((UUID*)&(sd->guidInstance), &uuid_str) == RPC_S_OK);
			
			lock_printf("session_guid %s\n", uuid_str);
			
			RpcStringFree(&uuid_str);
			
			free(sd);
		}
		else if(strcmp(cmd, "list_sessions") == 0)
		{
			DPSESSIONDESC2 session;
			memset(&session, 0, sizeof(session));
			
			session.dwSize          = sizeof(session);
			session.guidApplication = TEST_APP_GUID;
			
			assert_dp_call("IDirectPlay4::EnumSessions",
				IDirectPlayX_EnumSessions(dp, &session, 3000, &list_sessions_cb, NULL, 0));
		}
		else if(strcmp(cmd, "join_session") == 0)
		{
			char *session_guid = strtok(NULL, " \n");
			
			DPSESSIONDESC2 session;
			memset(&session, 0, sizeof(session));
			
			session.dwSize = sizeof(session);
			UuidFromString((RPC_CSTR)(session_guid), &(session.guidInstance));
			
			assert_dp_call("IDirectPlay4::Open",
				IDirectPlayX_Open(dp, &session, DPOPEN_JOIN));
		}
		else if(strcmp(cmd, "create_player") == 0)
		{
			DPID player_id;
			
			DPNAME name;
			memset(&name, 0, sizeof(name));
			
			name.dwSize        = sizeof(name);
			name.lpszLongNameA = strtok(NULL, " \n");
			
			assert_dp_call("IDirectPlay4::CreatePlayer",
				IDirectPlayX_CreatePlayer(dp, &player_id, &name, NULL, NULL, 0, 0));
			
			lock_printf("player_id %u\n", (unsigned int)(player_id));
		}
		else if(strcmp(cmd, "list_players") == 0)
		{
			IDirectPlayX_EnumPlayers(dp, NULL, &list_players_cb, NULL, DPENUMPLAYERS_ALL);
		}
		else if(strcmp(cmd, "send_message") == 0)
		{
			DPID player_from = strtoul(strtok(NULL, " \n"), NULL, 10);
			DPID player_to   = strtoul(strtok(NULL, " \n"), NULL, 10);
			char *message    = strtok(NULL, " \n");
			
			assert_dp_call("IDirectPlay4::Send",
				IDirectPlayX_Send(dp, player_from, player_to, 0, message, strlen(message) + 1));
		}
		else if(strcmp(cmd, "exit") == 0)
		{
			break;
		}
	}
	
	SetEvent(exit_event);
	WaitForSingleObject(recv_thread, INFINITE);
	
	CloseHandle(recv_thread);
	CloseHandle(exit_event);
	
	IDirectPlayX_Release(dp);
	
	CoUninitialize();
	
	return 0;
}
