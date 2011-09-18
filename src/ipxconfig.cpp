/* IPXWrapper - Configuration tool
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
#include <windowsx.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>

#include "config.h"

#define ID_PRI_LIST 1

#define ID_NIC_LIST 11
#define ID_NIC_ENABLED 12
#define ID_NIC_NET 13
#define ID_NIC_NODE 14

#define ID_OPT_PORT 21
#define ID_OPT_W95 22
#define ID_OPT_BCAST 23
#define ID_OPT_FILTER 24
#define ID_OPT_LOG 25

#define ID_OK 31
#define ID_CANCEL 32
#define ID_APPLY 33

struct iface {
	/* C style strings used so they can be passed directly to the listview */
	char name[MAX_ADAPTER_DESCRIPTION_LENGTH+4];
	char hwaddr[18];
	
	char ipx_net[12];
	char ipx_node[18];
	
	bool enabled;
	bool primary;
	
	int pri_index;
};

typedef std::vector<iface> iface_list;

static void get_nics();
static bool reg_write(const char *name, void *value, size_t size);
static size_t reg_read(const char *name, void *buf, size_t max_size);
static bool save_config();
static bool store_nic();
static bool saddr_to_bin(unsigned char *bin, const char *str, int nbytes);
static void baddr_to_str(char *str, const unsigned char *bin, int nbytes);
static void init_windows();
static void update_nic_conf();
static void init_pri_list();
static HWND create_child(HWND parent, int x, int y, int w, int h, LPCTSTR class_name, LPCTSTR title, DWORD style = 0, DWORD ex_style = 0, unsigned int id = 0);
static HWND create_group(HWND parent, int x, int y, int w, int h, LPCTSTR title);
static int get_text_width(HWND hwnd, const char *txt);
static int get_text_height(HWND hwnd);
static RECT get_window_rect(HWND hwnd);
static std::string w32_errmsg(DWORD errnum);
static void die(std::string msg);

static iface_list nics;
static reg_global global_conf;
static HKEY regkey = NULL;
static unsigned char log_calls;

static std::string inv_error;
static HWND inv_window = NULL;

typedef LRESULT CALLBACK (*wproc_fptr)(HWND,UINT,WPARAM,LPARAM);
static wproc_fptr default_groupbox_wproc = NULL;

static struct {
	HWND main;
	
	HWND primary_group;
	HWND primary;
	
	HWND nic_group;
	HWND nic_list;
	HWND nic_enabled;
	HWND nic_net_lbl;
	HWND nic_net;
	HWND nic_node_lbl;
	HWND nic_node;
	
	HWND opt_group;
	HWND opt_w95;
	HWND opt_bcast;
	HWND opt_filter;
	HWND opt_log;
	HWND opt_port_lbl;
	HWND opt_port;
	
	HWND ok_btn;
	HWND can_btn;
	HWND app_btn;
} windows;

