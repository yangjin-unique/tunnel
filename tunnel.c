#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h> /* for inet_ntoa debug use, remove later */
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/select.h>
#include <sys/time.h>


#define TUNNEL_UDP_SERVER_PORT     3344
#define BUF_SIZE    2000

enum {CLIENT_MODE, SERVER_MODE};


void 
usage(void)
{
    fprintf(stderr, "prgram [-s | -c <ip address>] -i <tun/tap interface name> -a <tun/tap interface ip>\n");
    exit(1);
}


#ifdef DEBUG
void
dbg(char *fmt, ...)
{
    va_list ar;

    va_start(ar, fmt);
    vfprintf(stdout, fmt, ar);
    va_end(ar);
}
#else
#define dbg(...)
#endif

/**
 * Creat a tun/tap device.
 * @dev name, like tapX
 * @flags 
 *        IFF_TUN   - TUN device (no Ethernet headers) 
 *        IFF_TAP   - TAP device  
 *        IFF_NO_PI - Do not provide packet information  
 * @return file descriptors
 */
int 
tun_alloc(const char *dev, int flags)
{
    struct ifreq ifr;
    int fd, err;

    if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
        fprintf(stderr, "open failed\n");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));

      /* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
       *        IFF_TAP   - TAP device  
       *
       *        IFF_NO_PI - Do not provide packet information  
       */ 
    ifr.ifr_flags = flags; 
    if( *dev )
       strncpy(ifr.ifr_name, dev, IFNAMSIZ);

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
       perror("ioctl faied\n");
       close(fd);
       return err;
    }
#if 0
    if ((err = ioctl(fd, TUNSETPERSIST, (void *) &ifr)) < 0) {
       perror("ioctl faied\n");
       close(fd);
       return err;
    }
#endif
    return fd;
}    


/**
 * Set given interface ip address 
 * @name interface name, like "eth0", "tap0"
 * @addr ip address, struct in_addr
 * @return 
 *      0   set successfully
 *      -1  set failed
 */
int
set_if_addr(const char *name, void *addr)
{
    struct ifreq ifr;
    struct sockaddr_in sin;
    int sockfd;
        
    memset(&ifr, 0, sizeof(ifr));
    memset(&sin, 0, sizeof(sin));
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("set_if_addr error\n");
        return -1;
    }
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr, addr, sizeof(struct in_addr));
    memcpy(&ifr.ifr_addr, &sin, sizeof(struct sockaddr));
    if (ioctl(sockfd, SIOCSIFADDR, &ifr) < 0) {
        perror("set_if_addr error\n");
        return -1;
    }
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("set_if_addr error\n");
        return -1;
    }
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("set_if_addr error\n");
        return -1;
    }
    return 0;
}


/**
 * Write n bytes to fd.
 * @fd 
 * @buf buffer to write
 * @nbytes
 * @reture -1 if write failed, else nbytes
 */
ssize_t 
write_nbytes(int fd, char *buf, ssize_t nbytes) 
{
    ssize_t left = nbytes;
    ssize_t n;

    while (left > 0) {
        n = write(fd, buf, left);
        if (n <= 0) {
            perror("write error\n");
            return -1;
        }
        buf += n;
        left -= n;
    }
    return nbytes;
}


