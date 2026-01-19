// WebSocket Gateway with Redis Integration for Microservices
// Compile: g++ -std=c++17 ws_gateway.cpp -lwebsockets -lhiredis -lpthread -o ws_gateway

#include <libwebsockets.h>
#include <hiredis/hiredis.h>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <json/json.h>
#include <iostream>

// Connection session data
struct SessionData {
    std::string user_id;
    std::string session_id;
    std::vector<std::string> subscriptions;
    lws* wsi;
};

// Global state
class GatewayState {
public:
    std::map<lws*, SessionData> sessions;
    std::mutex sessions_mutex;
    redisContext* redis_ctx;
    redisContext* redis_sub_ctx;
    bool running = true;
    
    void add_session(lws* wsi, const std::string& user_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        sessions[wsi] = {user_id, generate_session_id(), {}, wsi};
    }
    
    void remove_session(lws* wsi) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        sessions.erase(wsi);
    }
    
    void subscribe_to_channel(lws* wsi, const std::string& channel) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        auto it = sessions.find(wsi);
        if (it != sessions.end()) {
            it->second.subscriptions.push_back(channel);
        }
    }
    
private:
    std::string generate_session_id() {
        static int counter = 0;
        return "session_" + std::to_string(++counter);
    }
};

GatewayState gateway_state;

// Redis publisher - sends messages to microservices
class RedisPublisher {
public:
    RedisPublisher(const std::string& host, int port) {
        ctx = redisConnect(host.c_str(), port);
        if (ctx == nullptr || ctx->err) {
            throw std::runtime_error("Redis connection failed");
        }
    }
    
    ~RedisPublisher() {
        if (ctx) redisFree(ctx);
    }
    
    bool publish(const std::string& channel, const std::string& message) {
        redisReply* reply = (redisReply*)redisCommand(ctx, 
            "PUBLISH %s %s", channel.c_str(), message.c_str());
        if (reply == nullptr) return false;
        bool success = reply->type != REDIS_REPLY_ERROR;
        freeReplyObject(reply);
        return success;
    }
    
    // Call a microservice via Redis request/response pattern
    std::string call_service(const std::string& service, const std::string& request) {
        std::string response_channel = "response_" + std::to_string(time(nullptr));
        
        // Subscribe to response channel
        redisReply* reply = (redisReply*)redisCommand(ctx,
            "SUBSCRIBE %s", response_channel.c_str());
        freeReplyObject(reply);
        
        // Publish request
        Json::Value req;
        req["response_channel"] = response_channel;
        req["payload"] = request;
        publish("service." + service, req.toStyledString());
        
        // Wait for response (simplified - real implementation needs timeout)
        redisReply* msg = (redisReply*)redisCommand(ctx, "BLPOP %s 5", 
            response_channel.c_str());
        std::string response;
        if (msg && msg->type == REDIS_REPLY_ARRAY && msg->elements > 1) {
            response = msg->element[1]->str;
        }
        freeReplyObject(msg);
        
        return response;
    }
    
private:
    redisContext* ctx;
};

// Redis subscriber - receives events from microservices
void redis_subscriber_thread(const std::string& host, int port) {
    redisContext* ctx = redisConnect(host.c_str(), port);
    if (ctx == nullptr || ctx->err) {
        std::cerr << "Redis subscriber connection failed" << std::endl;
        return;
    }
    
    // Subscribe to all microservice events
    redisReply* reply = (redisReply*)redisCommand(ctx, 
        "PSUBSCRIBE events.*");
    freeReplyObject(reply);
    
    while (gateway_state.running) {
        redisReply* msg;
        if (redisGetReply(ctx, (void**)&msg) == REDIS_OK) {
            if (msg->type == REDIS_REPLY_ARRAY && msg->elements >= 4) {
                std::string channel = msg->element[2]->str;
                std::string message = msg->element[3]->str;
                
                // Parse message and route to appropriate WebSocket clients
                Json::Value event;
                Json::Reader reader;
                if (reader.parse(message, event)) {
                    // Broadcast to subscribed clients
                    std::lock_guard<std::mutex> lock(gateway_state.sessions_mutex);
                    for (auto& [wsi, session] : gateway_state.sessions) {
                        bool subscribed = false;
                        for (const auto& sub : session.subscriptions) {
                            if (channel.find(sub) != std::string::npos) {
                                subscribed = true;
                                break;
                            }
                        }
                        
                        if (subscribed) {
                            // Signal LWS to send data
                            lws_callback_on_writable(wsi);
                        }
                    }
                }
            }
            freeReplyObject(msg);
        }
    }
    
    redisFree(ctx);
}

// WebSocket protocol callback
static int callback_websocket(struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len) {
    static RedisPublisher redis_pub("127.0.0.1", 6379);
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            std::cout << "Client connected" << std::endl;
            gateway_state.add_session(wsi, "user_" + std::to_string(time(nullptr)));
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            std::string message((char*)in, len);
            Json::Value msg;
            Json::Reader reader;
            
            if (reader.parse(message, msg)) {
                std::string action = msg["action"].asString();
                
                if (action == "subscribe") {
                    // Subscribe to event channels
                    std::string channel = msg["channel"].asString();
                    gateway_state.subscribe_to_channel(wsi, channel);
                    
                } else if (action == "call_service") {
                    // Make synchronous call to microservice
                    std::string service = msg["service"].asString();
                    std::string request = msg["request"].toStyledString();
                    
                    std::string response = redis_pub.call_service(service, request);
                    
                    // Send response back to client
                    Json::Value resp;
                    resp["type"] = "service_response";
                    resp["data"] = response;
                    std::string resp_str = resp.toStyledString();
                    
                    unsigned char buf[LWS_PRE + resp_str.length()];
                    memcpy(&buf[LWS_PRE], resp_str.c_str(), resp_str.length());
                    lws_write(wsi, &buf[LWS_PRE], resp_str.length(), LWS_WRITE_TEXT);
                    
                } else if (action == "publish_event") {
                    // Publish event to microservices
                    std::string channel = msg["channel"].asString();
                    std::string event = msg["event"].toStyledString();
                    redis_pub.publish("events." + channel, event);
                }
            }
            break;
        }
        
        case LWS_CALLBACK_CLOSED: {
            std::cout << "Client disconnected" << std::endl;
            gateway_state.remove_session(wsi);
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "ws-gateway-protocol",
        callback_websocket,
        0,
        4096,
    },
    { NULL, NULL, 0, 0 }
};

int main() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        std::cerr << "Failed to create WebSocket context" << std::endl;
        return 1;
    }
    
    std::cout << "WebSocket Gateway running on port 8080" << std::endl;
    std::cout << "Integrating with microservices via Redis" << std::endl;
    
    // Start Redis subscriber thread
    std::thread redis_thread(redis_subscriber_thread, "127.0.0.1", 6379);
    
    // Main event loop
    while (gateway_state.running) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    redis_thread.join();
    
    return 0;
}