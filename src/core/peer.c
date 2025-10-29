#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include "../include/core.h"

// Create a new peer
peer_t* peer_create(uint64_t id, struct sockaddr_in addr) {
    peer_t *peer = (peer_t*)calloc(1, sizeof(peer_t));
    if (!peer) {
        perror("Failed to allocate peer");
        return NULL;
    }
    
    peer->id = id;
    memcpy(&peer->addr, &addr, sizeof(struct sockaddr_in));
    peer->last_seen = time(NULL);
    peer->is_active = true;
    
    // Generate keypair for this peer
    if (keypair_generate(&peer->keys) != 0) {
        free(peer);
        return NULL;
    }
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    printf("Peer %lu created at %s:%d\n", id, ip_str, ntohs(addr.sin_port));
    
    return peer;
}

// Destroy peer and free resources
void peer_destroy(peer_t *peer) {
    if (!peer) return;
    
    printf("Destroying peer %lu\n", peer->id);
    free(peer);
}

// Update peer's last seen timestamp
void peer_update_last_seen(peer_t *peer) {
    if (!peer) return;
    
    peer->last_seen = time(NULL);
    peer->is_active = true;
}

// Check if peer is alive based on timeout
bool peer_is_alive(peer_t *peer, int timeout_sec) {
    if (!peer) return false;
    
    time_t now = time(NULL);
    time_t elapsed = now - peer->last_seen;
    
    if (elapsed > timeout_sec) {
        peer->is_active = false;
        return false;
    }
    
    return peer->is_active;
}