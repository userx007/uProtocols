#include <iostream>
#include <string>
#include <map>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <csignal>

class PacketCapture {
private:
    pcap_t *handle;
    std::map<std::string, int> protocol_stats;
    std::map<std::string, int> ip_stats;
    int total_packets;
    bool running;
    
public:
    PacketCapture() : handle(nullptr), total_packets(0), running(true) {}
    
    ~PacketCapture() {
        if (handle) {
            pcap_close(handle);
        }
    }
    
    bool initialize(const std::string &device, const std::string &filter_str) {
        char errbuf[PCAP_ERRBUF_SIZE];
        bpf_u_int32 net, mask;
        
        // Get network info
        if (pcap_lookupnet(device.c_str(), &net, &mask, errbuf) == -1) {
            std::cerr << "Warning: Couldn't get netmask: " << errbuf << std::endl;
            net = 0;
            mask = 0;
        }
        
        // Open device
        handle = pcap_open_live(device.c_str(), BUFSIZ, 1, 1000, errbuf);
        if (!handle) {
            std::cerr << "Couldn't open device: " << errbuf << std::endl;
            return false;
        }
        
        // Compile and set filter
        struct bpf_program filter;
        if (pcap_compile(handle, &filter, filter_str.c_str(), 0, net) == -1) {
            std::cerr << "Filter compilation failed: " << pcap_geterr(handle) << std::endl;
            return false;
        }
        
        if (pcap_setfilter(handle, &filter) == -1) {
            std::cerr << "Filter installation failed: " << pcap_geterr(handle) << std::endl;
            pcap_freecode(&filter);
            return false;
        }
        
        pcap_freecode(&filter);
        return true;
    }
    
    void process_packet(const u_char *packet, int len) {
        struct ether_header *eth = (struct ether_header *)packet;
        
        if (ntohs(eth->ether_type) != ETHERTYPE_IP) {
            return;
        }
        
        struct ip *ip_hdr = (struct ip *)(packet + sizeof(struct ether_header));
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_hdr->ip_src), src_ip, INET_ADDRSTRLEN);
        
        // Update statistics
        total_packets++;
        ip_stats[src_ip]++;
        
        switch (ip_hdr->ip_p) {
            case IPPROTO_TCP: {
                protocol_stats["TCP"]++;
                struct tcphdr *tcp = (struct tcphdr *)(packet + sizeof(struct ether_header) + 
                                                       (ip_hdr->ip_hl * 4));
                std::cout << "TCP packet: " << src_ip << ":" << ntohs(tcp->th_sport)
                          << " -> " << ntohs(tcp->th_dport) << std::endl;
                break;
            }
            case IPPROTO_UDP: {
                protocol_stats["UDP"]++;
                struct udphdr *udp = (struct udphdr *)(packet + sizeof(struct ether_header) + 
                                                       (ip_hdr->ip_hl * 4));
                std::cout << "UDP packet: " << src_ip << ":" << ntohs(udp->uh_sport)
                          << " -> " << ntohs(udp->uh_dport) << std::endl;
                break;
            }
            case IPPROTO_ICMP:
                protocol_stats["ICMP"]++;
                std::cout << "ICMP packet from: " << src_ip << std::endl;
                break;
            default:
                protocol_stats["Other"]++;
                break;
        }
    }
    
    static void packet_callback(u_char *user, const struct pcap_pkthdr *h, 
                                const u_char *bytes) {
        PacketCapture *capture = reinterpret_cast<PacketCapture*>(user);
        capture->process_packet(bytes, h->len);
    }
    
    void start_capture(int packet_count = -1) {
        std::cout << "Starting packet capture..." << std::endl;
        pcap_loop(handle, packet_count, packet_callback, (u_char*)this);
    }
    
    void print_statistics() {
        std::cout << "\n=== Capture Statistics ===" << std::endl;
        std::cout << "Total packets: " << total_packets << std::endl;
        
        std::cout << "\nProtocol distribution:" << std::endl;
        for (const auto &entry : protocol_stats) {
            double percentage = (entry.second * 100.0) / total_packets;
            std::cout << "  " << entry.first << ": " << entry.second 
                      << " (" << percentage << "%)" << std::endl;
        }
        
        std::cout << "\nTop 5 source IPs:" << std::endl;
        int count = 0;
        for (const auto &entry : ip_stats) {
            if (count++ >= 5) break;
            std::cout << "  " << entry.first << ": " << entry.second 
                      << " packets" << std::endl;
        }
    }
    
    void stop() {
        running = false;
        if (handle) {
            pcap_breakloop(handle);
        }
    }
};

PacketCapture *global_capture = nullptr;

void signal_handler(int signum) {
    std::cout << "\nInterrupt signal received. Stopping capture..." << std::endl;
    if (global_capture) {
        global_capture->stop();
    }
}

int main(int argc, char *argv[]) {
    std::string device = "eth0";
    std::string filter = "ip";
    
    if (argc > 1) {
        device = argv[1];
    }
    if (argc > 2) {
        filter = argv[2];
    }
    
    PacketCapture capture;
    global_capture = &capture;
    
    // Setup signal handler
    std::signal(SIGINT, signal_handler);
    
    if (!capture.initialize(device, filter)) {
        return 1;
    }
    
    std::cout << "Capturing on device: " << device << std::endl;
    std::cout << "Filter: " << filter << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;
    
    capture.start_capture(50); // Capture 50 packets
    capture.print_statistics();
    
    return 0;
}

// Compile: g++ -o advanced_bpf_with_statistics advanced_bpf_with_statistics.cpp -lpcap -std=c++11
// Run: sudo ./advanced_bpf_with_statistics eth0 "tcp or udp"