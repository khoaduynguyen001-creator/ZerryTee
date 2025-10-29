#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../include/core.h"

// Generate a new keypair (simplified - in production use libsodium or similar)
int keypair_generate(keypair_t *kp) {
    if (!kp) return -1;
    
    // For prototype: use /dev/urandom for key generation
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /dev/urandom");
        return -1;
    }
    
    // Generate private key
    if (read(fd, kp->private_key, KEYPAIR_SIZE) != KEYPAIR_SIZE) {
        close(fd);
        return -1;
    }
    
    // Generate public key (simplified - should derive from private)
    if (read(fd, kp->public_key, KEYPAIR_SIZE) != KEYPAIR_SIZE) {
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

// Save keypair to file
int keypair_save(const keypair_t *kp, const char *filename) {
    if (!kp || !filename) return -1;
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to open file for writing");
        return -1;
    }
    
    // Write private key
    if (fwrite(kp->private_key, 1, KEYPAIR_SIZE, fp) != KEYPAIR_SIZE) {
        fclose(fp);
        return -1;
    }
    
    // Write public key
    if (fwrite(kp->public_key, 1, KEYPAIR_SIZE, fp) != KEYPAIR_SIZE) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    printf("Keypair saved to %s\n", filename);
    return 0;
}

// Load keypair from file
int keypair_load(keypair_t *kp, const char *filename) {
    if (!kp || !filename) return -1;
    
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file for reading");
        return -1;
    }
    
    // Read private key
    if (fread(kp->private_key, 1, KEYPAIR_SIZE, fp) != KEYPAIR_SIZE) {
        fclose(fp);
        return -1;
    }
    
    // Read public key
    if (fread(kp->public_key, 1, KEYPAIR_SIZE, fp) != KEYPAIR_SIZE) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    printf("Keypair loaded from %s\n", filename);
    return 0;
}

// Print keypair (for debugging)
void keypair_print(const keypair_t *kp) {
    if (!kp) return;
    
    printf("Public Key: ");
    for (int i = 0; i < KEYPAIR_SIZE; i++) {
        printf("%02x", kp->public_key[i]);
    }
    printf("\n");
    
    printf("Private Key: ");
    for (int i = 0; i < 8; i++) {  // Only show first 8 bytes for security
        printf("%02x", kp->private_key[i]);
    }
    printf("...\n");
}