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
    strncpy(client->virtual_ip, "10.0.0.1", sizeof(client->virtual_ip) - 1); // Default IP
    
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
                printf("Received %d bytes from TUN interface\n", n);
                // TODO: Forward to peer via UDP (will be implemented in Step 4)
                // For now, just log it
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
                        // TODO: Parse assigned virtual IP from JOIN_RESPONSE
                        // For now, configure TUN with default IP
                        if (client->tun) {
                            tun_configure(client->tun, client->virtual_ip, "255.255.255.0");
                            tun_up(client->tun);
                            printf("TUN interface configured with IP: %s\n", client->virtual_ip);
                        }
                        break;
                        
                    case PKT_KEEPALIVE:
                        printf("Received KEEPALIVE from controller\n");
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