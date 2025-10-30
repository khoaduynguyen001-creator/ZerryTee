#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "../include/crypto.h"

int derive_session_key(uint64_t id_a, uint64_t id_b, uint8_t out_key[AEAD_KEY_SIZE]) {
    uint64_t lo = id_a < id_b ? id_a : id_b;
    uint64_t hi = id_a < id_b ? id_b : id_a;
    uint8_t buf[1 + sizeof(uint64_t) * 2];
    buf[0] = 0x5A; // domain sep
    memcpy(buf + 1, &lo, sizeof(uint64_t));
    memcpy(buf + 1 + sizeof(uint64_t), &hi, sizeof(uint64_t));
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(buf, sizeof(buf), hash);
    memcpy(out_key, hash, AEAD_KEY_SIZE);
    return 0;
}

int aead_encrypt_chacha20poly1305(const uint8_t key[AEAD_KEY_SIZE],
                                  const uint8_t nonce[AEAD_NONCE_SIZE],
                                  const uint8_t *plaintext, size_t plaintext_len,
                                  uint8_t *ciphertext, size_t *ciphertext_len) {
    int ok = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, AEAD_NONCE_SIZE, NULL) != 1) break;
        if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) break;
        int outlen = 0, tmplen = 0;
        if (EVP_EncryptUpdate(ctx, ciphertext, &outlen, plaintext, (int)plaintext_len) != 1) break;
        if (EVP_EncryptFinal_ex(ctx, ciphertext + outlen, &tmplen) != 1) break;
        outlen += tmplen;
        uint8_t tag[AEAD_TAG_SIZE];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, AEAD_TAG_SIZE, tag) != 1) break;
        memcpy(ciphertext + outlen, tag, AEAD_TAG_SIZE);
        outlen += AEAD_TAG_SIZE;
        *ciphertext_len = (size_t)outlen;
        ok = 1;
    } while (0);
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

int aead_decrypt_chacha20poly1305(const uint8_t key[AEAD_KEY_SIZE],
                                  const uint8_t nonce[AEAD_NONCE_SIZE],
                                  const uint8_t *ciphertext, size_t ciphertext_len,
                                  uint8_t *plaintext, size_t *plaintext_len) {
    if (ciphertext_len < AEAD_TAG_SIZE) return -1;
    size_t ct_len = ciphertext_len - AEAD_TAG_SIZE;
    const uint8_t *tag = ciphertext + ct_len;
    int ok = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, AEAD_NONCE_SIZE, NULL) != 1) break;
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) break;
        int outlen = 0, tmplen = 0;
        if (EVP_DecryptUpdate(ctx, plaintext, &outlen, ciphertext, (int)ct_len) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, AEAD_TAG_SIZE, (void*)tag) != 1) break;
        if (EVP_DecryptFinal_ex(ctx, plaintext + outlen, &tmplen) != 1) break;
        outlen += tmplen;
        *plaintext_len = (size_t)outlen;
        ok = 1;
    } while (0);
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}
