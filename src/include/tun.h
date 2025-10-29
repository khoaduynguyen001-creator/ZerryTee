#ifndef TUN_H
#define TUN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TUN_MTU 1500
#define TUN_NAME_MAX 16

// TUN interface structure
typedef struct {
    int fd;                          // File descriptor for TUN device
    char name[TUN_NAME_MAX];         // Interface name (e.g., "utun0", "tun0")
    bool is_up;                      // Interface up/down status
    uint32_t ip_addr;                // Virtual IP address (network byte order)
    uint32_t netmask;                // Netmask (network byte order)
} tun_t;

// Function declarations
tun_t* tun_create(const char *preferred_name);
void tun_destroy(tun_t *tun);
int tun_read(tun_t *tun, uint8_t *buffer, size_t len);
int tun_write(tun_t *tun, const uint8_t *buffer, size_t len);
int tun_configure(tun_t *tun, const char *ip_str, const char *netmask_str);
int tun_up(tun_t *tun);
int tun_down(tun_t *tun);
const char* tun_get_name(tun_t *tun);
int tun_get_fd(tun_t *tun);

#endif // TUN_H

