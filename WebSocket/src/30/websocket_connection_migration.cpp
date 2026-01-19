#include <libwebsockets.h>
#include <string>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>
#include <iostream>

// Connection state management
enum ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING
};

// Message structure for queuing
struct QueuedMessage {
    uint64_t sequence_num;
    std::string payload;
    std::chrono::steady_clock::time_point timestamp;
};

class WebSocketMigration {
private:
    struct lws_context* context;
    struct lws* wsi;
    ConnectionState state;
    std::string session_token;
    uint64_t next_sequence_num;
    uint64_t last_acked_sequence;
    std::queue<QueuedMessage> pending_messages;
    std::mutex queue_mutex;
    
    // Reconnection parameters
    int reconnect_attempts;
    int max_reconnect_attempts;
    std::chrono::milliseconds base_backoff;
    std::chrono::steady_clock::time_point last_connect_attempt;
    
    // Connection quality tracking
    std::chrono::steady_clock::time_point last_pong_received;
    int missed_pongs;
    const int max_missed_pongs = 3;

public:
    WebSocketMigration() 
        : context(nullptr), 
          wsi(nullptr),
          state(DISCONNECTED),
          next_sequence_num(0),
          last_acked_sequence(0),
          reconnect_attempts(0),
          max_reconnect_attempts(10),
          base_backoff(std::chrono::milliseconds(1000)),
          missed_pongs(0) {}
    
    // Calculate exponential backoff delay
    std::chrono::milliseconds calculate_backoff() {
        int exponent = std::min(reconnect_attempts, 6); // Cap at 2^6 = 64x
        return base_backoff * (1 << exponent); // Exponential: 1s, 2s, 4s, 8s...
    }
    
    // Initialize WebSocket connection with session token
    bool connect(const char* address, int port, const char* path) {
        struct lws_context_creation_info info;
        memset(&info, 0, sizeof(info));
        
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.protocols = protocols;
        info.gid = -1;
        info.uid = -1;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        
        context = lws_create_context(&info);
        if (!context) {
            std::cerr << "Failed to create context" << std::endl;
            return false;
        }
        
        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        
        ccinfo.context = context;
        ccinfo.address = address;
        ccinfo.port = port;
        ccinfo.path = path;
        ccinfo.host = address;
        ccinfo.origin = address;
        ccinfo.protocol = protocols[0].name;
        ccinfo.userdata = this;
        
        state = CONNECTING;
        wsi = lws_client_connect_via_info(&ccinfo);
        last_connect_attempt = std::chrono::steady_clock::now();
        
        return wsi != nullptr;
    }
    
    // Handle reconnection with backoff
    bool attempt_reconnect(const char* address, int port, const char* path) {
        auto now = std::chrono::steady_clock::now();
        auto time_since_last = now - last_connect_attempt;
        auto backoff = calculate_backoff();
        
        if (time_since_last < backoff) {
            // Not enough time has passed
            return false;
        }
        
        if (reconnect_attempts >= max_reconnect_attempts) {
            std::cerr << "Max reconnection attempts reached" << std::endl;
            state = DISCONNECTED;
            return false;
        }
        
        std::cout << "Reconnection attempt " << (reconnect_attempts + 1) 
                  << " after " << backoff.count() << "ms" << std::endl;
        
        reconnect_attempts++;
        state = RECONNECTING;
        
        return connect(address, port, path);
    }
    
    // Send message with sequence number
    bool send_message(const std::string& message) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        QueuedMessage qmsg;
        qmsg.sequence_num = next_sequence_num++;
        qmsg.payload = message;
        qmsg.timestamp = std::chrono::steady_clock::now();
        
