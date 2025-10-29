#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../include/client.h"
#include "../include/transport.h"

static client_t *g_client = NULL;

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
    
    // Parse command line arguments
    if (argc >= 2) {
        controller_ip = argv[1];
    }
    if (argc >= 3) {
        controller_port = atoi(argv[2]);
    }
    
    printf("========================================\n");
    printf("ZeroTier Clone Client\n");
    printf("========================================\n");
    printf("Controller: %s:%d\n", controller_ip, controller_port);
    printf("========================================\n\n");
    
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create client (includes TUN interface)
    printf("Creating client...\n");
    g_client = client_create(controller_ip, controller_port);
    if (!g_client) {
        fprintf(stderr, "Failed to create client\n");
        fprintf(stderr, "Note: TUN interface requires root privileges\n");
        fprintf(stderr, "Try running with: sudo %s %s %d\n", argv[0], controller_ip, controller_port);
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
    sleep(1); // Give client thread time to start
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
    
    // Main loop - just wait
    while (1) {
        sleep(1);
    }
    
    return 0;
}

