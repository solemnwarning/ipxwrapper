/* IPXWrapper - Configuration tool
 * Copyright (C) 2011-2014 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include "interface.h"
#include "addr.h"

const char *IPXCONFIG_WINDOW_CLASS = "ipxconfig_class";

enum {
	ID_NIC_LIST    = 11,
	ID_NIC_ENABLED = 12,
	ID_NIC_NET     = 13,
	ID_NIC_NODE    = 14,
	
	ID_OPT_PORT      = 21,
	ID_OPT_W95       = 22,
	ID_OPT_LOG_DEBUG = 25,
	ID_OPT_LOG_TRACE = 26,
	ID_OPT_FW_EXCEPT = 27,
	ID_OPT_USE_PCAP  = 28,
	
	ID_OK     = 31,
	ID_CANCEL = 32,
	ID_APPLY  = 33,
};

struct iface {
	/* C style string so it can be passed directly to the listview */
	char name[MAX_ADAPTER_DESCRIPTION_LENGTH + 4];
	
	addr48_t hwaddr;
	
	iface_config_t config;
};

static void reload_nics();
static void reload_primary_nics();
static bool stash_nic_config();

static bool save_config();
static void main_window_init();
static void main_window_update();
static void main_window_resize(int width, int height);

static HWND create_child(HWND parent, LPCTSTR class_name, LPCTSTR title, DWORD style = 0, DWORD ex_style = 0, unsigned int id = 0);
static HWND create_GroupBox(HWND parent, LPCTSTR title);
static HWND create_STATIC(HWND parent, LPCTSTR text);
static HWND create_checkbox(HWND parent, LPCTSTR label, int id);
static bool get_checkbox(HWND checkbox);
static void set_checkbox(HWND checkbox, bool state);
static int get_text_width(HWND hwnd, const char *txt);
static int get_text_height(HWND hwnd);

static std::string w32_errmsg(DWORD errnum);
static void die(std::string msg);
static bool _pcap_installed();

static const bool PCAP_INSTALLED = _pcap_installed();

static std::vector<iface> nics;

static main_config_t main_config = get_main_config();
static addr48_t primary_iface    = get_primary_iface();

static std::string inv_error;
static HWND inv_window = NULL;

typedef LRESULT CALLBACK (*wproc_fptr)(HWND,UINT,WPARAM,LPARAM);
static wproc_fptr default_groupbox_wproc = NULL;

static struct {
	HWND main;
	
	HWND box_primary;
	HWND primary;
	
	HWND box_nic;
	HWND nic_list;
	HWND nic_enabled;
	HWND nic_net_lbl;
	HWND nic_net;
	HWND nic_node_lbl;
	HWND nic_node;
	
	HWND box_options;
	
	HWND opt_port_lbl;
	HWND opt_port;
	
	HWND opt_w95;
	HWND opt_log_debug;
	HWND opt_log_trace;
	HWND opt_fw_except;
	HWND opt_use_pcap;
	
	HWND ok_btn;
	HWND can_btn;
	HWND app_btn;
} wh;

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
						main_window_update();
					}
				}else if(nhdr->code == LVN_ITEMCHANGING) {
					NMLISTVIEW *lv = (NMLISTVIEW*)lp;
					
					if((lv->uOldState & LVIS_FOCUSED && !(lv->uNewState & LVIS_FOCUSED)) || (lv->uOldState & LVIS_SELECTED && !(lv->uNewState & LVIS_SELECTED))) {
						if(!stash_nic_config()) {
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
						int nic = ListView_GetNextItem(wh.nic_list, (LPARAM)(-1), LVNI_FOCUSED);
						nics[nic].config.enabled = get_checkbox(wh.nic_enabled);
						
						reload_primary_nics();
						main_window_update();
						
						break;
					}
					
					case ID_OPT_LOG_DEBUG: {
						main_window_update();
						break;
					}
					
					case ID_OPT_USE_PCAP: {
						main_config.use_pcap = get_checkbox(wh.opt_use_pcap);
						
						reload_nics();
						
						break;
					}
					
					case ID_OK: {
						if(save_config()) {
							PostMessage(wh.main, WM_CLOSE, 0, 0);
						}
						
						break;
					}
					
					case ID_CANCEL: {
						PostMessage(wh.main, WM_CLOSE, 0, 0);
						break;
					}
					
					case ID_APPLY: {
						save_config();
						break;
					}
					
					default:
						break;
				}
			}
			
			break;
		}
		
		case WM_SIZE: {
			main_window_resize(LOWORD(lp), HIWORD(lp));
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
			return main_wproc(wh.main, msg, wp, lp);
		
		default:
			return default_groupbox_wproc(hwnd, msg, wp, lp);
	}
}

