#!/bin/bash

# Script to generate new TCP/IP documentation files (topics 51-100)
# Usage: ./generate_new_tcpip_docs.sh

# Create docs directory if it doesn't exist
mkdir -p docs

# Define the new topics as an associative array
declare -A topics=(
    [51]="IP_Routing_And_Forwarding"
    [52]="ARP_Protocol_Implementation"
    [53]="ICMP_Error_Messages"
    [54]="IP_Fragmentation_And_Reassembly"
    [55]="IPv6_Extension_Headers"
    [56]="Iterative_Vs_Concurrent_Servers"
    [57]="Thread_Pool_Architecture"
    [58]="Event_Driven_Architecture"
    [59]="Fork_Based_Concurrency"
    [60]="Hybrid_Server_Models"
    [61]="C_Socket_Programming"
    [62]="CPP_Networking_Boost_Asio"
    [63]="Python_Socket_Programming"
    [64]="Go_Networking"
    [65]="Rust_Tokio_Ecosystem"
    [66]="TCP_Fast_Open"
    [67]="TCP_Selective_Acknowledgment"
    [68]="TCP_Window_Scaling"
    [69]="TCP_Timestamps"
    [70]="ECN_Explicit_Congestion_Notification"
    [71]="DPDK_Data_Plane_Development_Kit"
    [72]="XDP_Express_Data_Path"
    [73]="AF_XDP_Sockets"
    [74]="RSS_And_RPS"
    [75]="TSO_And_GSO"
    [76]="SCTP_Stream_Control_Transmission_Protocol"
    [77]="DCCP_Datagram_Congestion_Control_Protocol"
    [78]="MPTCP_Multipath_TCP"
    [79]="gRPC_Over_HTTP2"
    [80]="HTTP3_And_QUIC"
    [81]="TLS_1_3_Features"
    [82]="Certificate_Pinning"
    [83]="Perfect_Forward_Secrecy"
    [84]="DDoS_Mitigation"
    [85]="VPN_Protocols"
    [86]="Packet_Capture_And_Analysis"
    [87]="Network_Metrics_Collection"
    [88]="Flow_Monitoring_NetFlow_sFlow"
    [89]="Application_Layer_Tracing"
    [90]="Network_Performance_Profiling"
    [91]="Mobile_Network_Optimization"
    [92]="WiFi_Direct_And_P2P"
    [93]="Bluetooth_Networking"
    [94]="Cellular_Network_Programming"
    [95]="Network_Switching_Detection"
    [96]="Service_Mesh_Networking"
    [97]="Network_Policies_In_Kubernetes"
    [98]="Edge_Computing_Networking"
    [99]="CDN_Integration"
    [100]="Future_Of_Network_Protocols"
)

# Generate each file using touch (empty files)
echo "Creating new TCP/IP documentation files..."
echo "=========================================="

for num in {51..100}; do
    filename="docs/${num}_${topics[$num]}.md"
    touch "$filename"
    echo "Created: $filename"
done
