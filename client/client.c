/*
Minimal client:
- generates or loads a persistent keypair (file: keypair.bin)
- POST /join to controller with node_id, pubkey_b64, udp_port
- receives peers list (pubkey + endpoint)
- tries to send an encrypted "hello" to each peer's endpoint using crypto_box_easy
- listens on UDP port for incoming messages and prints raw info

Compile:
  gcc client.c -o client -lsodium -lcurl

Run:
  ./client <controller_host:8080> <local_udp_port> <node_id>

Example:
  ./client 127.0.0.1:8080 40000 node-A
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sodium.h>
#include <curl/curl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define KEYFILE "keypair.bin"
#define PUBKEY_LEN crypto_box_PUBLICKEYBYTES
#define PRIVKEY_LEN crypto_box_SECRETKEYBYTES
#define NONCE_LEN crypto_box_NONCEBYTES
#define MAC_LEN crypto_box_MACBYTES

typedef struct {
    unsigned char pk[PUBKEY_LEN];
    unsigned char sk[PRIVKEY_LEN];
} keypair_t;

int save_keypair(const keypair_t *kp) {
    FILE *f = fopen(KEYFILE, "wb");
    if (!f) return -1;
    fwrite(kp, 1, sizeof(keypair_t), f);
    fclose(f);
    return 0;
}
int load_keypair(keypair_t *kp) {
    FILE *f = fopen(KEYFILE, "rb");
    if (!f) return -1;
    if (fread(kp, 1, sizeof(keypair_t), f) != sizeof(keypair_t)) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

struct mem {
    char *data;
    size_t size;
};
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsz = size * nmemb;
    struct mem *m = (struct mem*)userdata;
    m->data = realloc(m->data, m->size + realsz + 1);
    memcpy(m->data + m->size, ptr, realsz);
    m->size += realsz;
    m->data[m->size] = 0;
    return realsz;
}

char *b64_encode(const unsigned char *in, size_t len) {
    size_t olen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    char *out = malloc(olen);
    sodium_bin2base64(out, olen, in, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    return out;
}

unsigned char *b64_decode_alloc(const char *b64, size_t *outlen) {
    size_t len = strlen(b64);
    unsigned char *out = malloc(len);
    if (sodium_base642bin(out, len, b64, len, NULL, outlen, NULL, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
        free(out); return NULL;
    }
    return out;
}

int post_join_and_get_peers(const char *controller, const char *node_id, const char *pubkey_b64,
                            int udp_port, struct mem *response) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char url[512];
    snprintf(url, sizeof(url), "http://%s/join", controller);

    char body[2048];
    snprintf(body, sizeof(body), "{\"node_id\":\"%s\",\"pubkey_b64\":\"%s\",\"udp_port\":%d}", node_id, pubkey_b64, udp_port);

    struct curl_slist *hdr = NULL;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    response->data = NULL; response->size = 0;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl perform failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_easy_cleanup(curl);
    return 0;
}

/* Very naive JSON parser for controller's simple output */
typedef struct {
    char node_id[64];
    char pubkey_b64[128];
    char endpoint[128];
} peer_t;

peer_t *parse_peers(const char *json, int *count) {
    const char *p = json;
    int cap = 8, n = 0;
    peer_t *arr = malloc(sizeof(peer_t)*cap);
    while ((p = strstr(p, "\"node_id\"")) != NULL) {
        const char *nid = strchr(p, ':');
        if (!nid) break;
        nid = strchr(nid, '"'); if (!nid) break; nid++;
        const char *nid_end = strchr(nid, '"'); if (!nid_end) break;
        size_t ln = nid_end - nid;
        if (n>=cap) { cap*=2; arr=realloc(arr,sizeof(peer_t)*cap); }
        strncpy(arr[n].node_id, nid, ln); arr[n].node_id[ln] = 0;

        const char *pkf = strstr(nid_end, "\"pubkey_b64\"");
        if (!pkf) break;
        const char *pk = strchr(pkf, '"'); if (!pk) break; pk++;
        const char *pk_end = strchr(pk, '"'); if (!pk_end) break;
        ln = pk_end - pk;
        strncpy(arr[n].pubkey_b64, pk, ln); arr[n].pubkey_b64[ln]=0;

        const char *epf = strstr(pk_end, "\"endpoint\"");
        if (!epf) break;
        const char *ep = strchr(epf, '"'); if (!ep) break; ep++;
        const char *ep_end = strchr(ep, '"'); if (!ep_end) break;
        ln = ep_end - ep;
        strncpy(arr[n].endpoint, ep, ln); arr[n].endpoint[ln]=0;

        n++; p = ep_end;
    }
    *count = n;
    return arr;
}

