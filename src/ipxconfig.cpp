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
#include <iostream>
#include <assert.h>
#include <string>
#include <vector>
#include <stdint.h>

#include "config.h"

#define ID_NIC_LIST 1
#define ID_NIC_ENABLE 2
#define ID_NIC_PRIMARY 3
#define ID_SAVE 4
#define ID_NIC_NET 5
#define ID_NIC_NODE 6
#define ID_W95_BUG 7
#define ID_UDP_BTN 8
#define ID_BCAST_ALL 9
#define ID_FILTER 10
#define ID_LOG 11

#define ID_DIALOG_TXT 1
#define ID_DIALOG_OK 2
#define ID_DIALOG_CANCEL 3

struct iface {
	/* C style strings used so they can be passed directly to the listview */
	char name[MAX_ADAPTER_DESCRIPTION_LENGTH+4];
	char hwaddr[18];
	
	char ipx_net[12];
	char ipx_node[18];
	
	bool enabled;
	bool primary;
};

typedef std::vector<iface> iface_list;

static void addr_input_dialog(const char *desc, char *dest, int size);
static void get_nics();
static void set_primary(unsigned int id);
static void save_nics();
static void saddr_to_bin(unsigned char *bin, const char *str);
static void baddr_to_str(char *str, const unsigned char *bin, int size);
static void init_windows();
static void update_nic_conf();
static HWND create_child(HWND parent, int x, int y, int w, int h, LPCTSTR class_name, LPCTSTR title, DWORD style = 0, DWORD ex_style = 0, unsigned int id = 0);
static void add_list_column(HWND hwnd, int id, char *text, int width);
static int get_text_width(HWND hwnd, const char *txt);
static int get_text_height(HWND hwnd);
static std::string w32_errmsg(DWORD errnum);
static void die(std::string msg);

static iface_list nics;
static reg_global global_conf;
static HKEY regkey = NULL;
static unsigned char log_calls;

typedef LRESULT CALLBACK (*wproc_fptr)(HWND,UINT,WPARAM,LPARAM);
static wproc_fptr groupbox_wproc = NULL;

static struct {
	HWND main;
	HWND nic_list;
	
	HWND nic_conf;
	HWND nic_net;
	HWND nic_node;
	HWND nic_enabled;
	HWND nic_primary;
	
	HWND global_conf;
	HWND w95_bug;
	HWND bcast_all;
	HWND filter;
	HWND log;
	
	HWND button_box;
} windows;

