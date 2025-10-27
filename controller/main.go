package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"sync"
)

// Client join request
type JoinReq struct {
	NodeID  string `json:"node_id"`
	PubKey  string `json:"pubkey_b64"`
	UDPPort int    `json:"udp_port"`
}

// Peer structure (node info)
type Peer struct {
	NodeID    string `json:"node_id"`
	PubKey    string `json:"pubkey_b64"`
	Endpoint  string `json:"endpoint"`   // real-world IP:port
	VirtualIP string `json:"virtual_ip"` // assigned by controller
}

// Global state
var (
	mu     sync.Mutex
	peers  = make(map[string]Peer)
	nextIP = 2 // starts from 10.0.0.2
)

// Generate virtual IPs like 10.0.0.2, 10.0.0.3, ...
func allocateIP() string {
	ip := fmt.Sprintf("10.0.0.%d", nextIP)
	nextIP++
	return ip
}

// Handle client joining
func joinHandler(w http.ResponseWriter, r *http.Request) {
	var jr JoinReq
	if err := json.NewDecoder(r.Body).Decode(&jr); err != nil {
		http.Error(w, "bad json", http.StatusBadRequest)
		log.Printf("[ERROR] Bad JSON from %s: %v\n", r.RemoteAddr, err)
		return
	}

	remoteIP, _, _ := net.SplitHostPort(r.RemoteAddr)
	endpoint := net.JoinHostPort(remoteIP, fmt.Sprintf("%d", jr.UDPPort))

	mu.Lock()
	defer mu.Unlock()

	// If node already exists, reuse its virtual IP; otherwise, assign new one
	var virtualIP string
	if existing, ok := peers[jr.NodeID]; ok {
		virtualIP = existing.VirtualIP
	} else {
		virtualIP = allocateIP()
	}

	p := Peer{
		NodeID:    jr.NodeID,
		PubKey:    jr.PubKey,
		Endpoint:  endpoint,
		VirtualIP: virtualIP,
	}
	peers[jr.NodeID] = p

	// Prepare full peer list for response
	list := make([]Peer, 0, len(peers))
	for _, v := range peers {
		list = append(list, v)
	}

	// Log the join event
	log.Printf("[JOIN] Node '%s' (%s) registered from %s (UDP %d) -> Assigned Virtual IP: %s | Total peers: %d\n",
		jr.NodeID, jr.PubKey[:8]+"...", remoteIP, jr.UDPPort, virtualIP, len(peers))

	// Respond with all current peers
	resp, _ := json.MarshalIndent(list, "", "  ")
	w.Header().Set("Content-Type", "application/json")
	w.Write(resp)
}

// Return all peers
func peersHandler(w http.ResponseWriter, r *http.Request) {
	mu.Lock()
	list := make([]Peer, 0, len(peers))
	for _, v := range peers {
		list = append(list, v)
	}
	mu.Unlock()

	resp, _ := json.MarshalIndent(list, "", "  ")
	w.Header().Set("Content-Type", "application/json")
	w.Write(resp)
}

func main() {
	http.HandleFunc("/join", joinHandler)
	http.HandleFunc("/peers", peersHandler)

	log.Println("🚀 Controller running on :8080 (use /join and /peers)")
	log.Fatal(http.ListenAndServe(":8080", nil))
}
