#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include "../include/tun.h"

#ifdef __APPLE__
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#endif

#ifdef __linux__
#include <linux/if_tun.h>
#endif

// Create TUN interface
tun_t* tun_create(const char *preferred_name) {
    tun_t *tun = (tun_t*)calloc(1, sizeof(tun_t));
    if (!tun) {
        perror("Failed to allocate TUN structure");
        return NULL;
    }
    
    tun->fd = -1;
    tun->is_up = false;
    
#ifdef __APPLE__
    // macOS uses utun interface
    // Create control socket
    tun->fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (tun->fd < 0) {
        perror("Failed to create utun socket");
        free(tun);
        return NULL;
    }
    
    // Find utun control ID
    struct ctl_info ctl_info;
    memset(&ctl_info, 0, sizeof(ctl_info));
    strncpy(ctl_info.ctl_name, UTUN_CONTROL_NAME, sizeof(ctl_info.ctl_name));
    
    if (ioctl(tun->fd, CTLIOCGINFO, &ctl_info) < 0) {
        perror("Failed to get utun control info");
        close(tun->fd);
        free(tun);
        return NULL;
    }
    
    // Connect to utun
    struct sockaddr_ctl addr;
    memset(&addr, 0, sizeof(addr));
    addr.sc_len = sizeof(addr);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id = ctl_info.ctl_id;
    addr.sc_unit = 0; // Let system assign unit number
    
    if (connect(tun->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect to utun");
        close(tun->fd);
        free(tun);
        return NULL;
    }
    
    // Get interface name
    socklen_t len = sizeof(tun->name);
    if (getsockopt(tun->fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, 
                   tun->name, &len) < 0) {
        perror("Failed to get utun interface name");
        close(tun->fd);
        free(tun);
        return NULL;
    }
    
    printf("Created utun interface: %s\n", tun->name);
    
#elif __linux__
    // Linux uses /dev/net/tun device
    const char *tun_dev = "/dev/net/tun";
    
    tun->fd = open(tun_dev, O_RDWR);
    if (tun->fd < 0) {
        perror("Failed to open TUN device");
        fprintf(stderr, "Error: You may need to run with sudo or ensure TUN module is loaded\n");
        free(tun);
        return NULL;
    }
    
    // Prepare interface request
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    
    // Set flags: IFF_TUN (layer 3) + IFF_NO_PI (no packet info)
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    
    // Set preferred name if provided
    if (preferred_name) {
        strncpy(ifr.ifr_name, preferred_name, IFNAMSIZ - 1);
    }
    
    // Create TUN interface
    if (ioctl(tun->fd, TUNSETIFF, &ifr) < 0) {
        perror("Failed to create TUN interface");
        close(tun->fd);
        free(tun);
        return NULL;
    }
    
    // Copy interface name
    strncpy(tun->name, ifr.ifr_name, TUN_NAME_MAX - 1);
    tun->name[TUN_NAME_MAX - 1] = '\0';
    
    printf("Created TUN interface: %s\n", tun->name);
    
#else
    (void)preferred_name; // Suppress unused parameter warning
    fprintf(stderr, "TUN interface not supported on this platform\n");
    free(tun);
    return NULL;
#endif
    
    // Set non-blocking mode
    int flags = fcntl(tun->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(tun->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("Failed to set non-blocking mode");
        // Continue anyway, blocking mode is acceptable
    }
    
    return tun;
}

// Destroy TUN interface
void tun_destroy(tun_t *tun) {
    if (!tun) return;
    
    if (tun->is_up) {
        tun_down(tun);
    }
    
    if (tun->fd >= 0) {
        close(tun->fd);
        tun->fd = -1;
    }
    
    printf("Destroyed TUN interface: %s\n", tun->name);
    free(tun);
}

// Read IP packet from TUN interface
int tun_read(tun_t *tun, uint8_t *buffer, size_t len) {
    if (!tun || tun->fd < 0) return -1;
    if (!buffer || len == 0) return -1;
    
#ifdef __APPLE__
    // macOS utun prepends 4-byte address family
    uint8_t temp_buffer[TUN_MTU + 4];
    ssize_t n = read(tun->fd, temp_buffer, sizeof(temp_buffer));
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // No data available (non-blocking)
        }
        perror("Failed to read from TUN");
        return -1;
    }
    
    if (n < 4) {
        fprintf(stderr, "Received packet too short\n");
        return -1;
    }
    
    // Skip 4-byte address family header
    size_t packet_len = n - 4;
    if (packet_len > len) {
        fprintf(stderr, "Packet too large for buffer\n");
        return -1;
    }
    
    memcpy(buffer, temp_buffer + 4, packet_len);
    return packet_len;
    
#elif __linux__
    ssize_t n = read(tun->fd, buffer, len);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // No data available (non-blocking)
        }
        perror("Failed to read from TUN");
        return -1;
    }
    
    return n;