/* Callback for the main window */
static LRESULT CALLBACK main_wproc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
	switch(msg) {
		case WM_CLOSE: {
			DestroyWindow(window);
			break;
		}
		
		case WM_DESTROY: {
			PostQuitMessage(0);
			break;
		}
		
		case WM_NOTIFY: {
			NMHDR *nhdr = (NMHDR*)lp;
			
			if(nhdr->idFrom == ID_NIC_LIST) {
				if(nhdr->code == LVN_ITEMCHANGED) {
					NMLISTVIEW *lv = (NMLISTVIEW*)lp;
					
					if(lv->uNewState & LVIS_FOCUSED) {
						update_nic_conf();
					}
				}else if(nhdr->code == LVN_ITEMCHANGING) {
					NMLISTVIEW *lv = (NMLISTVIEW*)lp;
					
					if((lv->uOldState & LVIS_FOCUSED && !(lv->uNewState & LVIS_FOCUSED)) || (lv->uOldState & LVIS_SELECTED && !(lv->uNewState & LVIS_SELECTED))) {
						if(!store_nic()) {
							return TRUE;
						}
					}
				}
			}
			
			break;
		}
		
		case WM_COMMAND: {
			if(HIWORD(wp) == BN_CLICKED) {
				switch(LOWORD(wp)) {
					case ID_NIC_ENABLED: {
						int nic = ListView_GetNextItem(windows.nic_list, (LPARAM)-1, LVNI_FOCUSED);
						nics[nic].enabled = Button_GetCheck(windows.nic_enabled) == BST_CHECKED;
						
						init_pri_list();
						update_nic_conf();
						
						break;
					}
					
					case ID_OK: {
						if(save_config()) {
							PostMessage(windows.main, WM_CLOSE, 0, 0);
						}
						
						break;
					}
					
					case ID_CANCEL: {
						PostMessage(windows.main, WM_CLOSE, 0, 0);
						break;
					}
					
					case ID_APPLY: {
						save_config();
						break;
					}
					
					default:
						break;
				}
			}else if(HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == ID_PRI_LIST) {
				int nic = ComboBox_GetCurSel(windows.primary);
				
				for(iface_list::iterator i = nics.begin(); i != nics.end(); i++) {
					i->primary = (nic > 0 && i - nics.begin() == nic - 1);
				}
			}
			
			break;
		}
		
		case WM_SIZE: {
			int width = LOWORD(lp), height = HIWORD(lp);
			
			int edge = GetSystemMetrics(SM_CYEDGE);
			int text_h = get_text_height(windows.primary_group);
			int edit_h = text_h + 2 * edge;
			int pri_h = edit_h + text_h + 10;
			
			MoveWindow(windows.primary_group, 0, 0, width, pri_h, TRUE);
			MoveWindow(windows.primary, 10, text_h, width - 20, edit_h, TRUE);
			
			/* Buttons */
			
			RECT btn_rect = get_window_rect(windows.ok_btn);
			int btn_w = btn_rect.right - btn_rect.left;
			int btn_h = btn_rect.bottom - btn_rect.top;
			
			MoveWindow(windows.app_btn, width - btn_w - 6, height - btn_h - 6, btn_w, btn_h, TRUE);
			MoveWindow(windows.can_btn, width - btn_w * 2 - 12, height - btn_h - 6, btn_w, btn_h, TRUE);
			MoveWindow(windows.ok_btn, width - btn_w * 3 - 18, height - btn_h - 6, btn_w, btn_h, TRUE);
			
			/* Options groupbox */
			
			int lbl_w = get_text_width(windows.nic_net_lbl, "UDP port number");
			int edit_w = get_text_width(windows.nic_node, "000000");
			
			int opt_h = 5 * text_h + edit_h + 18;
			
			MoveWindow(windows.opt_group, 0, height - opt_h - btn_h - 12, width, opt_h, TRUE);
			
			int y = text_h;
			
			MoveWindow(windows.opt_port_lbl, 10, y + edge, lbl_w, text_h, TRUE);
			MoveWindow(windows.opt_port, 15 + lbl_w, y, edit_w, edit_h, TRUE);
			
			MoveWindow(windows.opt_filter, 10, y += edit_h + 2, width - 20, text_h, TRUE);
			MoveWindow(windows.opt_bcast, 10, y += text_h + 2, width - 20, text_h, TRUE);
			MoveWindow(windows.opt_w95, 10, y += text_h + 2, width - 20, text_h, TRUE);
			MoveWindow(windows.opt_log, 10, y += text_h + 2, width - 20, text_h, TRUE);
			
			/* NIC groupbox */
			
			lbl_w = get_text_width(windows.nic_net_lbl, "Network number");
			edit_w = get_text_width(windows.nic_node, "00:00:00:00:00:00");
			
			int net_h = height - pri_h - opt_h - btn_h - 12;
			
			MoveWindow(windows.nic_group, 0, pri_h, width, net_h, TRUE);
			
			y = net_h - (6 + edit_h);
			
			MoveWindow(windows.nic_node_lbl, 10, y + edge, lbl_w, text_h, TRUE);
			MoveWindow(windows.nic_node, 15 + lbl_w, y, edit_w, edit_h, TRUE);
			
			y -= 2 + edit_h;
			
			MoveWindow(windows.nic_net_lbl, 10, y + edge, lbl_w, text_h, TRUE);
			MoveWindow(windows.nic_net, 15 + lbl_w, y, edit_w, edit_h, TRUE);
			
			y -= 2 + edit_h;
			
			MoveWindow(windows.nic_enabled, 10, y, width - 20, text_h, TRUE);
			
			y -= 6;
			
			MoveWindow(windows.nic_list, 10, text_h, width - 20, y - text_h, TRUE);
			
			break;
		}
		
		default:
			return DefWindowProc(window, msg, wp, lp);
	}
	
	return 0;
}

