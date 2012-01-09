== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008-2012 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.

-- INTRODUCTION --

IPXwrapper is a WinSock wrapper which transparently tunnels IPX packets over a
chosen UDP port (54792 by default). To use it, simply copy the four included
DLL files to the directory containing your legacy program.

If you are running Windows Vista or later and the game uses DirectPlay you may
also need to import directplay-win32.reg or directplay-win64.reg as appropriate.

Using more than one program at a time with IPXWrapper requires running ipxrouter
first, this will bind to the UDP port and pass any recieved packets to the
correct process.

Most software binds only to the default interface, if you get no errors but still
can't connect to other computers, try running the ipxconfig program and selecting
the appropriate default interface.

Software using IPXWrapper can't communicate with software that is using the real
IPX protocol and vice-versa.

-- COMPATIBILITY --

Software that uses WinSock 1.x and/or (Pre version 8) DirectPlay is supported.
