###Simple Tunnel
This is a simple program to build a UDP (TCP not coming yet) tunnel between two hosts by using tun/tap device driver in linux kernel. TUN/TAP device are widely used by open source project OpenVPN/OpenVZ/Vtun/Xen. More info about tun/tap driver can be found in [kernel doc!](https://www.kernel.org/doc/Documentation/networking/tuntap.txt) 

##How to Run it?

You can run this on two linux hosts, one runs as a server and the other runs as a client. The two hosts must be reachable in both derections.

#Server side
Run: ./tunnel -s -i tapX -a <192.168.168.1>

#Clinet side
Run: ./tunnle -c <server_ip_address> -i tapX -a <192.168.168.2>

Now, use ping tool to test if 192.168.168.2 is reachable on server, and test if 192.168.168.1 is reachable on client.
