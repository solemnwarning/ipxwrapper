/* ipxwrapper - Standalone router executable
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

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#include "router.h"
#include "common.h"
#include "config.h"

struct reg_global global_conf;

#define APPWM_TRAY (WM_APP+1)
#define MNU_EXIT 101

static void die(const char *fmt, ...);
static void init_ui();
static LRESULT CALLBACK tray_wproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void show_menu(HWND hwnd);

int main(int argc, char **argv) {
	log_open("ipxrouter.log");
	
	reg_open(KEY_QUERY_VALUE);
		
	if(reg_get_bin("global", &global_conf, sizeof(global_conf)) != sizeof(global_conf)) {
		global_conf.udp_port = DEFAULT_PORT;
		global_conf.w95_bug = 1;
		global_conf.bcast_all = 0;
		global_conf.filter = 1;
	}
	
	WSADATA wsdata;
	int err = WSAStartup(MAKEWORD(2,0), &wsdata);
	if(err) {
		die("Failed to initialize winsock: %s", w32_error(err));
	}
	
	struct router_vars *router = router_init(TRUE);
	if(!router) {
		die("Error while initializing router, check ipxrouter.log");
	}
	
	HANDLE worker = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&router_main, router, 0, NULL);
	if(!worker) {
		die("Failed to create router thread: %s", w32_error(GetLastError()));
	}
	
	init_ui();
	
	MSG msg;
	
	while(GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	EnterCriticalSection(&(router->crit_sec));
	
	router->running = FALSE;
	WSASetEvent(router->wsa_event);
	
	LeaveCriticalSection(&(router->crit_sec));
	
	if(WaitForSingleObject(worker, 3000) == WAIT_TIMEOUT) {
		log_printf("Router thread didn't exit in 3 seconds, terminating");
		TerminateThread(worker, 0);
	}
	
	CloseHandle(worker);
	router_destroy(router);
	
	WSACleanup();
	
	reg_close();
	
	log_close();
	
	return 0;
}

static void die(const char *fmt, ...) {
	va_list argv;
	char msg[512];
	
	va_start(argv, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argv);
	va_end(argv);
	
	MessageBox(NULL, msg, "Fatal error", MB_OK | MB_TASKMODAL);
	exit(1);
}

static void init_ui() {
	WNDCLASS wclass;
	
	wclass.style = 0;
	wclass.lpfnWndProc = &tray_wproc;
	wclass.cbClsExtra = 0;
	wclass.cbWndExtra = 0;
	wclass.hInstance = GetModuleHandle(NULL);
	wclass.hIcon = NULL;
	wclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wclass.hbrBackground = NULL;
	wclass.lpszMenuName = NULL;
	wclass.lpszClassName = "ipxrouter_tray";
	
	if(!RegisterClass(&wclass)) {
		die("RegisterClass: %s", w32_error(GetLastError()));
	}
	
	HWND window = CreateWindow(
		"ipxrouter_tray",
		"IPX Router",
		0,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		GetModuleHandle(NULL),
		NULL
	);
	
	if(!window) {
		die("CreateWindow: ", w32_error(GetLastError()));
	}
	
	HICON icon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(50), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
	if(!icon) {
		die("Error loading icon: ", w32_error(GetLastError()));
	}
	
	NOTIFYICONDATA tray;
	
	tray.cbSize = sizeof(tray);
	tray.hWnd = window;
	tray.uID = 1;
	tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	tray.uCallbackMessage = APPWM_TRAY;
	tray.hIcon = icon;
	strcpy(tray.szTip, "IPXWrapper Router");
	
	if(!Shell_NotifyIcon(NIM_ADD, &tray)) {
		die("Shell_NotifyIcon failed");
	}
}

static LRESULT CALLBACK tray_wproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch(msg) {
		case WM_CLOSE: {
			DestroyWindow(hwnd);
			break;
		}
		
		case WM_DESTROY: {
			PostQuitMessage(0);
			break;
		}
		
		case APPWM_TRAY: {
			if(lp == WM_LBUTTONUP || lp == WM_RBUTTONUP) {
				show_menu(hwnd);
			}
			
			break;
		}
		
		case WM_COMMAND: {
			if(wp == MNU_EXIT) {
				if(MessageBox(NULL, "If the router is stopped any existing sockets will become invalid.\nAre you sure you want to exit?", "IPXWrapper", MB_YESNO) == IDNO) {
					return 0;
				}
				
				NOTIFYICONDATA tray;
				tray.cbSize = sizeof(tray);
				tray.hWnd = hwnd;
				tray.uID = 1;
				tray.uFlags = 0;
				
				Shell_NotifyIcon(NIM_DELETE, &tray);
				DestroyWindow(hwnd);
			}
			
			break;
		}
		
		default: {
			return DefWindowProc(hwnd, msg, wp, lp);
		}
	}
	
	return 0;
}

static void show_menu(HWND hwnd) {
	POINT cursor_pos;
	
	GetCursorPos(&cursor_pos);
	
	SetForegroundWindow(hwnd);
	
	HMENU menu = CreatePopupMenu();
	if(!menu) {
		die("CreatePopupMenu: %s", w32_error(GetLastError()));
	}
	
	InsertMenu(menu, -1, MF_BYPOSITION | MF_STRING, MNU_EXIT, "Exit");
	
	SetMenuDefaultItem(menu, MNU_EXIT, FALSE);
	
	SetFocus(hwnd);
	
	TrackPopupMenu(
		menu,
		TPM_LEFTALIGN | TPM_BOTTOMALIGN,
		cursor_pos.x, cursor_pos.y,
		0,
		hwnd,
		NULL
	);
	
	DestroyMenu(menu);
}