/* Callback for the main window */
static LRESULT CALLBACK main_wproc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
	switch(msg) {
		case WM_CLOSE:
			DestroyWindow(window);
			break;
			
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		
		case WM_NOTIFY:
			NMHDR *nhdr = (NMHDR*)lp;
			
			if(nhdr->idFrom == ID_NIC_LIST) {
				if(nhdr->code == LVN_GETDISPINFO) {
					NMLVDISPINFO *di = (NMLVDISPINFOA*)lp;
					
					char *values[] = {
						nics[di->item.iItem].name,
						nics[di->item.iItem].ipx_net,
						nics[di->item.iItem].ipx_node,
						(char*)(nics[di->item.iItem].enabled ? "Yes" : "No"),
						(char*)(nics[di->item.iItem].primary ? "Yes" : "No")
					};
					
					di->item.pszText = values[di->item.iSubItem];
					di->item.cchTextMax = strlen(di->item.pszText);
				}else if(nhdr->code == LVN_ITEMCHANGED) {
					NMLISTVIEW *lv = (NMLISTVIEW*)lp;
					
					if(lv->uNewState & LVIS_FOCUSED) {
						update_nic_conf();
					}
				}
			}
			
			break;
			
		case WM_COMMAND:
			if(HIWORD(wp) == BN_CLICKED) {
				int selected_nic = ListView_GetNextItem(windows.nic_list, (LPARAM)-1, LVNI_FOCUSED);
				
				switch(LOWORD(wp)) {
					case ID_NIC_ENABLE:
						nics[selected_nic].enabled = Button_GetCheck(windows.nic_enabled) == BST_CHECKED;
						update_nic_conf();
						
						ListView_Update(windows.nic_list, selected_nic);
						
						break;
					
					case ID_NIC_PRIMARY:
						if(Button_GetCheck(windows.nic_primary) == BST_CHECKED) {
							set_primary(selected_nic);
						}else{
							nics[selected_nic].primary = false;
						}
						
						for(unsigned int i = 0; i < nics.size(); i++) {
							ListView_Update(windows.nic_list, i);
						}
						
						break;
					
					case ID_NIC_NET:
						addr_input_dialog("Network number", nics[selected_nic].ipx_net, 4);
						break;
					
					case ID_NIC_NODE:
						addr_input_dialog("Node number", nics[selected_nic].ipx_node, 6);
						break;
					
					case ID_SAVE:
						save_nics();
						break;
					
					case ID_W95_BUG:
						global_conf.w95_bug = Button_GetCheck(windows.w95_bug) == BST_CHECKED;
						break;
					
					case ID_UDP_BTN:
						addr_input_dialog("UDP port", NULL, 2);
						break;
					
					case ID_BCAST_ALL:
						global_conf.bcast_all = Button_GetCheck(windows.bcast_all) == BST_CHECKED;
						break;
					
					case ID_FILTER:
						global_conf.filter = Button_GetCheck(windows.filter) == BST_CHECKED;
						break;
					
					case ID_LOG:
						log_calls = Button_GetCheck(windows.log) == BST_CHECKED;
						break;
					
					default:
						break;
				}
			}
			
			break;
		
		case WM_SIZE:
			int width = LOWORD(lp), height = HIWORD(lp);
			
			RECT rect;
			
			assert(GetWindowRect(windows.nic_conf, &rect));
			int conf_h = rect.bottom - rect.top;
			
			assert(GetWindowRect(windows.global_conf, &rect));
			int gc_h = rect.bottom - rect.top;
			
			assert(GetWindowRect(windows.button_box, &rect));
			int btn_w = rect.right - rect.left;
			
			MoveWindow(windows.nic_list, 0, 0, width, height-conf_h-gc_h, TRUE);
			MoveWindow(windows.nic_conf, 0, height-conf_h-gc_h, width, conf_h, TRUE);
			MoveWindow(windows.global_conf, 0, height-gc_h, width-btn_w-5, gc_h, TRUE);
			MoveWindow(windows.button_box, width-btn_w, height-gc_h, btn_w, gc_h, TRUE);
		
			break;
		
		default:
			return DefWindowProc(window, msg, wp, lp);
	}
	
	return 0;
}

/* Callback for the NIC config groupbox */
static LRESULT CALLBACK conf_group_wproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if(msg == WM_COMMAND) {
		PostMessage(windows.main, msg, wp, lp);
		return 0;
	}
	
	return groupbox_wproc(hwnd, msg, wp, lp);
}

struct addr_dialog_vars {
	const char *desc;
	
	char *dest;
	int size;
};