int main()
{
	INITCOMMONCONTROLSEX common_controls;
	common_controls.dwSize = sizeof(common_controls);
	common_controls.dwICC  = ICC_LISTVIEW_CLASSES;
	
	if(!InitCommonControlsEx(&common_controls))
	{
		die("Could not initialise common controls");
	}
	
	WNDCLASSEX wclass;
	wclass.cbSize        = sizeof(wclass);
	wclass.style         = CS_HREDRAW | CS_VREDRAW;
	wclass.lpfnWndProc   = &main_wproc;
	wclass.cbClsExtra    = 0;
	wclass.cbWndExtra    = 0;
	wclass.hInstance     = GetModuleHandle(NULL);
	wclass.hIcon         = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(50), IMAGE_ICON, 32, 32, LR_SHARED);
	wclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wclass.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
	wclass.lpszMenuName  = NULL;
	wclass.lpszClassName = IPXCONFIG_WINDOW_CLASS;
	wclass.hIconSm       = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(50), IMAGE_ICON, 16, 16, LR_SHARED);
	
	if(!RegisterClassEx(&wclass))
	{
		die("Could not register window class: " + w32_errmsg(GetLastError()));
	}
	
	if(!PCAP_INSTALLED && main_config.use_pcap)
	{
		MessageBox(NULL, "IPXWrapper is currently configured to use WinPcap, but it doesn't seem to be installed.\n"
			"Configuration has been reset to use IP encapsulation.", "WinPcap not found", MB_OK | MB_TASKMODAL | MB_ICONWARNING);
		main_config.use_pcap = false;
	}
	
	main_window_init();
	
	MSG msg;
	BOOL mret;
	
	while((mret = GetMessage(&msg, NULL, 0, 0))) {
		if(mret == -1) {
			die("GetMessage failed: " + w32_errmsg(GetLastError()));
		}
		
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		
		if(inv_window && !PeekMessage(&msg, NULL, 0, 0, 0)) {
			MessageBox(wh.main, inv_error.c_str(), "Error", MB_OK);
			
			SetFocus(inv_window);
			Edit_SetSel(inv_window, 0, Edit_GetTextLength(inv_window));
			
			inv_window = NULL;
		}
	}
	
	return msg.wParam;
}

static void _add_nic(addr48_t hwaddr, const char *name)
{
	iface iface;
	
	strcpy(iface.name, name);
	
	iface.hwaddr = hwaddr;
	iface.config = get_iface_config(hwaddr);
	
	nics.push_back(iface);
}

/* Repopulate the list of network interfaces.
 * 
 * This is called during main window initialisation and when switching between
 * IP/Ethernet encapsulation.
*/
static void reload_nics()
{
	nics.clear();
	
	if(main_config.use_pcap)
	{
		/* Ethernet encapsulation is selected, so we base the NIC list
		 * on the list of usable WinPcap interfaces.
		*/
		
		ipx_pcap_interface_t *pcap_interfaces = ipx_get_pcap_interfaces(), *i;
		
		DL_FOREACH(pcap_interfaces, i)
		{
			_add_nic(i->mac_addr, i->desc);
		}
		
		ipx_free_pcap_interfaces(&pcap_interfaces);
	}
	else{
		/* IP encapsulation is selected, so we base the NIC list on the
		 * list of IP interfaces.
		*/
		
		_add_nic(WILDCARD_IFACE_HWADDR, "Wildcard interface");
		
		IP_ADAPTER_INFO *ifroot = load_sys_interfaces(), *ifptr;
		
		for(ifptr = ifroot; ifptr; ifptr = ifptr->Next)
		{
			if(ifptr->AddressLength != 6)
			{
				continue;
			}
			
			_add_nic(addr48_in(ifptr->Address), ifptr->Description);
		}
		
		free(ifroot);
	}
	
	ListView_DeleteAllItems(wh.nic_list);
	
	LVITEM lvi;
	lvi.mask      = LVIF_TEXT | LVIF_STATE;
	lvi.iItem     = 0;
	lvi.iSubItem  = 0;
	lvi.state     = 0;
	lvi.stateMask = 0;
	
	for(auto i = nics.begin(); i != nics.end(); ++i)
	{
		lvi.pszText = i->name;
		
		ListView_InsertItem(wh.nic_list, &lvi);
		++(lvi.iItem);
	}
	
	ListView_SetColumnWidth(wh.nic_list, 0, LVSCW_AUTOSIZE);
	
	reload_primary_nics();
	main_window_update();
}

