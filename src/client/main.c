#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../include/client.h"
#include "../include/transport.h"

static client_t *g_client = NULL;

static int hex2bin(const char *hex, uint8_t *out, size_t outlen) {
    size_t len = strlen(hex);
    if (len != outlen * 2) return -1;
    for (size_t i = 0; i < outlen; i++) {
        char byte_hex[3] = { hex[2*i], hex[2*i+1], 0 };
        char *end = NULL;
        long v = strtol(byte_hex, &end, 16);
        if (*end != '\0' || v < 0 || v > 255) return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    if (g_client) {
        if (g_client->connected) {
            client_disconnect(g_client);
        }
        if (g_client->running) {
            client_stop(g_client);
        }
        client_destroy(g_client);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    const char *controller_ip = "127.0.0.1";
    uint16_t controller_port = DEFAULT_PORT;
    uint8_t network_id[NETWORK_ID_SIZE] = {0};
    int have_netid = 0;
    
    // Parse command line arguments
    if (argc >= 2) {
        controller_ip = argv[1];
    }
    if (argc >= 3) {
        controller_port = atoi(argv[2]);
    }
    if (argc >= 4) {
        if (hex2bin(argv[3], network_id, NETWORK_ID_SIZE) == 0) {
            have_netid = 1;
        } else {
            fprintf(stderr, "Invalid network ID hex (expect 32 hex chars)\n");
            return 1;
        }
    }
    
    printf("========================================\n");
    printf("ZeroTier Clone Client\n");
    printf("========================================\n");
    printf("Controller: %s:%d\n", controller_ip, controller_port);
    if (have_netid) {
        printf("Network ID provided\n");
    } else {
        printf("No network ID provided (controller may reject JOIN)\n");
    }
    printf("========================================\n\n");
    
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create client (includes TUN interface)
    printf("Creating client...\n");
    g_client = client_create(controller_ip, controller_port, have_netid ? network_id : NULL);
    if (!g_client) {
        fprintf(stderr, "Failed to create client\n");
        fprintf(stderr, "Note: TUN interface requires root privileges\n");
        fprintf(stderr, "Try running with: sudo %s %s %d <network_id_hex>\n", argv[0], controller_ip, controller_port);
        return 1;
    }
    
    // Start client
    printf("Starting client...\n");
    if (client_start(g_client) != 0) {
        fprintf(stderr, "Failed to start client\n");
        client_destroy(g_client);
        return 1;
    }
    
    // Connect to controller
    printf("Connecting to controller...\n");
    sleep(1);
    if (client_connect(g_client) != 0) {
        fprintf(stderr, "Failed to connect to controller\n");
        client_stop(g_client);
        client_destroy(g_client);
        return 1;
    }
    
    printf("\nClient connected and running!\n");
    printf("TUN interface: %s\n", tun_get_name(g_client->tun));
    printf("Virtual IP: %s\n", g_client->virtual_ip);
    printf("Press Ctrl+C to disconnect.\n\n");
    
    while (1) {
        sleep(1);
    }
    
    return 0;
}

