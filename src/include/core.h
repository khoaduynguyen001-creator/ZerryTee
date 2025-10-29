#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

#define MAX_PEERS 256
#define MAX_NETWORK_NAME 64
#define KEYPAIR_SIZE 32
#define SIGNATURE_SIZE 64
#define NETWORK_ID_SIZE 16

// Key pair structure for Ed25519
typedef struct {
    uint8_t public_key[KEYPAIR_SIZE];
    uint8_t private_key[KEYPAIR_SIZE];
} keypair_t;

// Peer information
typedef struct {
    uint64_t id;
    struct sockaddr_in addr;
    keypair_t keys;
    time_t last_seen;
    bool is_active;
} peer_t;

// Network structure
typedef struct {
    uint8_t network_id[NETWORK_ID_SIZE];
    char name[MAX_NETWORK_NAME];
    peer_t peers[MAX_PEERS];
    int peer_count;
    keypair_t network_keys;
    bool is_controller;
} network_t;

// Function declarations
// keypair.c
int keypair_generate(keypair_t *kp);
int keypair_save(const keypair_t *kp, const char *filename);
int keypair_load(keypair_t *kp, const char *filename);
void keypair_print(const keypair_t *kp);

// network.c
network_t* network_create(const char *name, bool is_controller);
void network_destroy(network_t *net);
int network_add_peer(network_t *net, peer_t *peer);
int network_remove_peer(network_t *net, uint64_t peer_id);
peer_t* network_find_peer(network_t *net, uint64_t peer_id);

// peer.c
peer_t* peer_create(uint64_t id, struct sockaddr_in addr);
void peer_destroy(peer_t *peer);
void peer_update_last_seen(peer_t *peer);
bool peer_is_alive(peer_t *peer, int timeout_sec);

#endif // CORE_H