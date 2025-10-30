# ZeroTier Clone - Implementation Roadmap

## Current State Analysis

### ✅ What's Implemented
- Basic controller-client architecture
- UDP transport layer
- Peer registration and management
- Keepalive mechanism
- Basic packet types (HELLO, JOIN_REQUEST, JOIN_RESPONSE, KEEPALIVE, BYE)
- Keypair generation (structure exists, needs actual crypto implementation)

### ❌ Critical Missing Features for ZeroTier-like Functionality

## Priority 1: Core Networking Features (Must Have)

<!-- ### 1. **Virtual Network Interface (TUN/TAP)** ⭐ CRITICAL
**Status:** Not implemented  
**Why:** This is the foundation that makes ZeroTier transparent to applications.

**What needs to be done:**
- Create TUN interface on Linux/macOS (requires root/admin privileges)
- Read packets from TUN interface (layer 3 IP packets)
- Write packets to TUN interface
- Handle interface lifecycle (create, configure, destroy)

**Implementation:**
- Use `/dev/net/tun` on Linux
- Use `utun` on macOS (requires `net.tun` entitlement or root)
- Add `tun.c` module with functions:
  - `tun_create(const char *name, char *dev_name)` - Create TUN interface
  - `tun_destroy(int fd)` - Destroy TUN interface
  - `tun_read(int fd, uint8_t *buffer, size_t len)` - Read IP packet
  - `tun_write(int fd, uint8_t *buffer, size_t len)` - Write IP packet
  - `tun_configure(int fd, const char *ip, const char *netmask)` - Configure IP

**Files to create:**
- `src/tun/tun.c`
- `src/include/tun.h`

--- -->

<!-- ### 2. **Peer-to-Peer Direct Communication** ⭐ CRITICAL
**Status:** Partially implemented (controller communication only)  
**Why:** Clients must communicate directly with each other, not through controller.

**What needs to be done:**
- Controller sends peer list to clients in JOIN_RESPONSE
- Clients store peer information locally
- Clients establish direct connections to other peers
- Implement peer discovery and connection handshake

**Implementation:**
- Add `peer_list` to `client_t` structure
- Add `PEER_INTRODUCE` packet type - controller sends peer info to clients
- Add `PEER_HELLO` packet type - peer-to-peer handshake
- Implement `client_connect_to_peer()` function
- Store peer addresses in client's peer table

**Files to modify:**
- `src/include/client.h` - Add peer list
- `src/client/client.c` - Implement peer connections
- `src/controller/controller.c` - Send peer list on JOIN

--- -->

### 3. **Packet Forwarding Between TUN and Peers** ⭐ CRITICAL
**Status:** Not implemented  
**Why:** Need to forward traffic between virtual interface and network peers.

**What needs to be done:**
- Read IP packets from TUN interface
- Parse destination IP, map to peer ID
- Encapsulate and send to peer via UDP
- Receive packets from peers, decapsulate, write to TUN

**Implementation:**
- Add packet forwarding thread in client
- Implement IP packet parsing (minimal - just read dest IP)
- Implement virtual IP to peer ID mapping
- Encapsulate IP packets in DATA packets
- Forward packets between TUN and UDP transport

**Files to modify:**
- `src/client/client.c` - Add forwarding logic
- Create IP packet parsing utilities

---

### 4. **Virtual IP Address Assignment** ⭐ CRITICAL
**Status:** Not implemented  
**Why:** Each client needs a unique IP in the virtual network.

**What needs to be done:**
- Controller assigns IP addresses to clients (e.g., 10.0.0.x)
- Store virtual IP in client and peer structures
- Include virtual IP in JOIN_RESPONSE
- Configure TUN interface with assigned IP

**Implementation:**
- Add `virtual_ip` field to `peer_t` and `client_t`
- Controller assigns IPs from pool (e.g., 10.0.0.1-254)
- Send assigned IP in JOIN_RESPONSE packet
- Use `tun_configure()` to set IP on interface

**Files to modify:**
- `src/include/core.h` - Add virtual_ip to peer_t
- `src/include/client.h` - Add virtual_ip to client_t
- `src/client/client.c` - Handle IP assignment
- `src/controller/controller.c` - Assign IPs

---

## Priority 2: Security & Encryption (Important)

### 5. **End-to-End Encryption**
**Status:** Structure exists, implementation needed  
**Why:** Secure communication between peers.

**What needs to be done:**
- Implement actual encryption (AES-256-GCM or ChaCha20-Poly1305)
- Key exchange mechanism (Diffie-Hellman or use Ed25519 keys)
- Encrypt DATA packets between peers
- Sign control packets (HELLO, JOIN_REQUEST, etc.)

