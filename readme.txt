== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for the licensing terms.

-- INTRODUCTION --

ipxwrapper is a winsock wrapper which intercepts IPX socket calls and tunnels
them over UDP, this allows you to run legacy software which uses IPX on systems
that don't have the IPX protocol installed, it also allows you to tunnel the
connection over IP tunneling software such as Hamachi.

ipxwrapper currently supports most of winsock 1.x, more of the winsock library
will be implemented over time.

-- INSTALLATION --

To install ipxwrapper, copy the DLL files to the directory containing the legacy
program exe, you need to do this for every program you want to tunnel.

DO NOT REPLACE THE WINSOCK DLLS THAT ARE IN YOUR WINDOWS/SYSTEM32 DIRECTORY AS
THIS WILL BREAK ALL NETWORKING SOFTWARE ON YOUR SYSTEM!