        if (state == CONNECTED) {
            // Send immediately
            std::string frame = build_message_frame(qmsg);
            int n = lws_write(wsi, 
                            (unsigned char*)frame.c_str() + LWS_PRE,
                            frame.length(), 
                            LWS_WRITE_TEXT);
            
            if (n < 0) {
                // Failed to send, queue it
                pending_messages.push(qmsg);
                return false;
            }
            return true;
        } else {
            // Queue for later
            pending_messages.push(qmsg);
            return false;
        }
    }
    
    // Build message with metadata for migration
    std::string build_message_frame(const QueuedMessage& msg) {
        // Format: SEQ:TOKEN:PAYLOAD
        return std::to_string(msg.sequence_num) + ":" + 
               session_token + ":" + msg.payload;
    }
    
    // Flush queued messages after reconnection
    void flush_pending_messages() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        while (!pending_messages.empty()) {
            QueuedMessage& msg = pending_messages.front();
            
            std::string frame = build_message_frame(msg);
            int n = lws_write(wsi,
                            (unsigned char*)frame.c_str() + LWS_PRE,
                            frame.length(),
                            LWS_WRITE_TEXT);
            
            if (n < 0) {
                // Failed, stop trying
                break;
            }
            
            pending_messages.pop();
        }
    }
    
    // Handle connection establishment
    void on_connected() {
        state = CONNECTED;
        reconnect_attempts = 0;
        missed_pongs = 0;
        last_pong_received = std::chrono::steady_clock::now();
        
        std::cout << "Connection established" << std::endl;
        
        // Request session token if first connection
        if (session_token.empty()) {
            send_control_message("REQUEST_SESSION");
        } else {
            // Resume session
            send_control_message("RESUME_SESSION:" + session_token);
            flush_pending_messages();
        }
    }
    
    // Handle incoming messages
    void on_message_received(const char* data, size_t len) {
        std::string message(data, len);
        
        // Parse message: TYPE:DATA
        size_t delimiter = message.find(':');
        if (delimiter != std::string::npos) {
            std::string msg_type = message.substr(0, delimiter);
            std::string msg_data = message.substr(delimiter + 1);
            
            if (msg_type == "SESSION_TOKEN") {
                session_token = msg_data;
                std::cout << "Received session token: " << session_token << std::endl;
            } else if (msg_type == "ACK") {
                uint64_t ack_seq = std::stoull(msg_data);
                last_acked_sequence = ack_seq;
                // Remove acknowledged messages from queue
                cleanup_acked_messages(ack_seq);
            } else if (msg_type == "PONG") {
                last_pong_received = std::chrono::steady_clock::now();
                missed_pongs = 0;
            }
        }
    }
    
    // Send control messages
    void send_control_message(const std::string& msg) {
        int n = lws_write(wsi,
                        (unsigned char*)msg.c_str() + LWS_PRE,
                        msg.length(),
                        LWS_WRITE_TEXT);
        if (n < 0) {
            std::cerr << "Failed to send control message" << std::endl;
        }
    }
    
    // Monitor connection health
    void check_connection_health() {
        auto now = std::chrono::steady_clock::now();
        auto time_since_pong = now - last_pong_received;
        
        if (time_since_pong > std::chrono::seconds(30)) {
            missed_pongs++;
            if (missed_pongs >= max_missed_pongs) {
                std::cout << "Connection appears dead, triggering reconnect" << std::endl;
                on_disconnected();
            } else {
                // Send ping
                send_control_message("PING");
            }
        }
    }
    
    // Handle disconnection
    void on_disconnected() {
        state = RECONNECTING;
        wsi = nullptr;
        std::cout << "Connection lost, will attempt reconnection" << std::endl;
    }
    
    // Cleanup acknowledged messages
    void cleanup_acked_messages(uint64_t ack_seq) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        while (!pending_messages.empty() && 
               pending_messages.front().sequence_num <= ack_seq) {
            pending_messages.pop();
        }
    }
    
    ConnectionState get_state() const { return state; }
    size_t pending_message_count() const { return pending_messages.size(); }
    
private:
    static struct lws_protocols protocols[];
};

// Protocol definition
struct lws_protocols WebSocketMigration::protocols[] = {
    {
        "migration-protocol",
        [](struct lws *wsi, enum lws_callback_reasons reason,
           void *user, void *in, size_t len) -> int {
            WebSocketMigration* client = 
                (WebSocketMigration*)lws_context_user(lws_get_context(wsi));
            
            switch (reason) {
                case LWS_CALLBACK_CLIENT_ESTABLISHED:
                    client->on_connected();
                    break;
                    
                case LWS_CALLBACK_CLIENT_RECEIVE:
                    client->on_message_received((const char*)in, len);
                    break;
                    
                case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
                case LWS_CALLBACK_CLOSED:
                    client->on_disconnected();
                    break;
                    
                default:
                    break;
            }
            return 0;
        },
        0,
        0,
    },
    { NULL, NULL, 0, 0 }
};

// Example usage
int main() {
    WebSocketMigration client;
    
    // Initial connection
    if (!client.connect("echo.websocket.org", 443, "/")) {
        std::cerr << "Initial connection failed" << std::endl;
        return 1;
    }
    
    // Main loop
    while (true) {
        // Service the connection
        if (client.get_state() == CONNECTED) {
            client.send_message("Hello from migrating client!");
            client.check_connection_health();
        } else if (client.get_state() == RECONNECTING) {
            client.attempt_reconnect("echo.websocket.org", 443, "/");
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    return 0;
}