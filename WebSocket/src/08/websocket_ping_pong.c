#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#define PING_INTERVAL_SEC 30
#define PONG_TIMEOUT_SEC 10

struct session_data {
    time_t last_ping_sent;
    time_t last_pong_received;
    int waiting_for_pong;
    int connection_alive;
};

static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("Connection established\n");
            session->last_pong_received = time(NULL);
            session->waiting_for_pong = 0;
            session->connection_alive = 1;
            
            // Request a callback for the next writable opportunity
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            time_t current_time = time(NULL);
            
            // Check if we're waiting for a pong and it timed out
            if (session->waiting_for_pong) {
                time_t time_since_ping = current_time - session->last_ping_sent;
                if (time_since_ping > PONG_TIMEOUT_SEC) {
                    lwsl_err("Pong timeout - closing connection\n");
                    return -1; // Close connection
                }
            }
            
            // Send ping if interval has elapsed
            time_t time_since_last_pong = current_time - session->last_pong_received;
            if (time_since_last_pong >= PING_INTERVAL_SEC && !session->waiting_for_pong) {
                unsigned char ping_payload[LWS_PRE + 125];
                unsigned char *p = &ping_payload[LWS_PRE];
                
                // Optional: Add timestamp or identifier to ping payload
                snprintf((char *)p, 125, "ping-%ld", current_time);
                int payload_len = strlen((char *)p);
                
                lwsl_user("Sending ping frame\n");
                int result = lws_write(wsi, p, payload_len, LWS_WRITE_PING);
                
                if (result < 0) {
                    lwsl_err("Failed to send ping\n");
                    return -1;
                }
                
                session->last_ping_sent = current_time;
                session->waiting_for_pong = 1;
            }
            
            // Request another callback for next opportunity
            lws_callback_on_writable(wsi);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE_PONG:
            lwsl_user("Received pong frame (payload: %.*s)\n", (int)len, (char *)in);
            session->last_pong_received = time(NULL);
            session->waiting_for_pong = 0;
            break;
            
        case LWS_CALLBACK_RECEIVE_PING:
            // libwebsockets automatically responds with pong, but we can log it
            lwsl_user("Received ping frame - auto-responding with pong\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            // Handle normal data frames
            lwsl_user("Received data: %.*s\n", (int)len, (char *)in);
            break;
            
        case LWS_CALLBACK_CLOSED:
            lwsl_user("Connection closed\n");
            session->connection_alive = 0;
            break;
            
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "websocket-protocol",
        callback_websocket,
        sizeof(struct session_data),
        1024,
    },
    { NULL, NULL, 0, 0 } // Terminator
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create context\n");
        return -1;
    }
    
    lwsl_user("WebSocket server started on port %d\n", info.port);
    
    // Main event loop
    while (1) {
        lws_service(context, 1000); // Service with 1 second timeout
    }
    
    lws_context_destroy(context);
    return 0;
}