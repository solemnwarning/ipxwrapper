IPXWrapper development hints
============================

Compiling
---------

IPXWrapper can be compiled using the toolchains from win-builds.org, both under Windows and cross-compiling from Linux. If cross-compiling, set the HOST environment variable to i686-w64-mingw32 or similar, depending on your system.

In addition to a GCC toolchain, you will need the following:

  * GNU Make
  * NASM
  * Perl
  * WinPcap headers

Running the test suite
----------------------

The test suite requires a Linux system and a Windows system, connected by two Ethernet networks with fixed IPv4 addresses on each. You must run `make tools` before attempting to run the test suite.

The Linux system:

  * Must have the following Perl modules installed:
   * IPC::Run
   * Net::Libdnet::Eth
   * Net::Pcap
   * NetPacket
   * Test::Spec
  * Must have access to the same IPXWrapper source tree as the Windows system.
  * Should be configured to automatically authenticate (i.e. by public key) when SSHing to the IP address of the Windows system.

The Windows system:

  * Must not have any network adapters besides the two used for testing.
  * Must be running an SSH server which kills orphaned processes upon disconnect, such as Bitvise SSH server.
  * Must map the IPXWrapper source tree to drive Z: within the SSH session.
  * Must have WinPcap installed and usable.
  * Must not be using Windows Firewall.

Once you have configured both machines, edit tests/config.pm as required and run `prove tests/` as root.

**NOTE**: The tests will fail if one of the systems is a VirtualBox host and the other is a guest running under the same system, communicating through host-only or bridged adapters due to a VirtualBox bug described here: https://www.virtualbox.org/ticket/3768. Using two VirtualBox guests is fine.
