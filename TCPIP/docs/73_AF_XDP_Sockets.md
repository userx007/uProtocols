# AF_XDP Sockets: Zero-Copy Packet I/O

## Detailed Description

AF_XDP (Address Family eXpress Data Path) is a Linux socket type that provides a high-performance, zero-copy interface for packet processing between kernel and user space. Introduced in Linux kernel 4.18, AF_XDP allows applications to bypass most of the kernel's network stack and directly access network packets with minimal overhead.

### Key Concepts

**Zero-Copy Architecture**: AF_XDP eliminates the need to copy packet data between kernel and user space by using shared memory regions. Packets are transferred via descriptor rings that contain pointers to packet buffers rather than the packets themselves.

**XDP Program Integration**: AF_XDP works in conjunction with eBPF/XDP programs that run in the kernel at the earliest possible point in packet processing. These programs can redirect packets to AF_XDP sockets for user-space processing.

**Descriptor Rings**: AF_XDP uses four circular buffer rings:
- **RX Ring**: Kernel writes descriptors of received packets
- **TX Ring**: User space writes descriptors of packets to transmit
- **Fill Ring**: User space provides buffers for the kernel to fill with incoming packets
- **Completion Ring**: Kernel notifies user space when transmitted packets are complete

**UMEM (User Memory)**: A shared memory region registered with the kernel where packet buffers reside. Both kernel and user space can access this memory without copying.

**Operating Modes**:
- **XDP_SKB**: Slowest mode, works with any driver but no zero-copy
- **XDP_DRV**: Native driver mode with zero-copy support (requires driver support)
- **XDP_ZC**: True zero-copy mode (requires specific hardware and driver support)

### Use Cases

- High-frequency trading systems
- Network monitoring and packet capture tools
- Custom network protocol implementations
- Software routers and switches
- DDoS mitigation systems
- Low-latency packet processors

## C/C++ Programming Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/if_ether.h>
#include <linux/if_xdp.h>
#include <linux/if_link.h>
#include <bpf/libbpf.h>
#include <bpf/xsk.h>
#include <net/if.h>

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define BATCH_SIZE 64

struct xdp_context {
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_ring_prod fq;  // Fill queue
    struct xsk_ring_cons cq;  // Completion queue
    struct xsk_umem *umem;
    struct xsk_socket *xsk;
    void *umem_area;
    uint64_t umem_frame_addr[NUM_FRAMES];
    uint32_t umem_frame_free;
};

// Configure UMEM (User Memory)
static struct xsk_umem* configure_umem(void *buffer, uint64_t size,
                                        struct xsk_ring_prod *fq,
                                        struct xsk_ring_cons *cq) {
    struct xsk_umem_config cfg = {
        .fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
        .comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
        .frame_size = FRAME_SIZE,
        .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
        .flags = 0
    };
    
    struct xsk_umem *umem;
    int ret = xsk_umem__create(&umem, buffer, size, fq, cq, &cfg);
    if (ret) {
        fprintf(stderr, "Failed to create UMEM: %s\n", strerror(errno));
        return NULL;
    }
    
    return umem;
}

// Create AF_XDP socket
static struct xsk_socket* create_xsk_socket(const char *ifname, int queue_id,
                                             struct xsk_umem *umem,
                                             struct xsk_ring_cons *rx,
                                             struct xsk_ring_prod *tx) {
    struct xsk_socket_config cfg = {
        .rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
        .tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
        .libbpf_flags = 0,
        .xdp_flags = XDP_FLAGS_DRV_MODE,  // Native driver mode
        .bind_flags = XDP_USE_NEED_WAKEUP
    };
    
    struct xsk_socket *xsk;
    int ret = xsk_socket__create(&xsk, ifname, queue_id, umem, rx, tx, &cfg);
    if (ret) {
        fprintf(stderr, "Failed to create XSK socket: %s\n", strerror(errno));
        return NULL;
    }
    
    return xsk;
}

// Initialize frame addresses
static void init_umem_frames(struct xdp_context *ctx) {
    for (int i = 0; i < NUM_FRAMES; i++) {
        ctx->umem_frame_addr[i] = i * FRAME_SIZE;
    }
    ctx->umem_frame_free = NUM_FRAMES;
}

