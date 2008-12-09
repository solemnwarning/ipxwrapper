/* ipxwrapper - Winsock/win32 stuff
 * Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef WINSTUFF_H
#define WINSTUFF_H

#define IPX_PTYPE		0x4000
#define IPX_FILTERPTYPE		0x4001
#define IPX_STOPFILTERPTYPE	0x4003
#define IPX_MAXSIZE		0x4006
#define IPX_ADDRESS		0x4007
#define IPX_MAX_ADAPTER_NUM	0x400D

typedef struct _PROTOCOL_INFOA {
	DWORD dwServiceFlags ;
	INT iAddressFamily ;
	INT iMaxSockAddr ;
	INT iMinSockAddr ;
	INT iSocketType ;
	INT iProtocol ;
	DWORD dwMessageSize ;
	LPSTR   lpProtocol ;
} PROTOCOL_INFOA;

typedef struct _PROTOCOL_INFOW {
	DWORD dwServiceFlags ;
	INT iAddressFamily ;
	INT iMaxSockAddr ;
	INT iMinSockAddr ;
	INT iSocketType ;
	INT iProtocol ;
	DWORD dwMessageSize ;
	LPWSTR  lpProtocol ;
} PROTOCOL_INFOW;

typedef struct _IPX_ADDRESS_DATA {
	INT   adapternum;
	UCHAR netnum[4];
	UCHAR nodenum[6];
	BOOLEAN wan;
	BOOLEAN status;
	INT   maxpkt;
	ULONG linkspeed;
} IPX_ADDRESS_DATA;

#ifdef UNICODE
typedef PROTOCOL_INFOW PROTOCOL_INFO;
#else
typedef PROTOCOL_INFOA PROTOCOL_INFO;
#endif

#endif /* !WINSTUFF_H */
