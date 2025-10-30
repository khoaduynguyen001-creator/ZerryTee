#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include "../include/controller.h"

static uint32_t compute_virtual_ip_for_index(int index) {
    // OVERLAY_BASE_IP + (index+2) to skip .0 and .1
    struct in_addr base;
    inet_aton(OVERLAY_BASE_IP, &base);
    uint32_t host = ntohl(base.s_addr);
    uint32_t ip = host + (uint32_t)(index + 2);
    return htonl(ip);
}

// Create controller
controller_t* controller_create(const char *network_name, uint16_t port) {
    if (!network_name) return NULL;
    
    controller_t *ctrl = (controller_t*)calloc(1, sizeof(controller_t));
    if (!ctrl) {
        perror("Failed to allocate controller");
        return NULL;
    }
    
    // Generate controller ID
    ctrl->controller_id = (uint64_t)time(NULL);
    
    // Create network
    ctrl->network = network_create(network_name, true);
    if (!ctrl->network) {
        free(ctrl);
        return NULL;
    }
    
    // Create transport
    ctrl->transport = transport_create(port);
    if (!ctrl->transport) {
        network_destroy(ctrl->network);
        free(ctrl);
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(ctrl->transport->socket_fd, F_GETFL, 0);
    fcntl(ctrl->transport->socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    ctrl->running = false;
    
    printf("Controller created with ID: %llu\n", (unsigned long long)ctrl->controller_id);
    return ctrl;
}

// Destroy controller
void controller_destroy(controller_t *ctrl) {
    if (!ctrl) return;
    
    if (ctrl->running) {
        controller_stop(ctrl);
    }
    
    if (ctrl->transport) {
        transport_destroy(ctrl->transport);
    }
    
    if (ctrl->network) {
        network_destroy(ctrl->network);
    }
    
    printf("Controller destroyed\n");
    free(ctrl);
}

// Start controller thread
int controller_start(controller_t *ctrl) {
    if (!ctrl) return -1;
    
    if (ctrl->running) {
        fprintf(stderr, "Controller already running\n");
        return -1;
    }
    
    ctrl->running = true;
    
    if (pthread_create(&ctrl->thread, NULL, controller_run, ctrl) != 0) {
        perror("Failed to create controller thread");
        ctrl->running = false;
        return -1;
    }
    
    printf("Controller started\n");
    return 0;
}

// Stop controller
void controller_stop(controller_t *ctrl) {
    if (!ctrl || !ctrl->running) return;
    
    printf("Stopping controller...\n");
    ctrl->running = false;
    
    pthread_join(ctrl->thread, NULL);
    printf("Controller stopped\n");
}

// Approve a peer to join the network
int controller_approve_peer(controller_t *ctrl, uint64_t peer_id, 
                            struct sockaddr_in addr) {
    if (!ctrl) return -1;
    
    peer_t *new_peer = peer_create(peer_id, addr);
    if (!new_peer) return -1;

    // Assign a virtual IP address from the overlay subnet
    uint32_t assigned_ip = compute_virtual_ip_for_index(ctrl->network->peer_count);
    new_peer->virtual_ip = assigned_ip;
    
    int result = network_add_peer(ctrl->network, new_peer);
    
    if (result == 0) {
        // Send JOIN_RESPONSE with assigned virtual IP as 4-byte payload
        transport_send(ctrl->transport, &addr, PKT_JOIN_RESPONSE,
                       ctrl->controller_id, peer_id, (const uint8_t*)&assigned_ip, sizeof(assigned_ip));

        // 1) Send existing peers to the new client
        for (int i = 0; i < ctrl->network->peer_count - 1; i++) {
            peer_t *p = &ctrl->network->peers[i];
            uint8_t payload[sizeof(uint64_t) + sizeof(uint32_t) + sizeof(struct sockaddr_in)] = {0};
            memcpy(payload, &p->id, sizeof(uint64_t));
            memcpy(payload + sizeof(uint64_t), &p->virtual_ip, sizeof(uint32_t));
            memcpy(payload + sizeof(uint64_t) + sizeof(uint32_t), &p->addr, sizeof(struct sockaddr_in));
            transport_send(ctrl->transport, &addr, PKT_PEER_INFO, ctrl->controller_id, peer_id, payload, sizeof(payload));
        }

        // 2) Broadcast the new peer to all existing clients
        for (int i = 0; i < ctrl->network->peer_count - 1; i++) {
            peer_t *p = &ctrl->network->peers[i];
            uint8_t payload[sizeof(uint64_t) + sizeof(uint32_t) + sizeof(struct sockaddr_in)] = {0};
            memcpy(payload, &new_peer->id, sizeof(uint64_t));
            memcpy(payload + sizeof(uint64_t), &new_peer->virtual_ip, sizeof(uint32_t));
            memcpy(payload + sizeof(uint64_t) + sizeof(uint32_t), &addr, sizeof(struct sockaddr_in));
            transport_send(ctrl->transport, &p->addr, PKT_PEER_INFO, ctrl->controller_id, p->id, payload, sizeof(payload));
        }
    }
    
    peer_destroy(new_peer);
    return result;
}

// List all peers
void controller_list_peers(controller_t *ctrl) {
    if (!ctrl || !ctrl->network) return;
    
    printf("\n=== Network: %s ===\n", ctrl->network->name);
    printf("Total peers: %d\n", ctrl->network->peer_count);
    
    for (int i = 0; i < ctrl->network->peer_count; i++) {
        peer_t *p = &ctrl->network->peers[i];
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(p->addr.sin_addr), ip_str, INET_ADDRSTRLEN);

        char vip_str[INET_ADDRSTRLEN] = {0};
        if (p->virtual_ip != 0) {
            struct in_addr vip; vip.s_addr = p->virtual_ip;
            inet_ntop(AF_INET, &vip, vip_str, INET_ADDRSTRLEN);
        }
        
        time_t now = time(NULL);
        int elapsed = (int)(now - p->last_seen);
        
        printf("  Peer %llu: %s:%d (vIP: %s) (last seen: %ds ago, %s)\n",
               (unsigned long long)p->id, ip_str, ntohs(p->addr.sin_port),
               vip_str[0] ? vip_str : "-",
               elapsed,
               p->is_active ? "active" : "inactive");
    }
    printf("\n");
}

// Controller main loop
void* controller_run(void *arg) {
    controller_t *ctrl = (controller_t*)arg;
    
    time_t last_keepalive = time(NULL);
    time_t last_check = time(NULL);
    
    printf("Controller thread started\n");
    
    while (ctrl->running) {
        time_t now = time(NULL);
        
        // Receive packets
        packet_header_t header;
        uint8_t data[MAX_PACKET_SIZE];
        struct sockaddr_in sender;
        
        int data_len = transport_receive(ctrl->transport, &header, data, &sender);
        
        if (data_len >= 0) {
            // Handle packet based on type
            switch (header.type) {
                case PKT_HELLO:
                    printf("Received HELLO from peer %llu\n", (unsigned long long)header.sender_id);
                    transport_send(ctrl->transport, &sender, PKT_HELLO_ACK,
                                 ctrl->controller_id, header.sender_id, NULL, 0);
                    break;
                    
                case PKT_JOIN_REQUEST:
                    printf("Received JOIN_REQUEST from peer %llu\n", (unsigned long long)header.sender_id);
                    controller_approve_peer(ctrl, header.sender_id, sender);
                    break;
                    
                case PKT_KEEPALIVE:
                    // Update peer's last seen
                    {
                        peer_t *peer = network_find_peer(ctrl->network, header.sender_id);
                        if (peer) {
                            peer_update_last_seen(peer);
                        }
                    }
                    break;
                    
                case PKT_BYE:
                    printf("Received BYE from peer %llu\n", (unsigned long long)header.sender_id);
                    network_remove_peer(ctrl->network, header.sender_id);
                    break;
                    
                case PKT_LIST_REQUEST: {
                    // Send all peers to requester (sender)
                    for (int i = 0; i < ctrl->network->peer_count; i++) {
                        peer_t *p = &ctrl->network->peers[i];
                        uint8_t payload[sizeof(uint64_t) + sizeof(uint32_t) + sizeof(struct sockaddr_in)] = {0};
                        memcpy(payload, &p->id, sizeof(uint64_t));
                        memcpy(payload + sizeof(uint64_t), &p->virtual_ip, sizeof(uint32_t));
                        memcpy(payload + sizeof(uint64_t) + sizeof(uint32_t), &p->addr, sizeof(struct sockaddr_in));
                        transport_send(ctrl->transport, &sender, PKT_PEER_INFO,
                                       ctrl->controller_id, header.sender_id,
                                       payload, sizeof(payload));
                    }
                    // Indicate end of list
                    transport_send(ctrl->transport, &sender, PKT_LIST_DONE,
                                   ctrl->controller_id, header.sender_id, NULL, 0);
                    break; }
                    
                default:
                    printf("Unknown packet type: %d\n", header.type);
                    break;
            }
        }
        
        // Send keepalives periodically
        if (now - last_keepalive >= KEEPALIVE_INTERVAL) {
            for (int i = 0; i < ctrl->network->peer_count; i++) {
                peer_t *p = &ctrl->network->peers[i];
                transport_send_keepalive(ctrl->transport, &p->addr,
                                        ctrl->controller_id, p->id);
            }
            last_keepalive = now;
        }
        
        // Check peer timeouts
        if (now - last_check >= 10) {
            for (int i = 0; i < ctrl->network->peer_count; i++) {
                peer_t *p = &ctrl->network->peers[i];
                if (!peer_is_alive(p, PEER_TIMEOUT)) {
                    printf("Peer %llu timed out\n", (unsigned long long)p->id);
                }
            }
            last_check = now;
        }
        
        usleep(100000); // 100ms
    }
    
    printf("Controller thread exiting\n");
    return NULL;
}