// Populate fill queue with buffers
static void populate_fill_queue(struct xdp_context *ctx, int count) {
    uint32_t idx_fq = 0;
    
    if (xsk_ring_prod__reserve(&ctx->fq, count, &idx_fq) != count) {
        fprintf(stderr, "Failed to reserve fill queue entries\n");
        return;
    }
    
    for (int i = 0; i < count; i++) {
        *xsk_ring_prod__fill_addr(&ctx->fq, idx_fq++) = 
            ctx->umem_frame_addr[--ctx->umem_frame_free];
    }
    
    xsk_ring_prod__submit(&ctx->fq, count);
}

// Receive packets
static void rx_packets(struct xdp_context *ctx) {
    uint32_t idx_rx = 0, idx_fq = 0;
    unsigned int rcvd = xsk_ring_cons__peek(&ctx->rx, BATCH_SIZE, &idx_rx);
    
    if (!rcvd)
        return;
    
    // Reserve space in fill queue to return buffers
    if (xsk_ring_prod__reserve(&ctx->fq, rcvd, &idx_fq) != rcvd) {
        fprintf(stderr, "Failed to reserve fill queue\n");
        return;
    }
    
    for (unsigned int i = 0; i < rcvd; i++) {
        uint64_t addr = xsk_ring_cons__rx_desc(&ctx->rx, idx_rx)->addr;
        uint32_t len = xsk_ring_cons__rx_desc(&ctx->rx, idx_rx++)->len;
        
        // Access packet data
        uint8_t *pkt = xsk_umem__get_data(ctx->umem_area, addr);
        
        // Process packet (example: print Ethernet header)
        struct ethhdr *eth = (struct ethhdr *)pkt;
        printf("Received packet: len=%u, proto=0x%04x\n", 
               len, ntohs(eth->h_proto));
        
        // Return buffer to fill queue
        *xsk_ring_prod__fill_addr(&ctx->fq, idx_fq++) = addr;
    }
    
    xsk_ring_prod__submit(&ctx->fq, rcvd);
    xsk_ring_cons__release(&ctx->rx, rcvd);
}

