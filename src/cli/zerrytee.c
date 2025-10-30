#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../include/transport.h"

static void usage() {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  zerrytee list <controller_ip> [port]\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp(argv[1], "list") != 0) {
        usage();
        return 1;
    }

    const char *controller_ip = argv[2];
    uint16_t port = (argc >= 4) ? (uint16_t)atoi(argv[3]) : DEFAULT_PORT;

    transport_t *t = transport_create(0);
    if (!t) {
        fprintf(stderr, "Failed to create transport\n");
        return 1;
    }

    struct sockaddr_in ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.sin_family = AF_INET;
    ctrl.sin_port = htons(port);
    if (inet_pton(AF_INET, controller_ip, &ctrl.sin_addr) <= 0) {
        fprintf(stderr, "Invalid controller IP\n");
        transport_destroy(t);
        return 1;
    }

    // Send LIST_REQUEST
    if (transport_send(t, &ctrl, PKT_LIST_REQUEST, 0 /*cli id*/, 0, NULL, 0) != 0) {
        fprintf(stderr, "Failed to send list request\n");
        transport_destroy(t);
        return 1;
    }

    printf("Connections (from %s:%d):\n", controller_ip, port);

    while (1) {
        packet_header_t header; uint8_t data[MAX_PACKET_SIZE]; struct sockaddr_in sender;
        int n = transport_receive(t, &header, data, &sender);
        if (n < 0) {
            usleep(10000);
            continue;
        }
        if (header.type == PKT_PEER_INFO && n == (int)(sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t))) {
            uint64_t pid; uint32_t vip_net; uint32_t ip_be; uint16_t port_be;
            memcpy(&pid, data, sizeof(uint64_t));
            memcpy(&vip_net, data + 8, sizeof(uint32_t));
            memcpy(&ip_be, data + 12, sizeof(uint32_t));
            memcpy(&port_be, data + 16, sizeof(uint16_t));
            char vip[16]; struct in_addr v; v.s_addr = vip_net; inet_ntop(AF_INET, &v, vip, sizeof(vip));
            struct in_addr a; a.s_addr = ip_be;
            printf("- peer_id=%llu addr=%s:%d vIP=%s\n", (unsigned long long)pid,
                   inet_ntoa(a), ntohs(port_be), vip);
        } else if (header.type == PKT_LIST_DONE) {
            break;
        }
    }

    transport_destroy(t);
    return 0;
}