/* Callback for groupboxes */
static LRESULT CALLBACK groupbox_wproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch(msg) {
		case WM_COMMAND:
		case WM_NOTIFY:
			return main_wproc(windows.main, msg, wp, lp);
		
		default:
			return default_groupbox_wproc(hwnd, msg, wp, lp);
	}
}

int main() {
	int err = RegOpenKeyEx(
		HKEY_CURRENT_USER,
		"Software\\IPXWrapper",
		0,
		KEY_QUERY_VALUE | KEY_SET_VALUE,
		&regkey
	);
	
	if(err != ERROR_SUCCESS) {
		if(err != ERROR_FILE_NOT_FOUND) {
			die("Error opening registry key: " + w32_errmsg(err));
		}
		
		regkey = NULL;
	}
	
	if(reg_read("global", &global_conf, sizeof(global_conf)) != sizeof(global_conf)) {
		global_conf.udp_port = DEFAULT_PORT;
		global_conf.w95_bug = 1;
		global_conf.bcast_all = 0;
		global_conf.filter = 1;
	}
	
	if(reg_read("log_calls", &log_calls, 1) != 1) {
		log_calls = 0;
	}
	
	get_nics();
	
	init_windows();
	
	MSG msg;
	BOOL mret;
	
	while((mret = GetMessage(&msg, NULL, 0, 0))) {
		if(mret == -1) {
			die("GetMessage failed: " + w32_errmsg(GetLastError()));
		}
		
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		
		if(inv_window && !PeekMessage(&msg, NULL, 0, 0, 0)) {
			MessageBox(windows.main, inv_error.c_str(), "Error", MB_OK);
			
			SetFocus(inv_window);
			Edit_SetSel(inv_window, 0, Edit_GetTextLength(inv_window));
			
			inv_window = NULL;
		}
	}
	
	RegCloseKey(regkey);
	
	return msg.wParam;
}

static void get_nics() {
	IP_ADAPTER_INFO tbuf;
	ULONG bufsize = sizeof(IP_ADAPTER_INFO);
	
	int rval = GetAdaptersInfo(&tbuf, &bufsize);
	if(rval != ERROR_SUCCESS && rval != ERROR_BUFFER_OVERFLOW) {
		die("Error getting network interfaces: " + w32_errmsg(rval));
	}
	
	IP_ADAPTER_INFO *buf = new IP_ADAPTER_INFO[bufsize / sizeof(tbuf)];
	
	rval = GetAdaptersInfo(buf, &bufsize);
	if(rval != ERROR_SUCCESS) {
		die("Error getting network interfaces: " + w32_errmsg(rval));
	}
	
	for(IP_ADAPTER_INFO *ptr = buf; ptr; ptr = ptr->Next) {
		if(ptr->AddressLength != 6) {
			continue;
		}
		
		iface new_if;
		
		strcpy(new_if.name, ptr->Description);
		
		baddr_to_str(new_if.hwaddr, ptr->Address, 6);
		
		strcpy(new_if.ipx_net, "00:00:00:01");
		strcpy(new_if.ipx_node, new_if.hwaddr);
		
		new_if.enabled = true;
		new_if.primary = false;
		
		reg_value regval;
		
		if(reg_read(new_if.hwaddr, &regval, sizeof(regval)) == sizeof(regval)) {
			baddr_to_str(new_if.ipx_net, regval.ipx_net, 4);
			baddr_to_str(new_if.ipx_node, regval.ipx_node, 6);
			
			new_if.enabled = regval.enabled;
			new_if.primary = regval.primary;
		}
		
		nics.push_back(new_if);
	}
	
	delete buf;
}

