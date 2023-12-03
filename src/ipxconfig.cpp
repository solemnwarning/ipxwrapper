/* IPXWrapper - Configuration tool
 * Copyright (C) 2011-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <set>

#include "config.h"
#include "interface2.h"
#include "addr.h"

const char *IPXCONFIG_WINDOW_CLASS = "ipxconfig_class";

enum {
	ID_NIC_LIST    = 11,
	ID_NIC_ENABLED = 12,
	ID_NIC_NET     = 13,
	ID_NIC_NODE    = 14,
	
	ID_OPT_W95       = 22,
	ID_OPT_LOG_DEBUG = 25,
	ID_OPT_LOG_TRACE = 26,
	ID_OPT_PROFILE   = 27,
	
	ID_OK     = 31,
	ID_CANCEL = 32,
	ID_APPLY  = 33,
	
	ID_ENCAP_IPXWRAPPER = 41,
	ID_ENCAP_DOSBOX = 42,
	ID_ENCAP_IPX = 44,
	
	ID_DOSBOX_SERVER_ADDR = 51,
	ID_DOSBOX_SERVER_PORT = 52,
	ID_DOSBOX_COALESCE = 53,
	ID_DOSBOX_FW_EXCEPT = 55,
	
	ID_IPXWRAPPER_PORT = 61,
	ID_IPXWRAPPER_FW_EXCEPT = 62,
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
static void main_window_layout(HWND *visible_groups);

static HWND create_child(HWND parent, LPCTSTR class_name, LPCTSTR title, DWORD style = 0, DWORD ex_style = 0, unsigned int id = 0);
static HWND create_GroupBox(HWND parent, LPCTSTR title);
static HWND create_STATIC(HWND parent, LPCTSTR text);
static HWND create_checkbox(HWND parent, LPCTSTR label, int id);
static bool get_checkbox(HWND checkbox);
static void set_checkbox(HWND checkbox, bool state);
static HWND create_radio(HWND parent, LPCTSTR label, int id);
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
	
	HWND box_encap;
	HWND encap_ipxwrapper;
	HWND encap_dosbox;
	HWND encap_ipx;
	
	HWND box_primary;
	HWND primary;
	
	HWND box_nic;
	HWND nic_list;
	HWND nic_enabled;
	HWND nic_net_lbl;
	HWND nic_net;
	HWND nic_node_lbl;
	HWND nic_node;
	
	HWND box_ipxwrapper_options;
	HWND ipxwrapper_port_lbl;
	HWND ipxwrapper_port;
	HWND ipxwrapper_fw_except;
	
	HWND box_dosbox_options;
	HWND dosbox_server_addr_lbl;
	HWND dosbox_server_addr;
	HWND dosbox_server_port_lbl;
	HWND dosbox_server_port;
	HWND dosbox_coalesce;
	HWND dosbox_fw_except;
	
	HWND box_ipx_options;
	HWND ipx_frame_type_lbl;
	HWND ipx_frame_type;
	
	HWND box_options;
	HWND opt_w95;
	HWND opt_log_debug;
	HWND opt_log_trace;
	HWND opt_profile;
	
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
					case ID_ENCAP_IPXWRAPPER:
					case ID_ENCAP_DOSBOX:
					case ID_ENCAP_IPX:
						main_window_update();
						reload_nics();
						break;
					
					case ID_NIC_ENABLED: {
						int nic = ListView_GetNextItem(wh.nic_list, (LPARAM)(-1), LVNI_FOCUSED);
						nics[nic].config.enabled = get_checkbox(wh.nic_enabled);
						
						reload_primary_nics();
						main_window_update();
						
						break;
					}
					
					case ID_DOSBOX_COALESCE: {
						bool coalesce = get_checkbox(wh.dosbox_coalesce);
						
						if(coalesce)
						{
							int result = MessageBox(NULL, "Packet coalescing requires all players to be using IPXWrapper 0.7.1 or later.\nAre you sure you want to enable it?", "Warning", MB_YESNO | MB_TASKMODAL | MB_ICONWARNING);
							if(result != IDYES)
							{
								set_checkbox(wh.dosbox_coalesce, false);
							}
						}
						
						break;
					}
					
					case ID_OPT_LOG_DEBUG: {
						main_window_update();
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
	fprof_init(stub_fstats, NUM_STUBS);
	
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
	
	if(!PCAP_INSTALLED && main_config.encap_type == ENCAP_TYPE_PCAP)
	{
		MessageBox(NULL, "IPXWrapper is currently configured to use WinPcap, but it doesn't seem to be installed.\n"
			"Configuration has been reset to use IP encapsulation.", "WinPcap not found", MB_OK | MB_TASKMODAL | MB_ICONWARNING);
		
		main_config.encap_type = ENCAP_TYPE_IPXWRAPPER;
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
	
	fprof_cleanup(stub_fstats, NUM_STUBS);
	
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
	
	if(main_config.encap_type == ENCAP_TYPE_PCAP)
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
	else if(main_config.encap_type == ENCAP_TYPE_IPXWRAPPER)
	{
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
	
	if(main_config.encap_type != ENCAP_TYPE_PCAP)
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
	if(main_config.encap_type == ENCAP_TYPE_IPXWRAPPER || main_config.encap_type == ENCAP_TYPE_PCAP)
	{
		int pri_index = ComboBox_GetCurSel(wh.primary);
		
		if(pri_index == 0)
		{
			const unsigned char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
			primary_iface = addr48_in(f6);
		}
		else{
			/* Iterate over the NIC list to find the selected primary interface.
			 * Can't just deref by index here as disabled interfaces don't increment
			 * the index.
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
		
		if(!stash_nic_config())
		{
			return false;
		}
	}
	
	if(main_config.encap_type == ENCAP_TYPE_IPXWRAPPER)
	{
		char port_s[32], *endptr;
		
		GetWindowText(wh.ipxwrapper_port, port_s, 32);
		int port = strtol(port_s, &endptr, 10);
		
		if(port < 1 || port > 65535 || *endptr) {
			MessageBox(wh.main, "Invalid port number.\n"
			                    "Port number must be an integer in the range 1 - 65535", "Error", MB_OK);
			
			SetFocus(wh.ipxwrapper_port);
			Edit_SetSel(wh.ipxwrapper_port, 0, Edit_GetTextLength(wh.ipxwrapper_port));
			
			return false;
		}
		
		main_config.udp_port = port;
		
		main_config.fw_except = get_checkbox(wh.ipxwrapper_fw_except);
	}
	else if(main_config.encap_type == ENCAP_TYPE_DOSBOX)
	{
		char server[256];
		server[ GetWindowText(wh.dosbox_server_addr, server, 256) ] = '\0';
		
		if(server[0] == '\0')
		{
			MessageBox(wh.main, "DOSBox IPX server address is required.", "Error", MB_OK);
			SetFocus(wh.dosbox_server_addr);
			
			return false;
		}
		
		char port_s[32], *endptr;
		
		GetWindowText(wh.dosbox_server_port, port_s, 32);
		int port = strtol(port_s, &endptr, 10);
		
		if(port < 1 || port > 65535 || *endptr) {
			MessageBox(wh.main, "Invalid port number.\n"
			                    "Port number must be an integer in the range 1 - 65535", "Error", MB_OK);
			
			SetFocus(wh.dosbox_server_port);
			Edit_SetSel(wh.dosbox_server_port, 0, Edit_GetTextLength(wh.dosbox_server_port));
			
			return false;
		}
		
		free(main_config.dosbox_server_addr);
		
		main_config.dosbox_server_addr = strdup(server);
		if(main_config.dosbox_server_addr == NULL)
		{
			MessageBox(wh.main, "Memory allocation failure.", "Error", MB_OK);
			abort();
		}
		
		main_config.dosbox_server_port = port;
		main_config.dosbox_coalesce = get_checkbox(wh.dosbox_coalesce);
		main_config.fw_except = get_checkbox(wh.dosbox_fw_except);
	}
	else if(main_config.encap_type == ENCAP_TYPE_PCAP)
	{
		main_config.frame_type = (main_config_frame_type)(ComboBox_GetCurSel(wh.ipx_frame_type) + 1);
	}
	
	main_config.w95_bug   = get_checkbox(wh.opt_w95);
	main_config.log_level = LOG_INFO;
	
	if(get_checkbox(wh.opt_log_debug))
	{
		main_config.log_level = LOG_DEBUG;
		
		if(get_checkbox(wh.opt_log_trace))
		{
			main_config.log_level = LOG_CALL;
		}
	}
	
	main_config.profile = get_checkbox(wh.opt_profile);
	
	if(main_config.encap_type == ENCAP_TYPE_IPXWRAPPER || main_config.encap_type == ENCAP_TYPE_PCAP)
	{
		for(auto i = nics.begin(); i != nics.end(); i++)
		{
			if(!set_iface_config(i->hwaddr, &(i->config)))
			{
				return false;
			}
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
		(WS_TILEDWINDOW & ~WS_SIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT,
		400, 445,
		NULL,
		NULL,
		NULL,
		NULL
	);
	
	if(!wh.main)
	{
		die("Could not create main window: " + w32_errmsg(GetLastError()));
	}
	
	/* +- Encapsulation type-------------------------------------+
	 * | ( ) IPXWrapper UDP encapsulation                        |
	 * |     ...                                                 |
	 * |                                                         |
	 * | ( ) DOSBox UDP encapsulation                            |
	 * |     ...                                                 |
	 * |                                                         |
	 * | ( ) Real IPX encapsulation (requires WinPcap)           |
	 * |     ...                                                 |
	 * +---------------------------------------------------------+
	*/
	
	{
		/* Create controls. */
		
		wh.box_encap = create_GroupBox(wh.main, "Encapsulation type");
		
		wh.encap_ipxwrapper = create_radio(wh.box_encap, "IPXWrapper UDP encapsulation\nConnect to other computers using IPXWrapper on your local network.", ID_ENCAP_IPXWRAPPER);
		wh.encap_dosbox     = create_radio(wh.box_encap, "DOSBox UDP encapsulation\nConnect to other computers via a DOSBox IPX server.", ID_ENCAP_DOSBOX);
		wh.encap_ipx        = create_radio(wh.box_encap, "Real IPX encapsulation (requires WinPcap)\nConnect to devices using the real IPX protocol on your local network.", ID_ENCAP_IPX);
		
		/* Initialise controls. */
		
		switch(main_config.encap_type)
		{
			case ENCAP_TYPE_IPXWRAPPER:
				set_checkbox(wh.encap_ipxwrapper, true);
				break;
				
			case ENCAP_TYPE_DOSBOX:
				set_checkbox(wh.encap_dosbox, true);
				break;
				
			case ENCAP_TYPE_PCAP:
				set_checkbox(wh.encap_ipx, true);
				break;
		}
		
		EnableWindow(wh.encap_ipx, PCAP_INSTALLED);
	}
	
	/* +- Primary interface -------------------------------------+
	 * | | Default                                           |▼| |
	 * +---------------------------------------------------------+
	*/
	
	{
		wh.box_primary = create_GroupBox(wh.main, "Primary interface");
		
		wh.primary = create_child(wh.box_primary, WC_COMBOBOX, NULL, CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0);
	}
	
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
	
	/* +- Network options ---------------------------------------+
	 * |       Broadcast port | 54792             |              |
	 * |                                                         |
	 * | □ Automatically create Windows Firewall exceptions      |
	 * +---------------------------------------------------------+
	*/
	
	{
		/* Create controls. */
		
		wh.box_ipxwrapper_options = create_GroupBox(wh.main, "Network options");
		
		wh.ipxwrapper_port_lbl = create_STATIC(wh.box_ipxwrapper_options, "Broadcast port");
		wh.ipxwrapper_port     = create_child(wh.box_ipxwrapper_options, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_IPXWRAPPER_PORT);
		
		wh.ipxwrapper_fw_except = create_checkbox(wh.box_ipxwrapper_options, "Automatically create Windows Firewall exceptions", ID_IPXWRAPPER_FW_EXCEPT);
		
		/* Initialise controls. */
		
		char port_s[8];
		
		sprintf(port_s, "%hu", main_config.udp_port);
		SetWindowText(wh.ipxwrapper_port, port_s);
		
		set_checkbox(wh.ipxwrapper_fw_except, main_config.fw_except);
		
		// TODO: Layout controls
	}
	
	/* +- Network options ---------------------------------------+
	 * |   DOSBox IPX server address | foo.bar.com       |       |
	 * |      DOSBox IPX server port | 213               |       |
	 * |                                                         |
	 * | □ Automatically create Windows Firewall exceptions      |
	 * +---------------------------------------------------------+
	*/
	
	{
		/* Create controls. */
		
		wh.box_dosbox_options = create_GroupBox(wh.main, "Network options");
		
		wh.dosbox_server_addr_lbl = create_STATIC(wh.box_dosbox_options, "DOSBox IPX server address");
		wh.dosbox_server_addr     = create_child(wh.box_dosbox_options, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_DOSBOX_SERVER_ADDR);
		
		wh.dosbox_server_port_lbl = create_STATIC(wh.box_dosbox_options, "DOSBox IPX server port");
		wh.dosbox_server_port     = create_child(wh.box_dosbox_options, "EDIT", "", WS_TABSTOP, WS_EX_CLIENTEDGE, ID_DOSBOX_SERVER_PORT);
		
		wh.dosbox_coalesce  = create_checkbox(wh.box_dosbox_options, "Coalesce packets when saturated", ID_DOSBOX_COALESCE);
		wh.dosbox_fw_except = create_checkbox(wh.box_dosbox_options, "Automatically create Windows Firewall exceptions", ID_DOSBOX_FW_EXCEPT);
		
		/* Initialise controls. */
		
		SetWindowText(wh.dosbox_server_addr, main_config.dosbox_server_addr);
		
		char port_s[8];
		
		sprintf(port_s, "%hu", main_config.dosbox_server_port);
		SetWindowText(wh.dosbox_server_port, port_s);
		
		set_checkbox(wh.dosbox_coalesce, main_config.dosbox_coalesce);
		set_checkbox(wh.dosbox_fw_except, main_config.fw_except);
	}
	
	/* +- Network options ---------------------------------------+
	 * |         Frame type   | Ethernet II  |▼|                 |
	 * +---------------------------------------------------------+
	*/
	
	{
		/* Create controls. */
		
		wh.box_ipx_options = create_GroupBox(wh.main, "Network options");
		
		wh.ipx_frame_type_lbl = create_STATIC(wh.box_ipx_options, "Frame type");
		wh.ipx_frame_type     = create_child( wh.box_ipx_options, WC_COMBOBOX, NULL, CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0);
		
		/* Initialise controls. */
		
		ComboBox_AddString(wh.ipx_frame_type, "Ethernet II");
		ComboBox_AddString(wh.ipx_frame_type, "Novell \"raw\" 802.3");
		ComboBox_AddString(wh.ipx_frame_type, "IEEE 802.2 (LLC)");
		
		ComboBox_SetCurSel(wh.ipx_frame_type, main_config.frame_type - 1);
	}
	
	/* +- Other options --------------------------------------+
	 * | □ Enable Windows 95 SO_BROADCAST bug                    |
	 * | □ Log debugging messages                                |
	 * | □ Log WinSock API calls                                 |
	 * | □ Log profiling counters                                |
	 * +---------------------------------------------------------+
	*/
	
	{
		wh.box_options = create_GroupBox(wh.main, "Other options");
		
		wh.opt_w95       = create_checkbox(wh.box_options, "Enable Windows 95 SO_BROADCAST bug", ID_OPT_W95);
		wh.opt_log_debug = create_checkbox(wh.box_options, "Log debugging messages", ID_OPT_LOG_DEBUG);
		wh.opt_log_trace = create_checkbox(wh.box_options, "Log WinSock API calls", ID_OPT_LOG_TRACE);
		wh.opt_profile   = create_checkbox(wh.box_options, "Log profiling counters", ID_OPT_PROFILE);
		
		set_checkbox(wh.opt_w95,       main_config.w95_bug);
		set_checkbox(wh.opt_log_debug, main_config.log_level <= LOG_DEBUG);
		set_checkbox(wh.opt_log_trace, main_config.log_level <= LOG_CALL);
		set_checkbox(wh.opt_profile,   main_config.profile);
	}
	
	wh.ok_btn  = create_child(wh.main, "BUTTON", "OK",     BS_PUSHBUTTON | WS_TABSTOP, 0, ID_OK);
	wh.can_btn = create_child(wh.main, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_CANCEL);
	wh.app_btn = create_child(wh.main, "BUTTON", "Apply",  BS_PUSHBUTTON | WS_TABSTOP, 0, ID_APPLY);
	
	/*******************
	 * Layout controls *
	 ******************
	*/
	
	RECT window_client_area;
	GetClientRect(wh.main, &window_client_area);
	
	int width = window_client_area.right - window_client_area.left;
	
	const int edge   = GetSystemMetrics(SM_CYEDGE);
	const int text_h = get_text_height(wh.box_primary);
	const int edit_h = text_h + 2 * edge;
	const int lbl_w  = get_text_width(wh.nic_net_lbl, "Network number") + 2 * edge;
	const int lbl_w2 = get_text_width(wh.nic_net_lbl, "DOSBox IPX server address") + 2 * edge;
	
	const int port_w = get_text_width(wh.nic_node, "000000") * 1.5;
	const int node_w = get_text_width(wh.nic_node, "00:00:00:00:00:00") * 1.5;
	
	const int BOX_TOP_PAD     = text_h;
	const int BOX_BOTTOM_PAD  = 6;
	const int BOX_SIDE_PAD    = 10;
	const int BOX_SIDE_MARGIN = 10;
	
	const int BOX_WIDTH = width - (BOX_SIDE_MARGIN * 2);
	const int BOX_INNER_WIDTH = BOX_WIDTH - (BOX_SIDE_PAD * 2);
	
	/* Encapsulation type */
	
	{
		int box_encap_y = BOX_TOP_PAD;
		
		MoveWindow(wh.encap_ipxwrapper, BOX_SIDE_PAD, box_encap_y, BOX_INNER_WIDTH, text_h * 2, TRUE);
		box_encap_y += text_h * 3;
		
		MoveWindow(wh.encap_dosbox, BOX_SIDE_PAD, box_encap_y, BOX_INNER_WIDTH, text_h * 2, TRUE);
		box_encap_y += text_h * 3;
		
		MoveWindow(wh.encap_ipx, BOX_SIDE_PAD, box_encap_y, BOX_INNER_WIDTH, text_h * 2, TRUE);
		box_encap_y += text_h * 2;
		
		int box_encap_h = box_encap_y + BOX_BOTTOM_PAD;
		
		MoveWindow(wh.box_encap, BOX_SIDE_MARGIN, 0, BOX_WIDTH, box_encap_h, TRUE);
	}
	
	/* "Primary interface" */
	
	{
		MoveWindow(wh.primary, BOX_SIDE_PAD, BOX_TOP_PAD, BOX_INNER_WIDTH, 300, TRUE);
		
		int box_primary_h = BOX_TOP_PAD + edit_h + BOX_BOTTOM_PAD;
		MoveWindow(wh.box_primary, BOX_SIDE_MARGIN,  0, BOX_WIDTH, box_primary_h, TRUE);
	}
	
	/* "Network adapters" */
	
	{
		int box_nic_y = BOX_TOP_PAD;
		
		int nic_list_h = ((5 * text_h) + (2 * edge));
		
		MoveWindow(wh.nic_list, BOX_SIDE_PAD, box_nic_y, BOX_INNER_WIDTH, nic_list_h, TRUE);
		
		box_nic_y += nic_list_h + 2;
		
		MoveWindow(wh.nic_node_lbl, BOX_SIDE_PAD,             box_nic_y + edge, lbl_w,  text_h, TRUE);
		MoveWindow(wh.nic_node,     BOX_SIDE_PAD + 5 + lbl_w, box_nic_y,        node_w, edit_h, TRUE);
		
		box_nic_y += edit_h + 2;
		
		MoveWindow(wh.nic_net_lbl, BOX_SIDE_PAD,             box_nic_y + edge, lbl_w,  text_h, TRUE);
		MoveWindow(wh.nic_net,     BOX_SIDE_PAD + 5 + lbl_w, box_nic_y,        node_w, edit_h, TRUE);
		
		box_nic_y += edit_h + 2;
		
		MoveWindow(wh.nic_enabled, BOX_SIDE_PAD, box_nic_y, BOX_INNER_WIDTH, text_h, TRUE);
		
		box_nic_y += edit_h + 2;
		
		int box_nic_h = box_nic_y + BOX_BOTTOM_PAD;
		
		MoveWindow(wh.box_nic, BOX_SIDE_MARGIN, 0, BOX_WIDTH, box_nic_h, TRUE);
	}
	
	/* "Network options" (IPXWrapper encapsulation) */
	
	{
		int box_ipxwrapper_options_y = BOX_TOP_PAD;
		
		MoveWindow(wh.ipxwrapper_port_lbl, BOX_SIDE_PAD, box_ipxwrapper_options_y, lbl_w, text_h, TRUE);
		MoveWindow(wh.ipxwrapper_port, BOX_SIDE_PAD + 5 + lbl_w, box_ipxwrapper_options_y, port_w, edit_h, TRUE);
		box_ipxwrapper_options_y += edit_h;
		
		MoveWindow(wh.ipxwrapper_fw_except, BOX_SIDE_PAD, box_ipxwrapper_options_y, BOX_INNER_WIDTH, text_h, TRUE);
		box_ipxwrapper_options_y += text_h;
		
		int box_ipxwrapper_options_h = box_ipxwrapper_options_y + BOX_BOTTOM_PAD;
		MoveWindow(wh.box_ipxwrapper_options, BOX_SIDE_MARGIN,  0, BOX_WIDTH, box_ipxwrapper_options_h, TRUE);
	}
	
	/* "Network options" (DOSBox encapsulation) */
	
	{
		int box_dosbox_options_y = BOX_TOP_PAD;
		
		MoveWindow(wh.dosbox_server_addr_lbl, BOX_SIDE_PAD, box_dosbox_options_y, lbl_w2, text_h, TRUE);
		MoveWindow(wh.dosbox_server_addr, 15 + lbl_w2, box_dosbox_options_y, node_w, edit_h, TRUE);
		box_dosbox_options_y += edit_h;
		
		MoveWindow(wh.dosbox_server_port_lbl, BOX_SIDE_PAD, box_dosbox_options_y, lbl_w2, text_h, TRUE);
		MoveWindow(wh.dosbox_server_port, 15 + lbl_w2, box_dosbox_options_y, port_w, edit_h, TRUE);
		box_dosbox_options_y += edit_h;
		
		box_dosbox_options_y += text_h; /* Padding. */
		
		MoveWindow(wh.dosbox_coalesce, BOX_SIDE_PAD, box_dosbox_options_y, width - 20, text_h, TRUE);
		box_dosbox_options_y += text_h;
		
		MoveWindow(wh.dosbox_fw_except, BOX_SIDE_PAD, box_dosbox_options_y, width - 20, text_h, TRUE);
		box_dosbox_options_y += text_h;
		
		int box_dosbox_options_h = box_dosbox_options_y + BOX_BOTTOM_PAD;
		
		MoveWindow(wh.box_dosbox_options, BOX_SIDE_MARGIN, 0, BOX_WIDTH, box_dosbox_options_h, TRUE);
	}
	
	/* "Network options" (Real IPX encapsulation) */
	
	{
		/* Find the longest frame type (in horizontal pixels) and multiply it
		 * by 1.5 to get the width of the frame type ComboBox, should be at
		 * least wide enough to avoid truncating them.
		*/
		
		int ft_w = 0;
		for(int count = ComboBox_GetCount(wh.ipx_frame_type), i = 0; i < count; ++i)
		{
			char ft_label[256];
			ComboBox_GetLBText(wh.ipx_frame_type, i, ft_label);
			
			int this_ft_w = get_text_width(wh.ipx_frame_type, ft_label) * 1.5;
			ft_w = std::max(ft_w, this_ft_w);
		}
		
		int box_ipx_options_y = BOX_TOP_PAD;
		
		MoveWindow(wh.ipx_frame_type_lbl, BOX_SIDE_PAD, box_ipx_options_y, lbl_w, text_h, TRUE);
		MoveWindow(wh.ipx_frame_type, 15 + lbl_w, box_ipx_options_y, ft_w, edit_h * 10, TRUE);
		box_ipx_options_y += edit_h;
		
		int box_ipx_options_h = box_ipx_options_y + BOX_BOTTOM_PAD;
		
		MoveWindow(wh.box_ipx_options, BOX_SIDE_MARGIN, 0, BOX_WIDTH, box_ipx_options_h, TRUE);
	}
	
	/* "Options" */
	
	{
		int box_options_y = BOX_TOP_PAD;
		
		MoveWindow(wh.opt_w95, BOX_SIDE_PAD, box_options_y, BOX_INNER_WIDTH, text_h, TRUE);
		box_options_y += text_h + 2;
		
		MoveWindow(wh.opt_log_debug, BOX_SIDE_PAD, box_options_y, BOX_INNER_WIDTH, text_h, TRUE);
		box_options_y += text_h + 2;
		
		MoveWindow(wh.opt_log_trace, BOX_SIDE_PAD, box_options_y, BOX_INNER_WIDTH, text_h, TRUE);
		box_options_y += text_h + 2;
		
		MoveWindow(wh.opt_profile, BOX_SIDE_PAD, box_options_y, BOX_INNER_WIDTH, text_h, TRUE);
		box_options_y += text_h + 2;
		
		int box_options_h = box_options_y + BOX_BOTTOM_PAD;
		
		MoveWindow(wh.box_options, BOX_SIDE_MARGIN, 0, BOX_WIDTH, box_options_h, TRUE);
	}
	
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
	EnableWindow(wh.nic_node,    nic_enabled && main_config.encap_type != ENCAP_TYPE_PCAP);
	
	if(selected_nic >= 0)
	{
		set_checkbox(wh.nic_enabled, nic_enabled);
		
		char net_s[ADDR32_STRING_SIZE];
		addr32_string(net_s, nics[selected_nic].config.netnum);
		
		char node_s[ADDR48_STRING_SIZE];
		addr48_string(node_s, (main_config.encap_type == ENCAP_TYPE_PCAP
			? nics[selected_nic].hwaddr
			: nics[selected_nic].config.nodenum));
		
		SetWindowText(wh.nic_net, net_s);
		SetWindowText(wh.nic_node, node_s);
	}
	
	EnableWindow(wh.opt_log_trace, get_checkbox(wh.opt_log_debug));
	
	std::vector<HWND> visible_groups = {
		wh.box_encap,
	};
	
	if(get_checkbox(wh.encap_ipxwrapper))
	{
		main_config.encap_type = ENCAP_TYPE_IPXWRAPPER;
		
		ShowWindow(wh.box_primary, SW_SHOW);
		visible_groups.push_back(wh.box_primary);
		
		ShowWindow(wh.box_nic, SW_SHOW);
		visible_groups.push_back(wh.box_nic);
		
		ShowWindow(wh.box_ipxwrapper_options, SW_SHOW);
		visible_groups.push_back(wh.box_ipxwrapper_options);
		
		ShowWindow(wh.box_dosbox_options, SW_HIDE);
		ShowWindow(wh.box_ipx_options, SW_HIDE);
	}
	else if(get_checkbox(wh.encap_dosbox))
	{
		main_config.encap_type = ENCAP_TYPE_DOSBOX;
		
		ShowWindow(wh.box_primary, SW_HIDE);
		ShowWindow(wh.box_nic, SW_HIDE);
		ShowWindow(wh.box_ipxwrapper_options, SW_HIDE);
		
		ShowWindow(wh.box_dosbox_options, SW_SHOW);
		visible_groups.push_back(wh.box_dosbox_options);
		
		ShowWindow(wh.box_ipx_options, SW_HIDE);
	}
	else if(get_checkbox(wh.encap_ipx))
	{
		main_config.encap_type = ENCAP_TYPE_PCAP;
		
		ShowWindow(wh.box_primary, SW_SHOW);
		visible_groups.push_back(wh.box_primary);
		
		ShowWindow(wh.box_nic, SW_SHOW);
		visible_groups.push_back(wh.box_nic);
		
		ShowWindow(wh.box_ipxwrapper_options, SW_HIDE);
		ShowWindow(wh.box_dosbox_options, SW_HIDE);
		
		ShowWindow(wh.box_ipx_options, SW_SHOW);
		visible_groups.push_back(wh.box_ipx_options);
	}
	
	visible_groups.push_back(wh.box_options);
	visible_groups.push_back(NULL);
	
	main_window_layout(visible_groups.data());
	
	UpdateWindow(wh.main);
}

