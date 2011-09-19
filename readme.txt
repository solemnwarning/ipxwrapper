== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008-2011 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.

-- INTRODUCTION --

IPXwrapper is a winsock wrapper which transparently tunnels IPX packets over a
chosen UDP port (54792 by default). To use it, simply copy the four included
DLL files to the directory containing your legacy program.

If you are running Windows Vista or newer and the game uses DirectPlay you will
also need to import the directplay.reg registry key.

Running more than one IPX program at a time requires running ipxrouter.exe first.

Most software binds only to the default interface, if you get no errors but still
can't connect to other computers, try running the ipxconfig program and setting
the appropriate default interface.

Software using IPXWrapper can't communicate with software that is using the real
IPX protocol and vice-versa.

-- COMPATIBILITY --

Most of WinSock 1.x is implemented, full WinSock 1.x support will eventually be
finished and WinSock 2.x may be added if any software that actually requires it
exists.

DirectPlay games are also supported, although the service provider is pretty new
and lacks several features such as asynchronous I/O support.
