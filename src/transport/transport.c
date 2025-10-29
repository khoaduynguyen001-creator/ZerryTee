#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include "../include/transport.h"

// Create transport layer
transport_t* transport_create(uint16_t port) {
    transport_t *trans = (transport_t*)calloc(1, sizeof(transport_t));
    if (!trans) {
        perror("Failed to allocate transport");
        return NULL;
    }
    
    // Create UDP socket
    trans->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (trans->socket_fd < 0) {
        perror("Failed to create socket");
        free(trans);
        return NULL;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(trans->socket_fd, SOL_SOCKET, SO_REUSEADDR, 
                   &reuse, sizeof(reuse)) < 0) {
        perror("Failed to set SO_REUSEADDR");
    }
    
    // Bind socket
    trans->port = port;
    memset(&trans->bind_addr, 0, sizeof(trans->bind_addr));
    trans->bind_addr.sin_family = AF_INET;
    trans->bind_addr.sin_addr.s_addr = INADDR_ANY;
    trans->bind_addr.sin_port = htons(port);
    
    if (bind(trans->socket_fd, (struct sockaddr*)&trans->bind_addr, 
             sizeof(trans->bind_addr)) < 0) {
        perror("Failed to bind socket");
        close(trans->socket_fd);
        free(trans);
        return NULL;
    }
    
    trans->sequence_num = 0;
    
    printf("Transport layer initialized on port %d\n", port);
    return trans;
}

// Destroy transport layer
void transport_destroy(transport_t *trans) {
    if (!trans) return;
    
    if (trans->socket_fd >= 0) {
        close(trans->socket_fd);
    }
    
    printf("Transport layer destroyed\n");
    free(trans);
}

// Send packet
int transport_send(transport_t *trans, struct sockaddr_in *dest, 
                   packet_type_t type, uint64_t sender_id, 
                   uint64_t dest_id, const uint8_t *data, uint16_t data_len) {
    if (!trans || !dest) return -1;
    
    if (data_len > MAX_PACKET_SIZE - sizeof(packet_header_t)) {
        fprintf(stderr, "Data too large: %d bytes\n", data_len);
        return -1;
    }
    
    uint8_t buffer[MAX_PACKET_SIZE];
    packet_header_t *header = (packet_header_t*)buffer;
    
    // Fill header
    header->version = 1;
    header->type = type;
    header->length = htons(data_len);
    header->sender_id = sender_id;
    header->dest_id = dest_id;
    header->sequence = htonl(trans->sequence_num++);
    
    // Copy data if present
    if (data && data_len > 0) {
        memcpy(buffer + sizeof(packet_header_t), data, data_len);
    }
    
    int total_len = sizeof(packet_header_t) + data_len;
    ssize_t sent = sendto(trans->socket_fd, buffer, total_len, 0,
                          (struct sockaddr*)dest, sizeof(*dest));
    
    if (sent < 0) {
        perror("Failed to send packet");
        return -1;
    }
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(dest->sin_addr), ip_str, INET_ADDRSTRLEN);
    printf("Sent packet type %d to %s:%d (%zd bytes)\n", 
           type, ip_str, ntohs(dest->sin_port), sent);
    
    return 0;
}

// Receive packet
int transport_receive(transport_t *trans, packet_header_t *header, 
                      uint8_t *data, struct sockaddr_in *sender) {
    if (!trans || !header) return -1;
    
    uint8_t buffer[MAX_PACKET_SIZE];
    socklen_t sender_len = sizeof(*sender);
    
    ssize_t received = recvfrom(trans->socket_fd, buffer, MAX_PACKET_SIZE, 0,
                                (struct sockaddr*)sender, &sender_len);
    
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Failed to receive packet");
        }
        return -1;
    }
    
    if (received < (ssize_t)sizeof(packet_header_t)) {
        fprintf(stderr, "Packet too small: %zd bytes\n", received);
        return -1;
    }
    
    // Copy header
    memcpy(header, buffer, sizeof(packet_header_t));
    header->length = ntohs(header->length);
    header->sequence = ntohl(header->sequence);
    
    // Copy data if present
    int data_len = received - sizeof(packet_header_t);
    if (data && data_len > 0) {
        memcpy(data, buffer + sizeof(packet_header_t), data_len);
    }
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sender->sin_addr), ip_str, INET_ADDRSTRLEN);
    printf("Received packet type %d from %s:%d (%zd bytes)\n", 
           header->type, ip_str, ntohs(sender->sin_port), received);
    
    return data_len;
}

// Send HELLO packet
int transport_send_hello(transport_t *trans, struct sockaddr_in *dest, 
                         uint64_t sender_id) {
    return transport_send(trans, dest, PKT_HELLO, sender_id, 0, NULL, 0);
}

// Send KEEPALIVE packet
int transport_send_keepalive(transport_t *trans, struct sockaddr_in *dest,
                             uint64_t sender_id, uint64_t dest_id) {
    return transport_send(trans, dest, PKT_KEEPALIVE, sender_id, dest_id, NULL, 0);
}