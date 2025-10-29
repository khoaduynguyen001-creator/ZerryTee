#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <pthread.h>
#include "core.h"
#include "transport.h"
#include "tun.h"

#define KEEPALIVE_INTERVAL 30

// Client structure
typedef struct {
    uint64_t client_id;
    transport_t *transport;
    tun_t *tun;                      // TUN interface
    struct sockaddr_in controller_addr;
    bool connected;
    pthread_t thread;
    bool running;
    keypair_t keys;
    char virtual_ip[16];            // Assigned virtual IP (e.g., "10.0.0.1")
} client_t;

// Function declarations
client_t* client_create(const char *controller_ip, uint16_t controller_port);
void client_destroy(client_t *client);
int client_connect(client_t *client);
int client_disconnect(client_t *client);
int client_start(client_t *client);
void client_stop(client_t *client);
void* client_run(void *arg);

#endif // CLIENT_H