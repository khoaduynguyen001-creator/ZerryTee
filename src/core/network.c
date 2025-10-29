#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/core.h"

// Create a new network
network_t* network_create(const char *name, bool is_controller) {
    if (!name) return NULL;
    
    network_t *net = (network_t*)calloc(1, sizeof(network_t));
    if (!net) {
        perror("Failed to allocate network");
        return NULL;
    }
    
    // Set network name
    strncpy(net->name, name, MAX_NETWORK_NAME - 1);
    net->name[MAX_NETWORK_NAME - 1] = '\0';
    
    // Generate network ID (simplified - hash of name + timestamp)
    time_t now = time(NULL);
    snprintf((char*)net->network_id, NETWORK_ID_SIZE, "%08lx%08x", 
             (unsigned long)now, (unsigned int)strlen(name));
    
    // Generate network keypair
    if (keypair_generate(&net->network_keys) != 0) {
        free(net);
        return NULL;
    }
    
    net->peer_count = 0;
    net->is_controller = is_controller;
    
    printf("Network '%s' created (controller: %s)\n", 
           net->name, is_controller ? "yes" : "no");
    printf("Network ID: ");
    for (int i = 0; i < NETWORK_ID_SIZE; i++) {
        printf("%02x", net->network_id[i]);
    }
    printf("\n");
    
    return net;
}

// Destroy network and free resources
void network_destroy(network_t *net) {
    if (!net) return;
    
    printf("Destroying network '%s'\n", net->name);
    free(net);
}

// Add a peer to the network
int network_add_peer(network_t *net, peer_t *peer) {
    if (!net || !peer) return -1;
    
    if (net->peer_count >= MAX_PEERS) {
        fprintf(stderr, "Network is full (max %d peers)\n", MAX_PEERS);
        return -1;
    }
    
    // Check if peer already exists
    for (int i = 0; i < net->peer_count; i++) {
        if (net->peers[i].id == peer->id) {
            fprintf(stderr, "Peer %lu already exists\n", peer->id);
            return -1;
        }
    }
    
    // Add peer
    memcpy(&net->peers[net->peer_count], peer, sizeof(peer_t));
    net->peer_count++;
    
    printf("Peer %lu added to network '%s' (total: %d)\n", 
           peer->id, net->name, net->peer_count);
    
    return 0;
}

// Remove a peer from the network
int network_remove_peer(network_t *net, uint64_t peer_id) {
    if (!net) return -1;
    
    for (int i = 0; i < net->peer_count; i++) {
        if (net->peers[i].id == peer_id) {
            // Shift remaining peers
            for (int j = i; j < net->peer_count - 1; j++) {
                memcpy(&net->peers[j], &net->peers[j + 1], sizeof(peer_t));
            }
            net->peer_count--;
            
            printf("Peer %lu removed from network '%s'\n", peer_id, net->name);
            return 0;
        }
    }
    
    fprintf(stderr, "Peer %lu not found\n", peer_id);
    return -1;
}

// Find a peer by ID
peer_t* network_find_peer(network_t *net, uint64_t peer_id) {
    if (!net) return NULL;
    
    for (int i = 0; i < net->peer_count; i++) {
        if (net->peers[i].id == peer_id) {
            return &net->peers[i];
        }
    }
    
    return NULL;
}