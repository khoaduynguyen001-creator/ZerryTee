#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include "../include/client.h"

// --- Packet forwarding helpers ---
static inline uint32_t extract_ipv4_dest(const uint8_t *packet, size_t len) {
    // Minimal IPv4 validation: need at least 20 bytes, version 4
    if (len < 20) return 0;
    uint8_t version = (packet[0] >> 4) & 0x0F;
    if (version != 4) return 0;
    // Dest IP at bytes 16..19
    uint32_t dest;
    memcpy(&dest, packet + 16, sizeof(uint32_t));
    return dest; // network byte order
}

static client_peer_t* find_peer_by_vip(client_t *client, uint32_t dest_ip_net) {
    for (int i = 0; i < client->peer_count; i++) {
        struct in_addr vip_addr;
        if (inet_pton(AF_INET, client->peers[i].virtual_ip, &vip_addr) == 1) {
            if (vip_addr.s_addr == dest_ip_net) {
                return &client->peers[i];
            }
        }
    }
    return NULL;
}

static void forward_ip_packet_to_peer(client_t *client, const uint8_t *buf, int len) {
    uint32_t dest_ip_net = extract_ipv4_dest(buf, (size_t)len);
    if (dest_ip_net == 0) {
        // Not IPv4 or invalid; ignore for now
        return;
    }
    client_peer_t *peer = find_peer_by_vip(client, dest_ip_net);
    if (!peer) {
        // No mapping yet; could be ARP/ICMP or peer not discovered
        return;
    }
    // Send the raw IP packet as payload
    transport_send(client->transport, &peer->addr, PKT_DATA,
                   client->client_id, peer->id, buf, (uint16_t)len);
}

static void install_overlay_route(tun_t *tun) {
    if (!tun) return;
    const char *ifname = tun_get_name(tun);
    if (!ifname || !*ifname) return;

#ifdef __APPLE__
    // macOS: route add -net 10.0.0.0/24 via interface
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "route -n add -net 10.0.0.0/24 -interface %s", ifname);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Warning: failed to add route via %s; you may need sudo: %s\n", ifname, cmd);
    } else {
        printf("Installed overlay route 10.0.0.0/24 via %s\n", ifname);
    }
#elif __linux__
    // Linux: ip route add 10.0.0.0/24 dev <ifname>
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip route add 10.0.0.0/24 dev %s", ifname);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Warning: failed to add route via %s; you may need sudo: %s\n", ifname, cmd);
    } else {
        printf("Installed overlay route 10.0.0.0/24 via %s\n", ifname);
    }
#else
    (void)ifname;
#endif
}

