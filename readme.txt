== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008-2012 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.

-- INTRODUCTION --

IPXWrapper is a wrapper library which transparently tunnels IPX packets over IP
using UDP. To use it, simply copy the four included DLL files to the directory
containing your legacy program.

If you are running Windows Vista or later and the game uses DirectPlay you may
also need to import directplay-win32.reg or directplay-win64.reg as appropriate.

The "wildcard" interface is used by default, this will send and receieve packets
on all available network interfaces and may cause problems if you share more
than one network with the other computers running IPXWrapper. To use a single
interface, run the ipxconfig program and select a different primary interface,
change which interfaces are enabled or pick a different interface in the options
of your program.

Software using IPXWrapper can't communicate with software that is using the real
IPX protocol and vice-versa.

-- COMPATIBILITY --

Software that uses WinSock 1.x and/or (Pre version 8) DirectPlay is supported.