/* Repopulate the list of interfaces available to be used as the primary.
 * 
 * This is called after reloading the list of NICs or when toggling the enabled
 * state of an interface.
*/
static void reload_primary_nics()
{
	ComboBox_ResetContent(wh.primary);
	
	ComboBox_AddString(wh.primary, "Default");
	ComboBox_SetCurSel(wh.primary, 0);
	
	for(auto i = nics.begin(); i != nics.end(); i++)
	{
		if(i->config.enabled)
		{
			int index = ComboBox_AddString(wh.primary, i->name);
			
			if(i->hwaddr == primary_iface)
			{
				ComboBox_SetCurSel(wh.primary, index);
			}
		}
	}
}

/* Fetch NIC settings from UI and stash them in the NIC list.
 * 
 * Returns false if an address or other text value could not be parsed, true
 * otherwise.
*/
static bool stash_nic_config()
{
	int selected_nic = ListView_GetNextItem(wh.nic_list, (LPARAM)-1, LVNI_FOCUSED);
	
	if(selected_nic == -1) {
		/* Return success if no NIC is selected */
		return true;
	}
	
	char net[32], node[32];
	
	GetWindowText(wh.nic_net, net, 32);
	GetWindowText(wh.nic_node, node, 32);
	
	if(!addr32_from_string(&(nics[selected_nic].config.netnum), net))
	{
		inv_error  = "Network number is invalid.\n"
			"Valid network numbers are in the format XX:XX:XX:XX";
		inv_window = wh.nic_net;
		
		return false;
	}
	
	if(!main_config.use_pcap)
	{
		if(!addr48_from_string(&(nics[selected_nic].config.nodenum), node))
		{
			inv_error  = "Node number is invalid.\n"
				"Valid numbers are in the format XX:XX:XX:XX:XX:XX";
			inv_window = wh.nic_node;
			
			return false;
		}
	}
	
	return true;
}