static bool reg_write(const char *name, void *value, size_t size) {
	if(!regkey) {
		int err = RegCreateKeyEx(
			HKEY_CURRENT_USER,
			"Software\\IPXWrapper",
			0,
			NULL,
			0,
			KEY_QUERY_VALUE | KEY_SET_VALUE,
			NULL,
			&regkey,
			NULL
		);
		
		if(err != ERROR_SUCCESS) {
			std::string msg = "Error creating registry key: " + w32_errmsg(err);
			MessageBox(NULL, msg.c_str(), "Error", MB_OK | MB_TASKMODAL | MB_ICONERROR);
			
			regkey = NULL;
			
			return false;
		}
	}
	
	int err = RegSetValueEx(regkey, name, 0, REG_BINARY, (BYTE*)value, size);
	if(err != ERROR_SUCCESS) {
		std::string msg = "Error writing value to registry: " + w32_errmsg(err);
		MessageBox(NULL, msg.c_str(), "Error", MB_OK | MB_TASKMODAL | MB_ICONERROR);
		
		return false;
	}
	
	return true;
}

static size_t reg_read(const char *name, void *buf, size_t max_size) {
	if(!regkey) {
		return 0;
	}
	
	DWORD size = max_size;
	
	int err = RegQueryValueEx(regkey, name, NULL, NULL, (BYTE*)buf, &size);
	if(err != ERROR_SUCCESS) {
		if(err != ERROR_MORE_DATA && err != ERROR_FILE_NOT_FOUND) {
			std::string msg = "Error reading value from registry: " + w32_errmsg(err);
			MessageBox(NULL, msg.c_str(), "Error", MB_OK | MB_TASKMODAL | MB_ICONERROR);
		}
		
		return 0;
	}
	
	return size;
}

static bool save_config() {
	if(!store_nic()) {
		return false;
	}
	
	char port_s[32], *endptr;
	GetWindowText(windows.opt_port, port_s, 32);
	
	int port = strtol(port_s, &endptr, 10);
	
	if(port < 1 || port > 65535 || *endptr) {
		MessageBox(windows.main, "Invalid port number.\nPort number must be an integer in the range 1 - 65535", "Error", MB_OK);
		
		SetFocus(windows.opt_port);
		Edit_SetSel(windows.opt_port, 0, Edit_GetTextLength(windows.opt_port));
		
		return false;
	}
	
	global_conf.udp_port = port;
	global_conf.w95_bug = Button_GetCheck(windows.opt_w95) == BST_CHECKED;
	global_conf.bcast_all = Button_GetCheck(windows.opt_bcast) == BST_CHECKED;
	global_conf.filter = Button_GetCheck(windows.opt_filter) == BST_CHECKED;
	log_calls = Button_GetCheck(windows.opt_log) == BST_CHECKED;
	
	for(iface_list::iterator i = nics.begin(); i != nics.end(); i++) {
		reg_value rval;
		
		saddr_to_bin(rval.ipx_net, i->ipx_net, 4);
		saddr_to_bin(rval.ipx_node, i->ipx_node, 6);
		rval.enabled = i->enabled;
		rval.primary = i->primary;
		
		if(!reg_write(i->hwaddr, &rval, sizeof(rval))) {
			return false;
		}
	}
	
	if(!reg_write("global", &global_conf, sizeof(global_conf)) || !reg_write("log_calls", &log_calls, 1)) {
		return false;
	}
	
	return true;
}

