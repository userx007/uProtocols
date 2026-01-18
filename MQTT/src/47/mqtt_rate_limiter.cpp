#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <MQTTClient.h>

// Token Bucket Rate Limiter
class TokenBucketRateLimiter {
private:
    double tokens;
    double max_tokens;
    double refill_rate; // tokens per second
    std::chrono::steady_clock::time_point last_refill;
    std::mutex mtx;

public:
    TokenBucketRateLimiter(double rate_per_sec, double burst_size)
        : tokens(burst_size),
          max_tokens(burst_size),
          refill_rate(rate_per_sec),
          last_refill(std::chrono::steady_clock::now()) {}

    bool try_consume(double count = 1.0) {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        
        if (tokens >= count) {
            tokens -= count;
            return true;
        }
        return false;
    }

    void refill() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_refill).count() / 1000.0;
        
        tokens = std::min(max_tokens, tokens + elapsed * refill_rate);
        last_refill = now;
    }

    double available_tokens() {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        return tokens;
    }
};

// Rate-Limited MQTT Publisher
class RateLimitedMQTTPublisher {
private:
    MQTTClient client;
    TokenBucketRateLimiter rate_limiter;
    std::atomic<uint64_t> messages_sent;
    std::atomic<uint64_t> messages_dropped;

public:
    RateLimitedMQTTPublisher(const char* broker, const char* client_id,
                             double rate_limit, double burst_size)
        : rate_limiter(rate_limit, burst_size),
          messages_sent(0),
          messages_dropped(0) {
        
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        
        MQTTClient_create(&client, broker, client_id,
                         MQTTCLIENT_PERSISTENCE_NONE, NULL);
        
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        
        int rc = MQTTClient_connect(client, &conn_opts);
        if (rc != MQTTCLIENT_SUCCESS) {
            throw std::runtime_error("Failed to connect to broker");
        }
        
        std::cout << "Connected to broker: " << broker << std::endl;
        std::cout << "Rate limit: " << rate_limit << " msg/sec, Burst: " 
                  << burst_size << std::endl;
    }

    ~RateLimitedMQTTPublisher() {
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
    }

    bool publish(const char* topic, const char* payload, int qos = 0) {
        if (!rate_limiter.try_consume(1.0)) {
            messages_dropped++;
            std::cerr << "Rate limit exceeded. Message dropped. "
                     << "Dropped: " << messages_dropped << std::endl;
            return false;
        }

        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = (void*)payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = qos;
        pubmsg.retained = 0;

        MQTTClient_deliveryToken token;
        int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
        
        if (rc == MQTTCLIENT_SUCCESS) {
            messages_sent++;
            return true;
        }
        
        return false;
    }

    void print_stats() {
        std::cout << "\n=== Rate Limiter Statistics ===" << std::endl;
        std::cout << "Messages sent: " << messages_sent << std::endl;
        std::cout << "Messages dropped: " << messages_dropped << std::endl;
        std::cout << "Available tokens: " << rate_limiter.available_tokens() << std::endl;
    }
};

// Example usage
int main() {
    try {
        // Create publisher with 10 msg/sec rate limit and burst of 20
        RateLimitedMQTTPublisher publisher(
            "tcp://localhost:1883",
            "rate_limited_publisher",
            10.0,  // 10 messages per second
            20.0   // burst size of 20 messages
        );

        // Simulate message storm - try to send 100 messages rapidly
        std::cout << "\nSimulating message storm...\n" << std::endl;
        
        for (int i = 0; i < 100; i++) {
            char payload[64];
            snprintf(payload, sizeof(payload), "Message %d", i);
            
            bool sent = publisher.publish("sensors/temperature", payload, 0);
            
            if (sent && i % 10 == 0) {
                std::cout << "Sent message " << i << std::endl;
            }
            
            // Small delay to see rate limiting in action
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        publisher.print_stats();

        // Wait a bit for tokens to refill
        std::cout << "\nWaiting for token refill..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        publisher.print_stats();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}