/* Callback for address entry dialog */
static LRESULT CALLBACK addr_dialog_wproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	static addr_dialog_vars *vars = NULL;
	
	switch(msg) {
		case WM_CREATE:
			CREATESTRUCT *cs = (CREATESTRUCT*)lp;
			vars = (addr_dialog_vars*)cs->lpCreateParams;
			
			if(vars->size == 2) {
				char buf[16];
				sprintf(buf, "%hu", global_conf.udp_port);
				vars->dest = buf;
				
				SetWindowText(hwnd, "Set UDP port");
			}
			
			HWND edit = create_child(hwnd, 0, 0, 0, 0, "EDIT", vars->dest, WS_TABSTOP, WS_EX_CLIENTEDGE, ID_DIALOG_TXT);
			HWND ok = create_child(hwnd, 0, 0, 0, 0, "BUTTON", "OK", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_DIALOG_OK);
			HWND cancel = create_child(hwnd, 0, 0, 0, 0, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_DIALOG_CANCEL);
			
			int edge = GetSystemMetrics(SM_CYEDGE);
			int height = get_text_height(edit) + 2*edge;
			
			int edit_w = get_text_width(edit, "FF:FF:FF:FF:FF:FF") + 2*edge;
			int btn_w = (int)(get_text_width(cancel, "Cancel") * 1.5) + 2*edge;
			
			MoveWindow(edit, 5, 5, edit_w, height, TRUE);
			MoveWindow(ok, edit_w+10, 5, btn_w, height, TRUE);
			MoveWindow(cancel, edit_w+btn_w+15, 5, btn_w, height, TRUE);
			
			RECT crect, brect;
			assert(GetClientRect(hwnd, &crect));
			assert(GetWindowRect(hwnd, &brect));
			
			int win_w = ((brect.right - brect.left) - crect.right) + edit_w + 2*btn_w + 20;
			int win_h = ((brect.bottom - brect.top) - crect.bottom) + height + 10;
			
			assert(GetWindowRect(windows.main, &brect));
			
			int win_x = brect.left + (brect.right - brect.left) / 2 - win_w / 2;
			int win_y = brect.top + (brect.bottom - brect.top) / 2 - win_h / 2;
			
			MoveWindow(hwnd, win_x, win_y, win_w, win_h, TRUE);
			
			EnableWindow(windows.main, FALSE);
			
			ShowWindow(hwnd, SW_SHOW);
			UpdateWindow(hwnd);
			
			break;
		
		case WM_CLOSE:
			delete vars;
			
			EnableWindow(windows.main, TRUE);
			SetFocus(windows.main);
			
			DestroyWindow(hwnd);
			
			break;
		
		case WM_COMMAND:
			if(HIWORD(wp) == BN_CLICKED) {
				int btn = LOWORD(wp);
				
				if(btn == ID_DIALOG_OK) {
					char text[256];
					GetWindowText(GetDlgItem(hwnd, ID_DIALOG_TXT), text, sizeof(text));
					
					if(vars->size == 2) {
						char *endptr;
						int p = strtol(text, &endptr, 10);
						
						if(p < 1 || p > 65535) {
							MessageBox(hwnd, "Invalid port number specified", NULL, MB_OK| MB_ICONERROR);
						}else{
							global_conf.udp_port = p;
							PostMessage(hwnd, WM_CLOSE, 0, 0);
						}
						
						return 0;
					}
					
					int buf[6];
					
					if(sscanf(text, "%02X:%02X:%02X:%02X:%02X:%02X", &buf[0], &buf[1], &buf[2], &buf[3], &buf[4], &buf[5]) == vars->size) {
						unsigned char uc[] = {buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]};
						unsigned char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
						unsigned char z6[] = {0,0,0,0,0,0};
						
						if(memcmp(uc, f6, vars->size) == 0 || memcmp(uc, z6, vars->size) == 0) {
							std::string m = "This is not a valid network/node number.\n";
							m += "Using it may cause problems. Use it anyway?";
							
							if(MessageBox(hwnd, m.c_str(), "Warning", MB_YESNO | MB_ICONWARNING) != IDYES) {
								return 0;
							}
						}
						
						baddr_to_str(vars->dest, uc, vars->size);
						
						int nic = ListView_GetNextItem(windows.nic_list, (LPARAM)-1, LVNI_FOCUSED);
						
						ListView_Update(windows.nic_list, nic);
						PostMessage(hwnd, WM_CLOSE, 0, 0);
					}else{
						std::string m = std::string("Invalid ") + vars->desc;
						MessageBox(hwnd, m.c_str(), NULL, MB_OK | MB_ICONEXCLAMATION);
					}
				}else if(btn == ID_DIALOG_CANCEL) {
					PostMessage(hwnd, WM_CLOSE, 0, 0);
				}
			}
			
			break;
		
		default:
			return DefWindowProc(hwnd, msg, wp, lp);
	};
	
	return 0;
}

static void addr_input_dialog(const char *desc, char *dest, int size) {
	addr_dialog_vars *v = new addr_dialog_vars;
	
	v->desc = desc;
	v->dest = dest;
	v->size = size;
	
	assert(CreateWindow(
		"ipxconfig_dialog",
		desc,
		WS_DLGFRAME,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		windows.main,
		NULL,
		NULL,
		v
	));
}