/* Fetch NIC settings from UI and store in NIC list */
static bool store_nic() {
	int selected_nic = ListView_GetNextItem(windows.nic_list, (LPARAM)-1, LVNI_FOCUSED);
	
	if(selected_nic == -1) {
		/* Return success if no NIC is selected */
		return true;
	}
	
	char net[32], node[32];
	
	GetWindowText(windows.nic_net, net, 32);
	GetWindowText(windows.nic_node, node, 32);
	
	if(!saddr_to_bin(NULL, net, 4)) {
		inv_error = "Network number is invalid.\nValid numbers are in the format XX:XX:XX:XX";
		inv_window = windows.nic_net;
		
		return false;
	}
	
	if(!saddr_to_bin(NULL, node, 6)) {
		inv_error = "Node number is invalid.\nValid numbers are in the format XX:XX:XX:XX:XX:XX";
		inv_window = windows.nic_node;
		
		return false;
	}
	
	strcpy(nics[selected_nic].ipx_net, net);
	strcpy(nics[selected_nic].ipx_node, node);
	
	return true;
}

/* Convert string format address to binary
 * Returns true on success or false if the string is invalid
*/
static bool saddr_to_bin(unsigned char *bin, const char *str, int nbytes) {
	for(int i = 0; i < nbytes; i++) {
		char term = (i+1 == nbytes ? '\0' : ':');
		
		if(isxdigit(str[0]) && (str[1] == term || (isxdigit(str[1]) && str[2] == term))) {
			if(bin) {
				bin[i] = strtoul(str, NULL, 16);
			}
			
			str += strcspn(str, ":");
			str += strspn(str, ":");
		}else{
			return false;
		}
	}
	
	return true;
}

/* Convert binary format address to string */
static void baddr_to_str(char *str, const unsigned char *bin, int nbytes) {
	for(int i = 0; i < nbytes; i++) {
		sprintf(str+(i*3), "%02X", bin[i]);
		
		if(i+1 < nbytes) {
			strcat(str, ":");
		}
	}
}

