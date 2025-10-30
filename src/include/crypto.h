#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define AEAD_KEY_SIZE 32
#define AEAD_NONCE_SIZE 12
#define AEAD_TAG_SIZE 16

int derive_session_key(uint64_t id_a, uint64_t id_b, uint8_t out_key[AEAD_KEY_SIZE]);
int aead_encrypt_chacha20poly1305(const uint8_t key[AEAD_KEY_SIZE],
                                  const uint8_t nonce[AEAD_NONCE_SIZE],
                                  const uint8_t *plaintext, size_t plaintext_len,
                                  uint8_t *ciphertext, size_t *ciphertext_len);
int aead_decrypt_chacha20poly1305(const uint8_t key[AEAD_KEY_SIZE],
                                  const uint8_t nonce[AEAD_NONCE_SIZE],
                                  const uint8_t *ciphertext, size_t ciphertext_len,
                                  uint8_t *plaintext, size_t *plaintext_len);

#endif