int main() {
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
		die("Error opening registry: " + w32_errmsg(err));
	}
	
	DWORD gsize = sizeof(global_conf);
	
	if(!regkey || RegQueryValueEx(regkey, "global", NULL, NULL, (BYTE*)&global_conf, &gsize) != ERROR_SUCCESS || gsize != sizeof(global_conf)) {
		global_conf.udp_port = DEFAULT_PORT;
		global_conf.w95_bug = 1;
		global_conf.bcast_all = 0;
		global_conf.filter = 1;
	}
	
	gsize = 1;
	
	if(!regkey || RegQueryValueEx(regkey, "log_calls", NULL, NULL, (BYTE*)&log_calls, &gsize) != ERROR_SUCCESS || gsize != 1) {
		log_calls = 0;
	}
	
	get_nics();
	
	init_windows();
	
	MSG msg;
	BOOL mret;
	
	while((mret = GetMessage(&msg, NULL, 0, 0))) {
		assert(mret != -1);
		
		TranslateMessage(&msg);
		DispatchMessage(&msg);
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
		
		reg_value rval;
		DWORD rsize = sizeof(rval);
		bool primary = false;
		
		if(RegQueryValueEx(regkey, new_if.hwaddr, NULL, NULL, (BYTE*)&rval, &rsize) == ERROR_SUCCESS && rsize == sizeof(rval)) {
			baddr_to_str(new_if.ipx_net, rval.ipx_net, 4);
			baddr_to_str(new_if.ipx_node, rval.ipx_node, 6);
			
			new_if.enabled = rval.enabled;
			primary = rval.primary;
		}
		
		nics.push_back(new_if);
		
		if(primary) {
			set_primary(nics.size()-1);
		}
	}
	
	delete buf;
}

static void set_primary(unsigned int id) {
	for(unsigned int i = 0; i < nics.size(); i++) {
		nics[i].primary = i == id ? true : false;
	}
}

static bool reg_write(const char *name, void *value, size_t size) {
	int err = RegSetValueEx(regkey, name, 0, REG_BINARY, (BYTE*)value, size);
	if(err != ERROR_SUCCESS) {
		std::string msg = "Error writing value to registry: " + w32_errmsg(err);
		MessageBox(NULL, msg.c_str(), "Error", MB_OK | MB_TASKMODAL | MB_ICONERROR);
		
		return false;
	}
	
	return true;
}

static void save_nics() {
	for(iface_list::iterator i = nics.begin(); i != nics.end(); i++) {
		reg_value rval;
		
		saddr_to_bin(rval.ipx_net, i->ipx_net);
		saddr_to_bin(rval.ipx_node, i->ipx_node);
		rval.enabled = i->enabled;
		rval.primary = i->primary;
		
		if(!reg_write(i->hwaddr, &rval, sizeof(rval))) {
			return;
		}
	}
	
	if(!reg_write("global", &global_conf, sizeof(global_conf)) || !reg_write("log_calls", &log_calls, 1)) {
		return;
	}
	
	MessageBox(
		windows.main,
		"Settings saved successfully",
		"Message",
		MB_OK | MB_ICONINFORMATION
	);
}

/* Convert string format address to binary
 * NO SANITY CHECKS, ENSURE INPUT DATA IS VALID AND BUFFER SIZE SUFFICIENT
*/
static void saddr_to_bin(unsigned char *bin, const char *str) {
	unsigned int i = 0;
	
	while(*str) {
		bin[i++] = strtoul(str, NULL, 16);
		str += strcspn(str, ":");
		str += strspn(str, ":");
	}
}

/* Convert binary format address to string
 * Same warnings as above
*/
static void baddr_to_str(char *str, const unsigned char *bin, int size) {
	for(int i = 0; i < size; i++) {
		sprintf(str+(i*3), "%02X", bin[i]);
		
		if(i+1 < size) {
			strcat(str, ":");
		}
	}
}