static void main_window_layout(HWND *visible_groups)
{
	int y = 0;
	
	for(int i = 0; visible_groups[i] != NULL; ++i)
	{
		HWND group = visible_groups[i];
		
		RECT group_bounds;
		GetWindowRect(group, &group_bounds);
		MapWindowPoints(HWND_DESKTOP, GetParent(group), (LPPOINT)(&group_bounds), 2);
		
		int group_x = group_bounds.left;
		
		int group_w = group_bounds.right - group_bounds.left;
		int group_h = group_bounds.bottom - group_bounds.top;
		
		MoveWindow(group, group_x, y, group_w, group_h, TRUE);
		
		y += group_h;
	}
	
	RECT window_client_area;
	GetClientRect(wh.main, &window_client_area);
	
	int width = window_client_area.right - window_client_area.left;
	int height = window_client_area.bottom - window_client_area.top;
	
	/* Buttons
	 * Height is constant. Vertical position is at the very bottom.
	 *
	 * TODO: Dynamic button sizing.
	*/
	
	const int BTN_PAD = 6;
	
	y += BTN_PAD;
	
	int btn_w = 75;
	int btn_h = 23;
	
	MoveWindow(wh.ok_btn,  width - (3 * btn_w) - (3 * BTN_PAD), y, btn_w, btn_h, TRUE);
	MoveWindow(wh.can_btn, width - (2 * btn_w) - (2 * BTN_PAD), y, btn_w, btn_h, TRUE);
	MoveWindow(wh.app_btn, width - (1 * btn_w) - (1 * BTN_PAD), y, btn_w, btn_h, TRUE);
	
	y += btn_h;
	y += BTN_PAD;
	
	RECT window_area;
	GetWindowRect(wh.main, &window_area);
	
	int area_width = window_area.right - window_area.left;
	int area_height = window_area.bottom - window_area.top;
	
	int pad_width = area_width - width;
	int pad_height = area_height - height;
	
	MoveWindow(wh.main, window_area.left, window_area.top, (width + pad_width), (y + pad_height), TRUE);
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

static HWND create_radio(HWND parent, LPCTSTR label, int id)
{
	return create_child(parent, "BUTTON", label, BS_AUTORADIOBUTTON | WS_TABSTOP | BS_MULTILINE, 0, id);
}

static int get_text_width(HWND hwnd, const char *txt) {
	HDC dc = GetDC(hwnd);
	if(!dc) {
		die("GetDC failed");
	}
	
	HFONT font = (HFONT)(SendMessage(hwnd, WM_GETFONT, 0, 0));
	SelectFont(dc, font);
	
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
