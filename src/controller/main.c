#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../include/controller.h"

static controller_t *g_controller = NULL;

static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    if (g_controller) {
        controller_stop(g_controller);
        controller_destroy(g_controller);
        g_controller = NULL;
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    const char *network_name = "TestNetwork";
    uint16_t port = DEFAULT_PORT;

    if (argc >= 2) {
        network_name = argv[1];
    }
    if (argc >= 3) {
        port = (uint16_t)atoi(argv[2]);
    }

    printf("========================================\n");
    printf("ZeroTier Clone Controller\n");
    printf("========================================\n");
    printf("Network: %s\n", network_name);
    printf("Port: %d\n", port);
    printf("========================================\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Creating controller...\n");
    g_controller = controller_create(network_name, port);
    if (!g_controller) {
        fprintf(stderr, "Failed to create controller\n");
        return 1;
    }

    printf("Starting controller...\n");
    if (controller_start(g_controller) != 0) {
        fprintf(stderr, "Failed to start controller\n");
        controller_destroy(g_controller);
        return 1;
    }

    printf("\nController is running. Press Ctrl+C to stop.\n\n");

    // Main loop - periodically list peers
    while (1) {
        sleep(5);
        controller_list_peers(g_controller);
    }

    return 0;
}
