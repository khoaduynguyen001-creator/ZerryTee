#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include <netinet/in.h>
#include "core.h"

#define MAX_PACKET_SIZE 1400
#define DEFAULT_PORT 9993

// Packet types
typedef enum {
    PKT_HELLO = 0x01,
    PKT_HELLO_ACK = 0x02,
    PKT_DATA = 0x03,
    PKT_KEEPALIVE = 0x04,
    PKT_BYE = 0x05,
    PKT_JOIN_REQUEST = 0x06,
    PKT_JOIN_RESPONSE = 0x07
} packet_type_t;

// Packet header
typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t length;
    uint64_t sender_id;
    uint64_t dest_id;
    uint32_t sequence;
} __attribute__((packed)) packet_header_t;

// Transport context
typedef struct {
    int socket_fd;
    uint16_t port;
    struct sockaddr_in bind_addr;
    uint32_t sequence_num;
} transport_t;

// Function declarations
transport_t* transport_create(uint16_t port);
void transport_destroy(transport_t *trans);
int transport_send(transport_t *trans, struct sockaddr_in *dest, 
                   packet_type_t type, uint64_t sender_id, 
                   uint64_t dest_id, const uint8_t *data, uint16_t data_len);
int transport_receive(transport_t *trans, packet_header_t *header, 
                      uint8_t *data, struct sockaddr_in *sender);
int transport_send_hello(transport_t *trans, struct sockaddr_in *dest, 
                         uint64_t sender_id);
int transport_send_keepalive(transport_t *trans, struct sockaddr_in *dest,
                             uint64_t sender_id, uint64_t dest_id);

#endif // TRANSPORT_H