int send_encrypted_udp(int sockfd, const char *endpoint, const unsigned char *dest_pk,
                       const unsigned char *my_pk, const unsigned char *my_sk, const char *plaintext) {
    unsigned char nonce[NONCE_LEN];
    randombytes_buf(nonce, NONCE_LEN);

    size_t ptlen = strlen(plaintext);
    unsigned char *cipher = malloc(ptlen + MAC_LEN);

    if (crypto_box_easy(cipher, (const unsigned char*)plaintext, ptlen, nonce, dest_pk, my_sk) != 0) {
        free(cipher); return -1;
    }

    size_t msglen = NONCE_LEN + (ptlen + MAC_LEN);
    unsigned char *msg = malloc(msglen);
    memcpy(msg, nonce, NONCE_LEN);
    memcpy(msg + NONCE_LEN, cipher, ptlen + MAC_LEN);

    char host[128]; int port;
    if (sscanf(endpoint, "%127[^:]:%d", host, &port) != 2) { free(cipher); free(msg); return -1; }

    struct sockaddr_in peer;
    memset(&peer,0,sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &peer.sin_addr)==0) { free(cipher); free(msg); return -1; }

    ssize_t r = sendto(sockfd, msg, msglen, 0, (struct sockaddr*)&peer, sizeof(peer));
    if (r < 0) perror("sendto");

    free(cipher); free(msg);
    return (int)r;
}

int handle_incoming(int sockfd) {
    unsigned char buf[4096];
    struct sockaddr_in src; socklen_t slen = sizeof(src);
    ssize_t r = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr*)&src, &slen);
    if (r <= 0) return -1;
    printf("[incoming %zd bytes] from %s:%d\n", r, inet_ntoa(src.sin_addr), ntohs(src.sin_port));
    printf("Raw bytes (hex, first 64): ");
    for (int i=0;i<r && i<64;i++) printf("%02x", buf[i]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <controller_host:8080> <local_udp_port> <node_id>\n", argv[0]);
        return 1;
    }
    if (sodium_init() < 0) { fprintf(stderr, "libsodium init failed\n"); return 1; }

    const char *controller = argv[1];
    int udp_port = atoi(argv[2]);
    const char *node_id = argv[3];

    keypair_t kp;
    if (load_keypair(&kp) != 0) {
        crypto_box_keypair(kp.pk, kp.sk);
        if (save_keypair(&kp) != 0) fprintf(stderr, "warning: failed to save keypair\n");
        else printf("New keypair generated and saved to %s\n", KEYFILE);
    } else {
        printf("Loaded keypair from %s\n", KEYFILE);
    }

    char *pub_b64 = b64_encode(kp.pk, PUBKEY_LEN);

    struct mem resp;
    if (post_join_and_get_peers(controller, node_id, pub_b64, udp_port, &resp) != 0) {
        fprintf(stderr, "join failed\n"); return 1;
    }
    printf("Controller returned peers JSON: %s\n", resp.data);

    int peercount=0;
    peer_t *peers = parse_peers(resp.data, &peercount);
    printf("Parsed %d peers\n", peercount);
    for (int i=0;i<peercount;i++) {
        printf("Peer %d: id=%s endpoint=%s\n", i, peers[i].node_id, peers[i].endpoint);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(udp_port);
    if (bind(sock, (struct sockaddr*)&a, sizeof(a))<0) { perror("bind"); return 1; }
    printf("UDP listening on %d\n", udp_port);

    for (int i=0;i<peercount;i++) {
        if (strcmp(peers[i].node_id, node_id) == 0) continue;
        size_t pklen; unsigned char *peer_pk = b64_decode_alloc(peers[i].pubkey_b64, &pklen);
        if (!peer_pk || pklen != PUBKEY_LEN) { free(peer_pk); continue; }
        char msg[256];
        snprintf(msg, sizeof(msg), "hello from %s at %d", node_id, udp_port);
        printf("Sending encrypted hello to %s (%s)\n", peers[i].node_id, peers[i].endpoint);
        send_encrypted_udp(sock, peers[i].endpoint, peer_pk, kp.pk, kp.sk, msg);
        free(peer_pk);
    }

    printf("Entering receive loop (Ctrl-C to quit).\n");
    while (1) {
        handle_incoming(sock);
    }

    free(peers); free(pub_b64); free(resp.data);
    close(sock);
    return 0;
}