static void init_windows() {
	INITCOMMONCONTROLSEX common_controls;
	common_controls.dwSize = sizeof(common_controls);
	common_controls.dwICC = ICC_LISTVIEW_CLASSES;
	
	if(!InitCommonControlsEx(&common_controls)) {
		die("Failed to initialise common controls");
	}
	
	WNDCLASSEX wclass;
	wclass.cbSize = sizeof(wclass);
	wclass.style = 0;
	wclass.lpfnWndProc = &main_wproc;
	wclass.cbClsExtra = 0;
	wclass.cbWndExtra = 0;
	wclass.hInstance = GetModuleHandle(NULL);
	wclass.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(50), IMAGE_ICON, 32, 32, LR_SHARED);
	wclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wclass.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
	wclass.lpszMenuName  = NULL;
	wclass.lpszClassName = "ipxconfig_class";
	wclass.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(50), IMAGE_ICON, 16, 16, LR_SHARED);
	
	if(!RegisterClassEx(&wclass)) {
		die("Failed to register ipxconfig_class: " + w32_errmsg(GetLastError()));
	}
	
	windows.main = CreateWindow(
		"ipxconfig_class",
		"IPXWrapper configuration",
		WS_TILEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		370, 445,
		NULL,
		NULL,
		NULL,
		NULL
	);
	
	if(!windows.main) {
		die("Failed to create main window: " + w32_errmsg(GetLastError()));
	}
	
	windows.primary_group = create_group(windows.main, 0, 0, 400, 50, "Primary interface");
	
	int text_h = get_text_height(windows.primary_group);
	
	windows.primary = create_child(windows.primary_group, 10, text_h, 380, 300, WC_COMBOBOX, NULL, CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, ID_PRI_LIST);
	init_pri_list();
	
	{
		windows.nic_group = create_group(windows.main, 0, 0, 0, 0, "Network adapters");
		
		windows.nic_list = create_child(windows.nic_group, 0, 0, 0, 0, WC_LISTVIEW, NULL, LVS_SINGLESEL | LVS_REPORT | WS_TABSTOP | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, ID_NIC_LIST);
		//ListView_SetExtendedListViewStyle(windows.nic_list, LVS_EX_FULLROWSELECT);
		
		LVCOLUMN lvc;
		lvc.mask = LVCF_FMT;
		lvc.fmt = LVCFMT_LEFT;
		
		ListView_InsertColumn(windows.nic_list, 0, &lvc);
		
		LVITEM lvi;
		
		lvi.mask = LVIF_TEXT | LVIF_STATE;
		lvi.iItem = 0;
		lvi.iSubItem = 0;
		lvi.state = 0;
		lvi.stateMask = 0;
		
		for(iface_list::iterator i = nics.begin(); i != nics.end(); i++) {
			lvi.pszText = i->name;
			
			ListView_InsertItem(windows.nic_list, &lvi);
			lvi.iItem++;
		}
		
		ListView_SetColumnWidth(windows.nic_list, 0, LVSCW_AUTOSIZE);
		
		windows.nic_enabled = create_child(windows.nic_group, 0, 0, 0, 0, "BUTTON", "Enable interface", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_NIC_ENABLED);
		
		windows.nic_net_lbl = create_child(windows.nic_group, 0, 0, 0, 0, "STATIC", "Network number", SS_RIGHT);
		windows.nic_net = create_child(windows.nic_group, 0, 0, 0, 0, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_NIC_NET);
		
		windows.nic_node_lbl = create_child(windows.nic_group, 0, 0, 0, 0, "STATIC", "Node number", SS_RIGHT);
		windows.nic_node = create_child(windows.nic_group, 0, 0, 0, 0, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_NIC_NODE);
		
		update_nic_conf();
	}
	
	{
		windows.opt_group = create_group(windows.main, 0, 0, 0, 0, "Options");
		
		windows.opt_port_lbl = create_child(windows.opt_group, 0, 0, 0, 0, "STATIC", "UDP port number", SS_RIGHT);
		windows.opt_port = create_child(windows.opt_group, 0, 0, 0, 0, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_OPT_PORT);
		
		char port_s[8];
		sprintf(port_s, "%hu", global_conf.udp_port);
		
		SetWindowText(windows.opt_port, port_s);
		
		windows.opt_w95 = create_child(windows.opt_group, 0, 0, 0, 0, "BUTTON", "Enable Windows 95 SO_BROADCAST bug", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_OPT_W95);
		windows.opt_bcast = create_child(windows.opt_group, 0, 0, 0, 0, "BUTTON", "Send broadcast packets to all subnets", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_OPT_BCAST);
		windows.opt_filter = create_child(windows.opt_group, 0, 0, 0, 0, "BUTTON", "Filter receieved packets by subnet", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_OPT_FILTER);
		windows.opt_log = create_child(windows.opt_group, 0, 0, 0, 0, "BUTTON", "Log all WinSock API calls", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_OPT_LOG);
		
		Button_SetCheck(windows.opt_w95, global_conf.w95_bug ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.opt_bcast, global_conf.bcast_all ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.opt_filter, global_conf.filter ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.opt_log, log_calls ? BST_CHECKED : BST_UNCHECKED);
	}
	
	/* TODO: Size buttons dynamically */
	int btn_w = 75;
	int btn_h = 23;
	
	windows.ok_btn = create_child(windows.main, 0, 0, btn_w, btn_h, "BUTTON", "OK", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_OK);
	windows.can_btn = create_child(windows.main, 0, 0, btn_w, btn_h, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_CANCEL);
	windows.app_btn = create_child(windows.main, 0, 0, btn_w, btn_h, "BUTTON", "Apply", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_APPLY);
	
	ShowWindow(windows.main, SW_SHOW);
	UpdateWindow(windows.main);
}