// Transmit packets
static void tx_packet(struct xdp_context *ctx, const void *data, size_t len) {
    uint32_t idx_tx = 0;
    
    if (xsk_ring_prod__reserve(&ctx->tx, 1, &idx_tx) != 1) {
        fprintf(stderr, "Failed to reserve TX queue entry\n");
        return;
    }
    
    // Get a free frame
    uint64_t addr = ctx->umem_frame_addr[--ctx->umem_frame_free];
    
    // Copy packet data
    memcpy(xsk_umem__get_data(ctx->umem_area, addr), data, len);
    
    // Setup TX descriptor
    xsk_ring_prod__tx_desc(&ctx->tx, idx_tx)->addr = addr;
    xsk_ring_prod__tx_desc(&ctx->tx, idx_tx)->len = len;
    
    xsk_ring_prod__submit(&ctx->tx, 1);
    
    // Trigger transmission if needed
    if (xsk_ring_prod__needs_wakeup(&ctx->tx)) {
        sendto(xsk_socket__fd(ctx->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    }
}

// Handle TX completions
static void complete_tx(struct xdp_context *ctx) {
    uint32_t idx_cq = 0;
    unsigned int completed = xsk_ring_cons__peek(&ctx->cq, BATCH_SIZE, &idx_cq);
    
    if (!completed)
        return;
    
    for (unsigned int i = 0; i < completed; i++) {
        uint64_t addr = *xsk_ring_cons__comp_addr(&ctx->cq, idx_cq++);
        // Return frame to free pool
        ctx->umem_frame_addr[ctx->umem_frame_free++] = addr;
    }
    
    xsk_ring_cons__release(&ctx->cq, completed);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }
    
    const char *ifname = argv[1];
    struct xdp_context ctx = {0};
    
    // Allocate UMEM
    size_t umem_size = NUM_FRAMES * FRAME_SIZE;
    posix_memalign(&ctx.umem_area, getpagesize(), umem_size);
    
    // Setup UMEM
    ctx.umem = configure_umem(ctx.umem_area, umem_size, &ctx.fq, &ctx.cq);
    if (!ctx.umem) {
        return 1;
    }
    
    // Create socket
    ctx.xsk = create_xsk_socket(ifname, 0, ctx.umem, &ctx.rx, &ctx.tx);
    if (!ctx.xsk) {
        xsk_umem__delete(ctx.umem);
        return 1;
    }
    
    // Initialize frames and populate fill queue
    init_umem_frames(&ctx);
    populate_fill_queue(&ctx, XSK_RING_PROD__DEFAULT_NUM_DESCS);
    
    printf("AF_XDP socket ready on %s\n", ifname);
    
    // Main packet processing loop
    while (1) {
        rx_packets(&ctx);
        complete_tx(&ctx);
        usleep(1000);  // Brief sleep to reduce CPU usage
    }
    
    // Cleanup
    xsk_socket__delete(ctx.xsk);
    xsk_umem__delete(ctx.umem);
    free(ctx.umem_area);
    
    return 0;
}
```

**Compilation:**
```bash
gcc -o af_xdp_example af_xdp_example.c -lbpf -lxdp
```

## Rust Programming Example

```rust
use libbpf_rs::skel::{OpenSkel, Skel, SkelBuilder};
use std::mem::MaybeUninit;
use std::ptr;
use std::time::Duration;

// Using xsk-rs crate for AF_XDP support
use xsk::{
    config::{SocketConfig, UmemConfig},
    socket::Socket,
    umem::Umem,
    CompQueue, FillQueue, RxQueue, TxQueue,
};

const FRAME_COUNT: u32 = 4096;
const FRAME_SIZE: u32 = 2048;
const BATCH_SIZE: usize = 64;

struct XdpSocket {
    umem: Umem,
    socket: Socket,
    fill_queue: FillQueue,
    comp_queue: CompQueue,
    rx_queue: RxQueue,
    tx_queue: TxQueue,
    frame_pool: Vec<u64>,
}

impl XdpSocket {
    fn new(interface: &str, queue_id: u32) -> Result<Self, Box<dyn std::error::Error>> {
        // Configure UMEM
        let umem_config = UmemConfig::default()
            .frame_count(FRAME_COUNT)
            .frame_size(FRAME_SIZE)
            .fill_queue_size(2048)
            .comp_queue_size(2048);

        // Create UMEM
        let (umem, fill_queue, comp_queue) = Umem::new(umem_config)?;

        // Configure socket
        let socket_config = SocketConfig::default()
            .rx_queue_size(2048)
            .tx_queue_size(2048)
            .bind_flags(xsk::BindFlags::XDP_USE_NEED_WAKEUP);

        // Create socket
        let (socket, rx_queue, tx_queue) = Socket::new(
            interface,
            queue_id,
            &umem,
            socket_config,
        )?;

        // Initialize frame pool
        let frame_pool: Vec<u64> = (0..FRAME_COUNT)
            .map(|i| (i * FRAME_SIZE) as u64)
            .collect();

        let mut xdp_socket = XdpSocket {
            umem,
            socket,
            fill_queue,
            comp_queue,
            rx_queue,
            tx_queue,
            frame_pool,
        };

        // Populate fill queue initially
        xdp_socket.populate_fill_queue(2048)?;

        Ok(xdp_socket)
    }

    fn populate_fill_queue(&mut self, count: usize) -> Result<(), Box<dyn std::error::Error>> {
        let available_frames = self.frame_pool.len().min(count);
        
        if available_frames == 0 {
            return Ok(());
        }

        unsafe {
            let nb = self.fill_queue.produce(available_frames);
            
            for _ in 0..nb {
                if let Some(frame_addr) = self.frame_pool.pop() {
                    self.fill_queue.insert(frame_addr);
                }
            }
            
            self.fill_queue.commit();
        }

        Ok(())
    }

    fn receive_packets(&mut self) -> Result<usize, Box<dyn std::error::Error>> {
        let mut received = 0;

        unsafe {
            let nb = self.rx_queue.consume(BATCH_SIZE);
            
            for _ in 0..nb {
                if let Some(desc) = self.rx_queue.peek() {
                    let addr = desc.addr();
                    let len = desc.len() as usize;
                    
                    // Access packet data
                    let packet_data = self.umem.data(addr, len);
                    
                    // Process packet (example: parse Ethernet header)
                    if len >= 14 {
                        let eth_type = u16::from_be_bytes([
                            packet_data[12],
                            packet_data[13],
                        ]);
                        println!("Received packet: len={}, ethertype=0x{:04x}", 
                                len, eth_type);
                    }
                    
                    // Return frame to pool
                    self.frame_pool.push(addr);
                    received += 1;
                }
            }
            
            self.rx_queue.release(nb);
        }

        // Refill the fill queue
        if received > 0 {
            self.populate_fill_queue(received)?;
        }

        Ok(received)
    }

    fn transmit_packet(&mut self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        if self.frame_pool.is_empty() {
            self.complete_tx()?;
            if self.frame_pool.is_empty() {
                return Err("No frames available".into());
            }
        }

        unsafe {
            let nb = self.tx_queue.produce(1);
            
            if nb > 0 {
                let frame_addr = self.frame_pool.pop().unwrap();
                
                // Copy data to UMEM
                let frame_data = self.umem.data_mut(frame_addr, data.len());
                frame_data.copy_from_slice(data);
                
                // Insert TX descriptor
                self.tx_queue.insert(frame_addr, data.len() as u32);
                self.tx_queue.commit();
                
                // Wake up kernel if needed
                if self.tx_queue.needs_wakeup() {
                    self.socket.wake_tx()?;
                }
            }
        }

        Ok(())
    }

    fn complete_tx(&mut self) -> Result<usize, Box<dyn std::error::Error>> {
        let mut completed = 0;

        unsafe {
            let nb = self.comp_queue.consume(BATCH_SIZE);
            
            for _ in 0..nb {
                if let Some(addr) = self.comp_queue.peek() {
                    self.frame_pool.push(addr);
                    completed += 1;
                }
            }
            
            self.comp_queue.release(nb);
        }

        Ok(completed)
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        eprintln!("Usage: {} <interface>", args[0]);
        std::process::exit(1);
    }

    let interface = &args[1];
    let mut xdp_socket = XdpSocket::new(interface, 0)?;
    
    println!("AF_XDP socket ready on {}", interface);

    // Main packet processing loop
    loop {
        // Process received packets
        match xdp_socket.receive_packets() {
            Ok(count) if count > 0 => {
                println!("Processed {} packets", count);
            }
            Ok(_) => {}
            Err(e) => eprintln!("RX error: {}", e),
        }

        // Handle TX completions
        if let Err(e) = xdp_socket.complete_tx() {
            eprintln!("TX completion error: {}", e);
        }

        // Brief sleep to reduce CPU usage
        std::thread::sleep(Duration::from_micros(100));
    }
}
```

**Cargo.toml dependencies:**
```toml
[dependencies]
xsk = "0.2"
libbpf-rs = "0.21"
```

## Summary

AF_XDP sockets provide an exceptional zero-copy interface for high-performance packet processing in Linux. By utilizing shared memory regions (UMEM) and descriptor rings, AF_XDP eliminates the overhead of copying packets between kernel and user space, achieving microsecond-level latencies and multi-million packets-per-second throughput.

The architecture uses four rings (RX, TX, Fill, Completion) to coordinate packet transfers efficiently. Applications register memory buffers with the kernel, and packets are transferred by exchanging descriptors rather than copying data. This makes AF_XDP ideal for network-intensive applications requiring maximum performance, such as high-frequency trading, custom protocol implementations, and network monitoring tools.

While AF_XDP requires more complex setup than traditional Berkeley sockets and depends on driver support for optimal performance, the dramatic performance improvements make it invaluable for latency-sensitive and high-throughput networking applications. Both C/C++ (via libbpf/libxdp) and Rust (via xsk-rs) provide robust libraries for working with AF_XDP, making this powerful technology accessible to developers.