**Implementation:**
- Use OpenSSL or libsodium for crypto
- Implement `packet_encrypt()` and `packet_decrypt()` functions
- Add encryption keys to peer structure
- Encrypt all DATA packets

**Files to create/modify:**
- `src/core/crypto.c` - Encryption functions
- `src/include/core.h` - Add encryption keys
- `src/transport/transport.c` - Encrypt/decrypt packets

---

### 6. **Authentication & Authorization**
**Status:** Not implemented  
**Why:** Verify peers are authorized to join network.

**What needs to be done:**
- Sign JOIN_REQUEST with client's private key
- Controller verifies signature
- Network membership tokens/certificates

**Implementation:**
- Use Ed25519 signatures for authentication
- Add signature to JOIN_REQUEST
- Controller verifies before approving

---

## Priority 3: Network Features (Nice to Have)

### 7. **NAT Traversal (STUN-like)**
**Status:** Not implemented  
**Why:** Connect peers behind NATs/firewalls.

**What needs to be done:**
- Detect NAT type
- Implement hole punching
- Use controller as relay if direct connection fails
- STUN-like protocol for NAT detection

**Implementation:**
- Add NAT detection packet types
- Implement relay forwarding on controller
- Try direct connection first, fallback to relay

---

### 8. **ARP Protocol Handling**
**Status:** Not implemented  
**Why:** TUN interface needs ARP for network discovery.

**What needs to be done:**
- Handle ARP requests/replies
- Map MAC addresses to peer IDs
- Respond to ARP queries for virtual IPs

**Implementation:**
- Parse ARP packets from TUN
- Maintain ARP table (MAC -> Peer ID mapping)
- Generate ARP replies

---

### 9. **Routing & Path Selection**
**Status:** Not implemented  
**Why:** Find optimal paths between peers.

**What needs to be done:**
- Measure latency to peers
- Select best path
- Implement mesh routing algorithm

**Implementation:**
- Track peer latencies
- Implement routing table
- Path selection algorithm

---

### 10. **Network Configuration**
**Status:** Not implemented  
**Why:** ZeroTier networks have configurable settings.

**What needs to be done:**
- IP range configuration
- Network rules (access control)
- Private/public network settings

---

## Implementation Order

### Phase 1: Basic P2P Networking (Week 1-2)
1. ✅ Virtual Network Interface (TUN)
2. ✅ Virtual IP assignment
3. ✅ Peer-to-peer direct communication
4. ✅ Basic packet forwarding (TUN ↔ UDP)

**Result:** Clients can ping each other via virtual IPs

### Phase 2: Security (Week 3)
5. ✅ End-to-end encryption
6. ✅ Authentication

**Result:** Secure encrypted communication

### Phase 3: Reliability (Week 4)
7. ✅ NAT traversal
8. ✅ ARP handling
9. ✅ Better error handling

**Result:** Works across NATs and firewalls

### Phase 4: Advanced Features (Week 5+)
10. ✅ Routing optimization
11. ✅ Network configuration
12. ✅ Metrics and monitoring

**Result:** Production-ready ZeroTier clone

---

## Quick Start: Minimum Viable Product

To get a basic working ZeroTier clone, focus on these 4 features:

1. **TUN interface** - Create virtual network interface
2. **Virtual IP assignment** - Give each client an IP
3. **Peer discovery** - Controller sends peer list to clients
4. **Packet forwarding** - Forward packets between TUN and peers

With these four features, you can:
- Create a virtual network
- Connect multiple clients
- Ping between clients using virtual IPs
- Route traffic through the overlay network

---

## Technical Notes

### Platform Differences
- **Linux:** Uses `/dev/net/tun`, requires CAP_NET_ADMIN or root
- **macOS:** Uses `utun` API, requires root or entitlement
- **Windows:** Uses TAP-Windows adapter (different API)

### Packet Format
Current packet format is basic. Consider adding:
- Virtual IP addresses in headers
- Encryption metadata
- Sequence numbers for reliability
- Type-length-value (TLV) fields for extensibility

### Testing Strategy
1. Create virtual network on localhost
2. Connect multiple clients
3. Test ping between clients
4. Test TCP/UDP applications
5. Test across real networks

---

## Resources

- [ZeroTier Documentation](https://docs.zerotier.com/)
- [TUN/TAP Interfaces](https://www.kernel.org/doc/Documentation/networking/tuntap.txt)
- [Linux Network Programming](https://www.kernel.org/doc/Documentation/networking/)
- [ZeroTier Protocol](https://github.com/zerotier/ZeroTierOne/blob/master/ZT_Protocol.md)