static bool save_config()
{
	if(!stash_nic_config())
	{
		return false;
	}
	
	int pri_index = ComboBox_GetCurSel(wh.primary);
	
	if(pri_index == 0)
	{
		primary_iface = addr48_in((unsigned char[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
	}
	else{
		/* Iterate over the NIC list to find the selected primary
		 * interface.
		 * 
		 * Can't just deref by index here as disabled interfaces don't
		 * increment the index.
		*/
		
		int this_nic = 0;
		
		for(auto i = nics.begin(); i != nics.end(); i++)
		{
			if(i->config.enabled && ++this_nic == pri_index)
			{
				primary_iface = i->hwaddr;
				break;
			}
		}
	}
	
	if(!main_config.use_pcap)
	{
		char port_s[32], *endptr;
		
		GetWindowText(wh.opt_port, port_s, 32);
		int port = strtol(port_s, &endptr, 10);
		
		if(port < 1 || port > 65535 || *endptr) {
			MessageBox(wh.main, "Invalid port number.\n"
				"Port number must be an integer in the range 1 - 65535", "Error", MB_OK);
			
			SetFocus(wh.opt_port);
			Edit_SetSel(wh.opt_port, 0, Edit_GetTextLength(wh.opt_port));
			
			return false;
		}
		
		main_config.udp_port = port;
	}
	
	main_config.w95_bug   = get_checkbox(wh.opt_w95);
	main_config.fw_except = get_checkbox(wh.opt_fw_except);
	main_config.log_level = LOG_INFO;
	
	if(get_checkbox(wh.opt_log_debug))
	{
		main_config.log_level = LOG_DEBUG;
		
		if(get_checkbox(wh.opt_log_trace))
		{
			main_config.log_level = LOG_CALL;
		}
	}
	
	for(auto i = nics.begin(); i != nics.end(); i++)
	{
		if(!set_iface_config(i->hwaddr, &(i->config)))
		{
			return false;
		}
	}
	
	return set_main_config(&main_config) && set_primary_iface(primary_iface);
}

/* Create the main window and all the child windows, populate them with the
 * current configuration and then display them.
*/
static void main_window_init()
{
	wh.main = CreateWindow(
		IPXCONFIG_WINDOW_CLASS,
		"IPXWrapper configuration",
		WS_TILEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		370, 445,
		NULL,
		NULL,
		NULL,
		NULL
	);
	
	if(!wh.main)
	{
		die("Could not create main window: " + w32_errmsg(GetLastError()));
	}
	
	/* +- Primary interface -------------------------------------+
	 * | | Default                                           |▼| |
	 * +---------------------------------------------------------+
	*/
	
	wh.box_primary = create_GroupBox(wh.main, "Primary interface");
	
	wh.primary = create_child(wh.box_primary, WC_COMBOBOX, NULL, CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0);
	
	/* +- Network adapters --------------------------------------+
	 * | +-----------------------------------------------------+ |
	 * | | ...                                                 | |
	 * | | ...                                                 | |
	 * | | ...                                                 | |
	 * | +-----------------------------------------------------+ |
	 * | □ Enable interface                                      |
	 * |       Network number | AA:BB:CC:DD       |              |
	 * |          Node number | AA:BB:CC:DD:EE:FF |              |
	 * +---------------------------------------------------------+
	*/
	
	{
		wh.box_nic = create_GroupBox(wh.main, "Network adapters");
		
		wh.nic_list = create_child(wh.box_nic, WC_LISTVIEW, NULL, LVS_SINGLESEL | LVS_REPORT | WS_TABSTOP | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, ID_NIC_LIST);
		
		LVCOLUMN lvc;
		lvc.mask = LVCF_FMT;
		lvc.fmt  = LVCFMT_LEFT;
		
		ListView_InsertColumn(wh.nic_list, 0, &lvc);
		
		wh.nic_enabled = create_checkbox(wh.box_nic, "Enable interface", ID_NIC_ENABLED);
		
		wh.nic_net_lbl = create_STATIC(wh.box_nic, "Network number");
		wh.nic_net     = create_child(wh.box_nic, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_NIC_NET);
		
		wh.nic_node_lbl = create_STATIC(wh.box_nic, "Node number");
		wh.nic_node     = create_child(wh.box_nic, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_NIC_NODE);
	}
	
	/* +- Options -----------------------------------------------+
	 * |       Broadcast port | 54792             |              |
	 * | □ Enable Windows 95 SO_BROADCAST bug                    |
	 * | □ Log debugging messages                                |
	 * | □ Log WinSock API calls                                 |
	 * | □ Automatically create Windows Firewall exceptions      |
	 * | □ Send and receive real IPX packets                     |
	 * +---------------------------------------------------------+
	*/
	
	{
		wh.box_options = create_GroupBox(wh.main, "Options");
		
		wh.opt_port_lbl = create_STATIC(wh.box_options, "Broadcast port");
		wh.opt_port     = create_child(wh.box_options, "EDIT",   "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_OPT_PORT);
		
		char port_s[8];
		
		sprintf(port_s, "%hu", main_config.udp_port);
		SetWindowText(wh.opt_port, port_s);
		
		wh.opt_w95       = create_checkbox(wh.box_options, "Enable Windows 95 SO_BROADCAST bug", ID_OPT_W95);
		wh.opt_log_debug = create_checkbox(wh.box_options, "Log debugging messages", ID_OPT_LOG_DEBUG);
		wh.opt_log_trace = create_checkbox(wh.box_options, "Log WinSock API calls", ID_OPT_LOG_TRACE);
		wh.opt_fw_except = create_checkbox(wh.box_options, "Automatically create Windows Firewall exceptions", ID_OPT_FW_EXCEPT);
		wh.opt_use_pcap  = create_checkbox(wh.box_options, "Send and receive real IPX packets", ID_OPT_USE_PCAP);
		
		set_checkbox(wh.opt_w95,       main_config.w95_bug);
		set_checkbox(wh.opt_log_debug, main_config.log_level <= LOG_DEBUG);
		set_checkbox(wh.opt_log_trace, main_config.log_level <= LOG_CALL);
		set_checkbox(wh.opt_fw_except, main_config.fw_except);
		set_checkbox(wh.opt_use_pcap,  main_config.use_pcap);
		
		EnableWindow(wh.opt_use_pcap,  PCAP_INSTALLED);
	}
	
	wh.ok_btn  = create_child(wh.main, "BUTTON", "OK",     BS_PUSHBUTTON | WS_TABSTOP, 0, ID_OK);
	wh.can_btn = create_child(wh.main, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_CANCEL);
	wh.app_btn = create_child(wh.main, "BUTTON", "Apply",  BS_PUSHBUTTON | WS_TABSTOP, 0, ID_APPLY);
	
	reload_nics();
	
	ShowWindow(wh.main, SW_SHOW);
	UpdateWindow(wh.main);
}

/* Reset the NIC network/node numbers and toggle the enabled state of controls
 * on the UI. Called when the selected NIC changes and when some options are
 * toggled.
*/
static void main_window_update()
{
	int selected_nic = ListView_GetNextItem(wh.nic_list, (LPARAM)(-1), LVNI_FOCUSED);
	
	bool nic_enabled = selected_nic >= 0
		? nics[selected_nic].config.enabled
		: false;
	
	EnableWindow(wh.nic_enabled, selected_nic >= 0);
	EnableWindow(wh.nic_net,     nic_enabled);
	EnableWindow(wh.nic_node,    nic_enabled && !main_config.use_pcap);
	
	if(selected_nic >= 0)
	{
		set_checkbox(wh.nic_enabled, nic_enabled);
		
		char net_s[ADDR32_STRING_SIZE];
		addr32_string(net_s, nics[selected_nic].config.netnum);
		
		char node_s[ADDR48_STRING_SIZE];
		addr48_string(node_s, (main_config.use_pcap
			? nics[selected_nic].hwaddr
			: nics[selected_nic].config.nodenum));
		
		SetWindowText(wh.nic_net, net_s);
		SetWindowText(wh.nic_node, node_s);
	}
	
	EnableWindow(wh.opt_port,      !main_config.use_pcap);
	EnableWindow(wh.opt_log_trace, get_checkbox(wh.opt_log_debug));
}

/* Update the size and position of everything within the window. */
static void main_window_resize(int width, int height)
{
	int edge   = GetSystemMetrics(SM_CYEDGE);
	int text_h = get_text_height(wh.box_primary);
	int edit_h = text_h + 2 * edge;
	int lbl_w  = get_text_width(wh.nic_net_lbl, "Network number");
	
	/* Primary interface
	 * Height is constant. Vertical position is at the very top.
	*/
	
	int pri_h = edit_h + text_h + 10;
	
	MoveWindow(wh.box_primary, 0,  0,      width,      pri_h,  TRUE);
	MoveWindow(wh.primary,     10, text_h, width - 20, height, TRUE);
	
	/* Buttons
	 * Height is constant. Vertical position is at the very bottom.
	 * 
	 * TODO: Dynamic button sizing.
	*/
	
	int btn_w = 75;
	int btn_h = 23;
	
	MoveWindow(wh.app_btn, width - btn_w - 6,      height - btn_h - 6, btn_w, btn_h, TRUE);
	MoveWindow(wh.can_btn, width - btn_w * 2 - 12, height - btn_h - 6, btn_w, btn_h, TRUE);
	MoveWindow(wh.ok_btn,  width - btn_w * 3 - 18, height - btn_h - 6, btn_w, btn_h, TRUE);
	
	/* Options
	 * Height is constant. Vertical position is above buttons.
	*/
	
	int box_options_y = text_h;
	
	int port_w = get_text_width(wh.nic_node, "000000");
	
	MoveWindow(wh.opt_port_lbl, 10,         box_options_y + edge, lbl_w,  text_h, TRUE);
	MoveWindow(wh.opt_port,     15 + lbl_w, box_options_y,        port_w, edit_h, TRUE);
	box_options_y += edit_h + 4;
	
	MoveWindow(wh.opt_w95,       10, box_options_y, width - 20, text_h, TRUE);
	box_options_y += text_h + 2;
	
	MoveWindow(wh.opt_log_debug, 10, box_options_y, width - 20, text_h, TRUE);
	box_options_y += text_h + 2;
	
	MoveWindow(wh.opt_log_trace, 10, box_options_y, width - 20, text_h, TRUE);
	box_options_y += text_h + 2;
	
	MoveWindow(wh.opt_fw_except, 10, box_options_y, width - 20, text_h, TRUE);
	box_options_y += text_h + 2;
	
	MoveWindow(wh.opt_use_pcap,  10, box_options_y, width - 20, text_h, TRUE);
	box_options_y += text_h + 2;
	
	int box_options_h = box_options_y + 2;
	
	MoveWindow(wh.box_options, 0, height - box_options_y - btn_h - 12, width, box_options_h, TRUE);
	
	/* NICs
	 * Fills available space between primary interface and options.
	*/
	
	int node_w    = get_text_width(wh.nic_node, "00:00:00:00:00:00");
	int box_nic_h = height - pri_h - box_options_h - btn_h - 12;
	
	MoveWindow(wh.box_nic, 0, pri_h, width, box_nic_h, TRUE);
	
	int box_nic_y = box_nic_h - 4;
	
	box_nic_y -= edit_h + 2;
	MoveWindow(wh.nic_node_lbl, 10,         box_nic_y + edge, lbl_w,  text_h, TRUE);
	MoveWindow(wh.nic_node,     15 + lbl_w, box_nic_y,        node_w, edit_h, TRUE);
	
	box_nic_y -= edit_h + 2;
	MoveWindow(wh.nic_net_lbl, 10,         box_nic_y + edge, lbl_w,  text_h, TRUE);
	MoveWindow(wh.nic_net,     15 + lbl_w, box_nic_y,        node_w, edit_h, TRUE);
	
	box_nic_y -= edit_h + 2;
	MoveWindow(wh.nic_enabled, 10, box_nic_y, width - 20, text_h, TRUE);
	
	box_nic_y -= 6;
	MoveWindow(wh.nic_list, 10, text_h, width - 20, box_nic_y - text_h, TRUE);
	
	
	UpdateWindow(wh.main);
}

static HWND create_child(HWND parent, LPCTSTR class_name, LPCTSTR title, DWORD style, DWORD ex_style, unsigned int id) {
	static unsigned int idnum = 100;
	
	HWND hwnd = CreateWindowEx(
		ex_style,
		class_name,
		title,
		WS_CHILD | WS_VISIBLE | style,
		0, 0, 0, 0,
		parent,
		(HMENU)(id ? id : idnum++),
		NULL,
		NULL
	);
	
	if(!hwnd)
	{
		die(std::string("Could not create ") + class_name + " window: " + w32_errmsg(GetLastError()));
	}
	
	SendMessage(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
	
	return hwnd;
}

static HWND create_GroupBox(HWND parent, LPCTSTR title)
{
	HWND groupbox = create_child(parent, "BUTTON", title, BS_GROUPBOX);
	default_groupbox_wproc = (wproc_fptr)SetWindowLongPtr(groupbox, GWLP_WNDPROC, (LONG_PTR)&groupbox_wproc);
	
	return groupbox;
}

static HWND create_STATIC(HWND parent, LPCTSTR text)
{
	return create_child(parent, "STATIC", text, SS_RIGHT);
}

static HWND create_checkbox(HWND parent, LPCTSTR label, int id)
{
	return create_child(parent, "BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP, 0, id);
}

static bool get_checkbox(HWND checkbox)
{
	return Button_GetCheck(checkbox) == BST_CHECKED;
}

static void set_checkbox(HWND checkbox, bool state)
{
	Button_SetCheck(checkbox, (state ? BST_CHECKED : BST_UNCHECKED));
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

/* Check if WinPcap is installed.
 * Returns true if wpcap.dll can be loaded.
*/
static bool _pcap_installed()
{
	HMODULE wpcap = LoadLibrary("wpcap.dll");
	if(wpcap)
	{
		FreeLibrary(wpcap);
		return true;
	}
	
	return false;
}

/* Used to display errors from shared functions. */
extern "C" void log_printf(enum ipx_log_level level, const char *fmt, ...)
{
	int icon = 0;
	const char *title = NULL;
	
	if(level >= LOG_ERROR)
	{
		icon  = MB_ICONERROR;
		title = "Error";
	}
	else if(level >= LOG_WARNING)
	{
		icon  = MB_ICONWARNING;
		title = "Warning";
	}
	else{
		return;
	}
	
	va_list argv;
	char msg[1024];
	
	va_start(argv, fmt);
	vsnprintf(msg, 1024, fmt, argv);
	va_end(argv);
	
	MessageBox(NULL, msg, title, MB_OK | MB_TASKMODAL | icon);
}