#else
    return -1;
#endif
}

// Write IP packet to TUN interface
int tun_write(tun_t *tun, const uint8_t *buffer, size_t len) {
    if (!tun || tun->fd < 0) return -1;
    if (!buffer || len == 0) return -1;
    
#ifdef __APPLE__
    // macOS utun requires 4-byte address family prefix
    uint8_t temp_buffer[TUN_MTU + 4];
    
    if (len > TUN_MTU) {
        fprintf(stderr, "Packet too large for TUN interface\n");
        return -1;
    }
    
    // Determine address family from IP header
    uint8_t version = (buffer[0] >> 4) & 0x0F;
    
    // Prepend address family (AF_INET for IPv4, AF_INET6 for IPv6)
    temp_buffer[0] = 0;
    temp_buffer[1] = 0;
    temp_buffer[2] = 0;
    temp_buffer[3] = (version == 4) ? AF_INET : AF_INET6;
    
    memcpy(temp_buffer + 4, buffer, len);
    
    ssize_t n = write(tun->fd, temp_buffer, len + 4);
    if (n < 0) {
        perror("Failed to write to TUN");
        return -1;
    }
    
    return n - 4; // Return actual packet length (excluding header)
    
#elif __linux__
    ssize_t n = write(tun->fd, buffer, len);
    if (n < 0) {
        perror("Failed to write to TUN");
        return -1;
    }
    
    return n;
#else
    return -1;
#endif
}

// Configure TUN interface with IP address and netmask
int tun_configure(tun_t *tun, const char *ip_str, const char *netmask_str) {
    if (!tun || !ip_str) return -1;
    
    // Parse IP address
    struct in_addr ip_addr;
    if (inet_aton(ip_str, &ip_addr) == 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip_str);
        return -1;
    }
    
    // Parse netmask (default to /24 if not provided)
    struct in_addr netmask;
    if (netmask_str) {
        if (inet_aton(netmask_str, &netmask) == 0) {
            fprintf(stderr, "Invalid netmask: %s\n", netmask_str);
            return -1;
        }
    } else {
        // Default to /24 (255.255.255.0)
        inet_aton("255.255.255.0", &netmask);
    }
    
    tun->ip_addr = ip_addr.s_addr;
    tun->netmask = netmask.s_addr;
    
    // Configure interface using system call
    char cmd[256];
    
#ifdef __APPLE__
    // macOS requires source and destination IP for utun (point-to-point)
    snprintf(cmd, sizeof(cmd),
             "ifconfig %s inet %s %s netmask %s up",
             tun->name, ip_str, ip_str, netmask_str ? netmask_str : "255.255.255.0");
#elif __linux__
    // Linux uses ip command
    snprintf(cmd, sizeof(cmd),
             "ip addr add %s/%d dev %s",
             ip_str, __builtin_popcount(ntohl(netmask.s_addr)), tun->name);
#else
    fprintf(stderr, "IP configuration not supported on this platform\n");
    return -1;
#endif
    
    printf("Configuring %s: %s\n", tun->name, cmd);
    
    int result = system(cmd);
    if (result != 0) {
        fprintf(stderr, "Warning: Failed to configure IP address (may need sudo)\n");
        fprintf(stderr, "You can manually configure with: %s\n", cmd);
        return -1;
    }
    
    printf("Configured %s with IP: %s/%s\n", 
           tun->name, ip_str, netmask_str ? netmask_str : "255.255.255.0");
    
    return 0;
}

// Bring TUN interface up
int tun_up(tun_t *tun) {
    if (!tun) return -1;
    
    if (tun->is_up) {
        return 0; // Already up
    }
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ifconfig %s up", tun->name);
    
    int result = system(cmd);
    if (result == 0) {
        tun->is_up = true;
        printf("Brought interface %s up\n", tun->name);
        return 0;
    }
    
    fprintf(stderr, "Failed to bring interface up (may need sudo)\n");
    return -1;
}

// Bring TUN interface down
int tun_down(tun_t *tun) {
    if (!tun) return -1;
    
    if (!tun->is_up) {
        return 0; // Already down
    }
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ifconfig %s down", tun->name);
    
    int result = system(cmd);
    if (result == 0) {
        tun->is_up = false;
        printf("Brought interface %s down\n", tun->name);
        return 0;
    }
    
    fprintf(stderr, "Failed to bring interface down\n");
    return -1;
}

// Get interface name
const char* tun_get_name(tun_t *tun) {
    if (!tun) return NULL;
    return tun->name;
}

// Get file descriptor
int tun_get_fd(tun_t *tun) {
    if (!tun) return -1;
    return tun->fd;
}