int 
main(int argc, char **argv) 
{
    int tapfd; 
    int netfd; 
    int mode = CLIENT_MODE; /* defalut run as client */
    struct in_addr remote_addr; /* remote server ip address, for client mode use */
    const char *if_name = NULL; /* tun/tap interface name */
    struct in_addr if_addr; /* tun/tap interface ip address */
    char *buf;
    ssize_t nbytes;
    int opt;

    while ((opt = getopt(argc, argv, "sc:i:a:")) != -1) {
        switch (opt) {
        case 's':
            mode = SERVER_MODE;
            break;
        case 'c':
            mode = CLIENT_MODE;
            if (inet_pton(AF_INET, optarg, &remote_addr) != 1) {
                perror("inet_pton error\n");
                exit(1);
            }      
            break;
        case 'i':
            if_name = optarg;
            if (!if_name) {
                fprintf(stderr, "specify the interface name\n");
                exit(1);
            }
            break;
        case 'a':
            if (inet_pton(AF_INET, optarg, &if_addr) != 1) {
                perror("inet_pton error\n");
                exit(1);
            }
            break;
        default:
            usage();
        }
    }

    if (!if_name || *if_name == '\0') {
        usage();
    }
    dbg("mode=%d, if_name=%s, remote_addr=%s\n\n", mode, if_name, inet_ntoa(remote_addr));
    /* create tap fd on tap device */
    tapfd = tun_alloc(if_name, IFF_TAP | IFF_NO_PI);
    if (tapfd < 0) {
        fprintf(stderr, "fd error\n");
        exit(1);
    }
    dbg("creat tun successfully fd = %d, name = %s\n\n", tapfd, if_name);

    if (set_if_addr(if_name, &if_addr) < 0) {
        fprintf(stderr, "set_if_addr error");
        exit(1);
    }
    /*create udp socket on physical device */
    netfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (netfd < 0) {
        perror("socket error\n");
        exit(1);
    }

    if (mode == SERVER_MODE) {
        dbg("server running ...\n\n");
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(TUNNEL_UDP_SERVER_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(netfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind error\n");
            exit(1);
        }
    }
    else {
        dbg("client running...\n\n"); 
        struct sockaddr_in raddr;
        memset(&raddr, 0, sizeof(raddr));
        raddr.sin_family = AF_INET;
        raddr.sin_port = htons(TUNNEL_UDP_SERVER_PORT);
        raddr.sin_addr = remote_addr; 
        if (connect(netfd, (struct sockaddr *)&raddr, sizeof(raddr)) < 0) {
            perror("connect to remote server failed\n"); 
            exit(1);
        }
    }

    buf = (char *) malloc(BUF_SIZE);
    if (!buf) {
        perror("malloc failed\n");
        exit(1);
    }
    
    struct sockaddr srcaddr;
    socklen_t addrlen = 0;
    fd_set readfds;
    int maxfd;
    int nready;
    maxfd = (netfd > tapfd) ? netfd : tapfd;

    for (; ;) {

        FD_ZERO(&readfds);
        FD_SET(netfd, &readfds);
        FD_SET(tapfd, &readfds);
        nready = select(maxfd+1, &readfds, NULL, NULL, NULL);

        if (nready < 0 && errno == EINTR) {
            continue;
        } 
        if (nready < 0) {
            perror("select error\n");
            exit(1);
        }
        if (nready > 0) {
            /* packets on netfd */
            if (FD_ISSET(netfd, &readfds)) {
                if (mode == CLIENT_MODE) {
                    nbytes = read(netfd, buf, BUF_SIZE);
                }
                else {
                    nbytes = recvfrom(netfd, buf,BUF_SIZE, 0, (struct sockaddr *)&srcaddr, &addrlen);
                }
                if (nbytes < 0) {
                    perror("read error1");
                    exit(1);
                }
                dbg("read %d bytes from eth\n\n", nbytes);
                if (nbytes > 0) {
                    nbytes = write_nbytes(tapfd, buf, nbytes); 
                    if (nbytes < 0) {
                        perror("write tapfd error\n");
                        exit(1);
                    }
                }
            }
            /* packets on tapfd */
            if (FD_ISSET(tapfd, &readfds)) {
                nbytes = read(tapfd, buf, BUF_SIZE);
                if (nbytes < 0) {
                    perror("read error2\n");
                    exit(1);
                }
                dbg("read %d bytes from tap\n\n", nbytes);
                if (nbytes > 0) {
                    if (mode == CLIENT_MODE) {
                        nbytes = write_nbytes(netfd, buf, nbytes); 
                    }
                    else {
                        if (addrlen > 0) {
                            nbytes = sendto(netfd, buf, nbytes, 0, &srcaddr, addrlen);
                        }
                        if (nbytes < 0) {
                            perror("write netfd error\n");
                            exit(1);
                        }
                    }
                }
            }
        }
    }

    free(buf);
    return 0;
}