// Create client
client_t* client_create(const char *controller_ip, uint16_t controller_port) {
    if (!controller_ip) return NULL;
    
    client_t *client = (client_t*)calloc(1, sizeof(client_t));
    if (!client) {
        perror("Failed to allocate client");
        return NULL;
    }
    
    // Generate client ID
    client->client_id = (uint64_t)time(NULL) + (uint64_t)getpid();
    
    // Generate client keypair
    if (keypair_generate(&client->keys) != 0) {
        free(client);
        return NULL;
    }
    
    // Create TUN interface
    printf("Creating TUN interface...\n");
    client->tun = tun_create(NULL);
    if (!client->tun) {
        fprintf(stderr, "Failed to create TUN interface\n");
        fprintf(stderr, "Note: TUN interface requires root privileges\n");
        free(client);
        return NULL;
    }
    
    // Create transport on random port
    client->transport = transport_create(0);
    if (!client->transport) {
        tun_destroy(client->tun);
        free(client);
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(client->transport->socket_fd, F_GETFL, 0);
    fcntl(client->transport->socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Set controller address
    memset(&client->controller_addr, 0, sizeof(client->controller_addr));
    client->controller_addr.sin_family = AF_INET;
    client->controller_addr.sin_port = htons(controller_port);
    if (inet_pton(AF_INET, controller_ip, &client->controller_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid controller IP address\n");
        transport_destroy(client->transport);
        tun_destroy(client->tun);
        free(client);
        return NULL;
    }
    
    client->connected = false;
    client->running = false;
    client->virtual_ip[0] = '\0';
    client->peer_count = 0;
    
    printf("Client created with ID: %llu\n", (unsigned long long)client->client_id);
    printf("Controller: %s:%d\n", controller_ip, controller_port);
    printf("TUN interface: %s\n", tun_get_name(client->tun));
    
    return client;
}

// Destroy client
void client_destroy(client_t *client) {
    if (!client) return;
    
    if (client->running) {
        client_stop(client);
    }
    
    if (client->connected) {
        client_disconnect(client);
    }
    
    if (client->transport) {
        transport_destroy(client->transport);
    }
    
    if (client->tun) {
        tun_destroy(client->tun);
    }
    
    printf("Client destroyed\n");
    free(client);
}

// Connect to controller
int client_connect(client_t *client) {
    if (!client) return -1;
    
    if (client->connected) {
        printf("Already connected to controller\n");
        return 0;
    }
    
    // Send HELLO
    printf("Sending HELLO to controller...\n");
    if (transport_send_hello(client->transport, &client->controller_addr,
                            client->client_id) != 0) {
        return -1;
    }
    
    // Send JOIN_REQUEST
    printf("Sending JOIN_REQUEST to controller...\n");
    if (transport_send(client->transport, &client->controller_addr,
                      PKT_JOIN_REQUEST, client->client_id, 0, NULL, 0) != 0) {
        return -1;
    }
    
    client->connected = true;
    printf("Connected to controller\n");
    
    return 0;
}

// Disconnect from controller
int client_disconnect(client_t *client) {
    if (!client || !client->connected) return -1;
    
    // Send BYE
    printf("Sending BYE to controller...\n");
    transport_send(client->transport, &client->controller_addr,
                  PKT_BYE, client->client_id, 0, NULL, 0);
    
    client->connected = false;
    printf("Disconnected from controller\n");
    
    return 0;
}

// Start client thread
int client_start(client_t *client) {
    if (!client) return -1;
    
    if (client->running) {
        fprintf(stderr, "Client already running\n");
        return -1;
    }
    
    client->running = true;
    
    if (pthread_create(&client->thread, NULL, client_run, client) != 0) {
        perror("Failed to create client thread");
        client->running = false;
        return -1;
    }
    
    printf("Client started\n");
    return 0;
}

// Stop client
void client_stop(client_t *client) {
    if (!client || !client->running) return;
    
    printf("Stopping client...\n");
    client->running = false;
    
    pthread_join(client->thread, NULL);
    printf("Client stopped\n");
}

// Client main loop
void* client_run(void *arg) {
    client_t *client = (client_t*)arg;
    
    time_t last_keepalive = time(NULL);
    uint8_t tun_buffer[1500];
    
    printf("Client thread started\n");
    
    while (client->running) {
        time_t now = time(NULL);
        
        // Setup select for both TUN and UDP socket
        fd_set read_fds;
        struct timeval timeout;
        int max_fd = 0;
        
        FD_ZERO(&read_fds);
        
        // Add TUN file descriptor
        if (client->tun) {
            int tun_fd = tun_get_fd(client->tun);
            FD_SET(tun_fd, &read_fds);
            if (tun_fd > max_fd) max_fd = tun_fd;
        }
        
        // Add UDP socket
        if (client->transport) {
            FD_SET(client->transport->socket_fd, &read_fds);
            if (client->transport->socket_fd > max_fd) max_fd = client->transport->socket_fd;
        }
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
        
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready < 0 && errno != EINTR) {
            perror("select failed");
            break;
        }
        
        // Read from TUN interface (packets from OS to forward to network)
        if (client->tun && FD_ISSET(tun_get_fd(client->tun), &read_fds)) {
            int n = tun_read(client->tun, tun_buffer, sizeof(tun_buffer));
            if (n > 0) {
                // Forward based on destination virtual IP (unicast)
                forward_ip_packet_to_peer(client, tun_buffer, n);
            }
        }
        
        // Receive packets from network (UDP)
        if (client->transport && FD_ISSET(client->transport->socket_fd, &read_fds)) {
            packet_header_t header;
            uint8_t data[MAX_PACKET_SIZE];
            struct sockaddr_in sender;
            
            int data_len = transport_receive(client->transport, &header, data, &sender);
            
            if (data_len >= 0) {
                // Handle packet based on type
                switch (header.type) {
                    case PKT_HELLO_ACK:
                        printf("Received HELLO_ACK from controller\n");
                        break;
                        
                    case PKT_JOIN_RESPONSE:
                        printf("Received JOIN_RESPONSE - Successfully joined network!\n");
                        // Payload contains 4-byte virtual IP (network byte order)
                        if (data_len == 4) {
                            uint32_t vip_net;
                            memcpy(&vip_net, data, sizeof(uint32_t));
                            struct in_addr vip; vip.s_addr = vip_net;
                            inet_ntop(AF_INET, &vip, client->virtual_ip, sizeof(client->virtual_ip));
                            printf("Assigned virtual IP: %s\n", client->virtual_ip);
                            // Configure TUN with assigned IP
                            if (client->tun) {
                                tun_configure(client->tun, client->virtual_ip, OVERLAY_NETMASK);
                                tun_up(client->tun);
                                printf("TUN interface configured with IP: %s\n", client->virtual_ip);
                                // Install overlay route automatically
                                install_overlay_route(client->tun);
                            }
                        }
                        break;
                        
                    case PKT_PEER_INFO: {
                        if (data_len == (int)(sizeof(uint64_t) + sizeof(uint32_t) + sizeof(struct sockaddr_in))) {
                            uint64_t pid; uint32_t vip_net; struct sockaddr_in paddr;
                            memcpy(&pid, data, sizeof(uint64_t));
                            memcpy(&vip_net, data + sizeof(uint64_t), sizeof(uint32_t));
                            memcpy(&paddr, data + sizeof(uint64_t) + sizeof(uint32_t), sizeof(struct sockaddr_in));

                            char vip_str[16];
                            struct in_addr vip; vip.s_addr = vip_net;
                            inet_ntop(AF_INET, &vip, vip_str, sizeof(vip_str));

                            // Add to local peer list if not exists
                            bool exists = false;
                            for (int i = 0; i < client->peer_count; i++) {
                                if (client->peers[i].id == pid) { exists = true; break; }
                            }
                            if (!exists && client->peer_count < CLIENT_MAX_PEERS) {
                                client_peer_t *cp = &client->peers[client->peer_count++];
                                cp->id = pid; cp->addr = paddr; cp->reachable = false;
                                strncpy(cp->virtual_ip, vip_str, sizeof(cp->virtual_ip) - 1);
                                printf("Discovered peer %llu at %s:%d (vIP %s)\n",
                                       (unsigned long long)pid,
                                       inet_ntoa(paddr.sin_addr), ntohs(paddr.sin_port), vip_str);

                                // Send direct hello to peer
                                transport_send(client->transport, &cp->addr, PKT_PEER_HELLO,
                                               client->client_id, cp->id, NULL, 0);
                            }
                        }
                        break; }

                    case PKT_PEER_HELLO:
                        printf("Received direct PEER_HELLO from peer %llu\n", (unsigned long long)header.sender_id);
                        // Mark peer as reachable
                        for (int i = 0; i < client->peer_count; i++) {
                            if (client->peers[i].id == header.sender_id) {
                                client->peers[i].reachable = true;
                                break;
                            }
                        }
                        break;
                        
                    case PKT_KEEPALIVE:
                        // Send keepalive back
                        transport_send_keepalive(client->transport, 
                                               &client->controller_addr,
                                               client->client_id, header.sender_id);
                        break;
                        
                    case PKT_DATA:
                        printf("Received DATA packet (%d bytes)\n", data_len);
                        // TODO: Forward to TUN interface (will be implemented in Step 4)
                        if (client->tun && data_len > 0) {
                            tun_write(client->tun, data, data_len);
                        }
                        break;
                        
                    default:
                        printf("Unknown packet type: %d\n", header.type);
                        break;
                }
            }
        }
        
        // Send keepalives periodically
        if (client->connected && now - last_keepalive >= KEEPALIVE_INTERVAL) {
            transport_send_keepalive(client->transport, &client->controller_addr,
                                    client->client_id, 0);
            last_keepalive = now;
        }
    }
    
    printf("Client thread exiting\n");
    return NULL;
}