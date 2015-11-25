use strict;
use warnings;

# Local device names, MAC and IP addresses.

our $local_dev_a = "eth1";
our $local_mac_a = "08:00:27:52:5F:9E";
our $local_ip_a  = "172.16.1.11";

our $local_dev_b = "eth2";
our $local_mac_b = "08:00:27:F5:BE:4C";
our $local_ip_b  = "172.16.2.11";

# Remote MAC and IP addresses.

our $remote_mac_a = "08:00:27:C3:6A:E6";
our $remote_ip_a  = "172.16.1.21";

our $remote_mac_b = "08:00:27:43:47:5C";
our $remote_ip_b  = "172.16.2.21";

# IP addresses of additional nodes used for testing DirectPlay.

our $remote_b_ip = "172.16.1.22";
our $remote_c_ip = "172.16.1.23";

# Network broadcast IPs.

our $net_a_bcast  = "172.16.1.255";
our $net_b_bcast  = "172.16.2.255";

1;