static void update_nic_conf() {
	int selected_nic = ListView_GetNextItem(windows.nic_list, (LPARAM)-1, LVNI_FOCUSED);
	bool enabled = selected_nic >= 0 ? nics[selected_nic].enabled : false;
	
	EnableWindow(windows.nic_net, enabled);
	EnableWindow(windows.nic_node, enabled);
	EnableWindow(windows.nic_enabled, selected_nic >= 0);
	
	if(selected_nic >= 0) {
		Button_SetCheck(windows.nic_enabled, nics[selected_nic].enabled ? BST_CHECKED : BST_UNCHECKED);
		SetWindowText(windows.nic_net, nics[selected_nic].ipx_net);
		SetWindowText(windows.nic_node, nics[selected_nic].ipx_node);
	}
}

static void init_pri_list() {
	ComboBox_ResetContent(windows.primary);
	
	ComboBox_AddString(windows.primary, "Default");
	ComboBox_SetCurSel(windows.primary, 0);
	
	for(iface_list::iterator i = nics.begin(); i != nics.end(); i++) {
		if(i->enabled) {
			i->pri_index = ComboBox_AddString(windows.primary, i->name);
			
			if(i->primary) {
				ComboBox_SetCurSel(windows.primary, i->pri_index);
			}
		}else{
			i->pri_index = 0;
		}
	}
}

static HWND create_child(HWND parent, int x, int y, int w, int h, LPCTSTR class_name, LPCTSTR title, DWORD style, DWORD ex_style, unsigned int id) {
	static unsigned int idnum = 100;
	
	HWND hwnd = CreateWindowEx(
		ex_style,
		class_name,
		title,
		WS_CHILD | WS_VISIBLE | style,
		x, y, w, h,
		parent,
		(HMENU)(id ? id : idnum++),
		NULL,
		NULL
	);
	
	if(!hwnd) {
		die(std::string("Failed to create child window (") + class_name + "): " + w32_errmsg(GetLastError()));
	}
	
	SendMessage(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
	
	return hwnd;
}

static HWND create_group(HWND parent, int x, int y, int w, int h, LPCTSTR title) {
	HWND groupbox = create_child(parent, x, y, w, h, "BUTTON", title, BS_GROUPBOX);
	default_groupbox_wproc = (wproc_fptr)SetWindowLongPtr(groupbox, GWLP_WNDPROC, (LONG_PTR)&groupbox_wproc);
	
	return groupbox;
}

static int get_text_width(HWND hwnd, const char *txt) {
	HDC dc = GetDC(hwnd);
	if(!dc) {
		die("GetDC failed");
	}
	
	SIZE size;
	if(!GetTextExtentPoint32(dc, txt, strlen(txt), &size)) {
		die("GetTextExtentPoint32 failed");
	}
	
	ReleaseDC(hwnd, dc);
	
	return size.cx;
}

static int get_text_height(HWND hwnd) {
	HDC dc = GetDC(hwnd);
	if(!dc) {
		die("GetDC failed");
	}
	
	TEXTMETRIC tm;
	if(!GetTextMetrics(dc, &tm)) {
		die("GetTextMetrics failed");
	}
	
	ReleaseDC(hwnd, dc);
	
	return tm.tmHeight;
}

static RECT get_window_rect(HWND hwnd) {
	RECT rect;
	
	if(!GetWindowRect(hwnd, &rect)) {
		die("GetWindowRect failed: " + w32_errmsg(GetLastError()));
	}
	
	return rect;
}

/* Convert a win32 error number to a message */
static std::string w32_errmsg(DWORD errnum) {
	char buf[256] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, sizeof(buf), NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
}

static void die(std::string msg) {
	MessageBox(NULL, msg.c_str(), "Fatal error", MB_OK | MB_TASKMODAL | MB_ICONERROR);
	exit(1);
}
