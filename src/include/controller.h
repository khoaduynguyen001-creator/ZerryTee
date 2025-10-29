#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdbool.h>
#include <pthread.h>
#include "core.h"
#include "transport.h"

#define KEEPALIVE_INTERVAL 30
#define PEER_TIMEOUT 90

// Controller structure
typedef struct {
    network_t *network;
    transport_t *transport;
    pthread_t thread;
    bool running;
    uint64_t controller_id;
} controller_t;

// Function declarations
controller_t* controller_create(const char *network_name, uint16_t port);
void controller_destroy(controller_t *ctrl);
int controller_start(controller_t *ctrl);
void controller_stop(controller_t *ctrl);
int controller_approve_peer(controller_t *ctrl, uint64_t peer_id, 
                            struct sockaddr_in addr);
void controller_list_peers(controller_t *ctrl);
void* controller_run(void *arg);

#endif // CONTROLLER_H