static void init_windows() {
	WNDCLASS wclass;
	
	memset(&wclass, 0, sizeof(wclass));
	wclass.style = 0;
	wclass.lpfnWndProc = &main_wproc;
	wclass.hInstance = GetModuleHandle(NULL);
	wclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wclass.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
	wclass.lpszMenuName  = NULL;
	wclass.lpszClassName = "ipxconfig_class";
	
	assert(RegisterClass(&wclass));
	
	wclass.lpfnWndProc = &addr_dialog_wproc;
	wclass.lpszClassName = "ipxconfig_dialog";
	
	assert(RegisterClass(&wclass));
	
	windows.main = CreateWindow(
		"ipxconfig_class",
		"IPXWrapper configuration",
		WS_TILEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		800, 500,
		NULL,
		NULL,
		NULL,
		NULL
	);
	
	assert(windows.main);
	
	windows.nic_list = create_child(windows.main, 0, 0, 0, 0, WC_LISTVIEW, NULL, LVS_SINGLESEL | LVS_REPORT | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_NIC_LIST);
	
	ListView_SetExtendedListViewStyle(windows.nic_list, LVS_EX_FULLROWSELECT);
	
	add_list_column(windows.nic_list, 0, "Name", 200);
	add_list_column(windows.nic_list, 1, "IPX Network Number", 120);
	add_list_column(windows.nic_list, 2, "IPX Node Number", 150);
	add_list_column(windows.nic_list, 3, "Enabled", 60);
	add_list_column(windows.nic_list, 4, "Primary", 60);
	
	windows.nic_conf = create_child(windows.main, 0, 0, 0, 0, "BUTTON", "Interface settings", BS_GROUPBOX);
	
	int window_edge = GetSystemMetrics(SM_CYEDGE);
	int text_h = get_text_height(windows.nic_conf);
	int row_h = window_edge*2 + text_h;
	int btn_w = get_text_width(windows.nic_conf, "Set IPX network number...") + 2*window_edge;
	
	{
		int cbox_w = get_text_width(windows.nic_conf, "Make primary interface");
		
		MoveWindow(windows.nic_conf, 0, 0, 0, text_h + 2*row_h + 15, TRUE);
		
		windows.nic_net = create_child(windows.nic_conf, 10, text_h, btn_w, row_h, "BUTTON", "Set IPX network number...", WS_TABSTOP | BS_PUSHBUTTON, 0, ID_NIC_NET);
		windows.nic_node = create_child(windows.nic_conf, 10, text_h+row_h+5, btn_w, row_h, "BUTTON", "Set IPX node number...", WS_TABSTOP | BS_PUSHBUTTON, 0, ID_NIC_NODE);
		
		windows.nic_enabled = create_child(windows.nic_conf, 20+btn_w, text_h, cbox_w, row_h, "BUTTON", "Enable interface", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_NIC_ENABLE);
		windows.nic_primary = create_child(windows.nic_conf, 20+btn_w, text_h+row_h+5, cbox_w, row_h, "BUTTON", "Make primary interface", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_NIC_PRIMARY);
		
		update_nic_conf();
	}
	
	{
		windows.global_conf = create_child(windows.main, 0, 0, 0, text_h + 2*row_h + 15, "BUTTON", "Global settings", BS_GROUPBOX);
		
		int cbox_w = get_text_width(windows.global_conf, "Enable Win 95 SO_BROADCAST bug");
		
		create_child(windows.global_conf, 10, text_h, btn_w, row_h, "BUTTON", "Set UDP port...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_UDP_BTN);
		windows.w95_bug = create_child(windows.global_conf, btn_w+20, text_h, cbox_w, row_h, "BUTTON", "Enable Win 95 SO_BROADCAST bug", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_W95_BUG);
		windows.bcast_all = create_child(windows.global_conf, btn_w+20, text_h+row_h+5, cbox_w, row_h, "BUTTON", "Send broadcasts to all subnets", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_BCAST_ALL);
		
		int cbox2_w = get_text_width(windows.global_conf, "Filter received packets by subnet");
		
		windows.filter = create_child(windows.global_conf, btn_w+cbox_w+30, text_h, cbox2_w, row_h, "BUTTON", "Filter received packets by subnet", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_FILTER);
		windows.log = create_child(windows.global_conf, btn_w+cbox_w+30, text_h+row_h+5, cbox2_w, row_h, "BUTTON", "Log all WinSock calls", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_LOG);
		
		Button_SetCheck(windows.w95_bug, global_conf.w95_bug ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.bcast_all, global_conf.bcast_all ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.filter, global_conf.filter ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.log, log_calls ? BST_CHECKED : BST_UNCHECKED);
	}
	
	{
		windows.button_box = create_child(windows.main, 0, 0, 0, 0, "BUTTON", NULL, BS_GROUPBOX);
		
		int btn_w = get_text_width(windows.button_box, "Save settings") + 2*window_edge;
		create_child(windows.button_box, 10, text_h, btn_w, row_h, "BUTTON", "Save settings", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_SAVE);
		
		MoveWindow(windows.button_box, 0, 0, btn_w+20, 0, TRUE);
	}
	
	LVITEM lvi;
	
	lvi.mask = LVIF_TEXT | LVIF_STATE;
	lvi.iItem = 0;
	lvi.iSubItem = 0;
	lvi.state = 0;
	lvi.stateMask = 0;
	lvi.pszText = LPSTR_TEXTCALLBACK;
	
	for(iface_list::iterator i = nics.begin(); i != nics.end(); i++) {
		ListView_InsertItem(windows.nic_list, &lvi);
		lvi.iItem++;
	}
	
	ShowWindow(windows.main, SW_SHOW);
	UpdateWindow(windows.main);
	
	groupbox_wproc = (wproc_fptr)SetWindowLongPtr(windows.nic_conf, GWLP_WNDPROC, (LONG)&conf_group_wproc);
	SetWindowLongPtr(windows.global_conf, GWLP_WNDPROC, (LONG)&conf_group_wproc);
	SetWindowLongPtr(windows.button_box, GWLP_WNDPROC, (LONG)&conf_group_wproc);
}

static void update_nic_conf() {
	int selected_nic = ListView_GetNextItem(windows.nic_list, (LPARAM)-1, LVNI_FOCUSED);
	bool enabled = selected_nic >= 0 ? nics[selected_nic].enabled : false;
	
	EnableWindow(windows.nic_net, enabled ? TRUE : FALSE);
	EnableWindow(windows.nic_node, enabled ? TRUE : FALSE);
	EnableWindow(windows.nic_enabled, selected_nic >= 0 ? TRUE : FALSE);
	EnableWindow(windows.nic_primary, enabled ? TRUE : FALSE);
	
	if(selected_nic >= 0) {
		Button_SetCheck(windows.nic_enabled, nics[selected_nic].enabled ? BST_CHECKED : BST_UNCHECKED);
		Button_SetCheck(windows.nic_primary, nics[selected_nic].primary ? BST_CHECKED : BST_UNCHECKED);
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
	
	assert(hwnd);
	
	SendMessage(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
	
	return hwnd;
}

static void add_list_column(HWND hwnd, int id, char *text, int width) {
	LVCOLUMN lvc;
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
	lvc.fmt = LVCFMT_LEFT;
	lvc.cx = width;
	lvc.pszText = text;
	
	ListView_InsertColumn(hwnd, id, &lvc);
}

static int get_text_width(HWND hwnd, const char *txt) {
	HDC dc = GetDC(hwnd);
	assert(dc);
	
	SIZE size;
	assert(GetTextExtentPoint32(dc, txt, strlen(txt), &size));
	
	ReleaseDC(hwnd, dc);
	
	return size.cx;
}

static int get_text_height(HWND hwnd) {
	HDC dc = GetDC(hwnd);
	assert(dc);
	
	TEXTMETRIC tm;
	assert(GetTextMetrics(dc, &tm));
	
	ReleaseDC(hwnd, dc);
	
	return tm.tmHeight;
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
