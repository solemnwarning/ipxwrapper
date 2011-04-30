== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008-2011 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.

-- INTRODUCTION --

IPXwrapper is a winsock wrapper which transparently tunnels IPX packets over IP
using UDP port 54792. To use it, simply copy ipxwrapper.dll, wsock32.dll and
mswsock.dll to the directory containing your legacy program.

DO NOT REPLACE THE WINSOCK DLLS THAT ARE IN YOUR WINDOWS/SYSTEM32 DIRECTORY AS
THIS WILL BREAK ALL NETWORKING SOFTWARE ON YOUR SYSTEM!

Software using IPXWrapper can't communicate with software that is using the real
IPX protocol and vice-versa.

-- CONFIGURATION --

The ipxconfig program may be used to set additional options, in most cases this is
unnecessary and the default options will work fine. All options are stored in the
registry under HKEY_CURRENT_USER\Software\IPXWrapper.

Interface settings:

IPX network number	- If you don't know what this is, the default will be fine
IPX node number		- Address of the host, defaults to the MAC address of the card
Enable interface	- Sets whether IPX will be emulated for this interface
Make primary interface	- Forces this interface to be the first one in the adapter list

Global settings:

UDP port		- UDP port number to use for tunneling IPX packets. Everyone must
			  use the same port number.
Win 95 SO_BROADCAST bug	- Emulate a bug in the windows 95 IPX implementation, most
			  software probably expects and/or relies on this bug so it is
			  enabled by default
Send broadcasts to all	- Send broadcast packets to all subnets rather than the broadcast
	subnets		  address of the bound interface
Filter recieved packets	- Ignore packets not recieved from an enabled interface
	by subnet

		

-- COMPATIBILITY --

Most of winsock 1.x is implemented, but some software still may not work because
it uses unimpletmented functionaliy. I plan to complete winsock 1.x support
eventually, winsock2 is unlikely, as the API is far bigger and any winsock2
program should be using IP anyway.
