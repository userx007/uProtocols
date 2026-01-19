#include <iostream>
#include <string>
#include <queue>
#include <memory>
#include <chrono>
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include <system_error>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>

class WebSocketException : public std::runtime_error {
public:
    enum class ErrorType {
        BROKEN_PIPE,
        CONNECTION_RESET,
        TIMEOUT,
        NETWORK_UNREACHABLE,
        DNS_FAILURE,
        PROTOCOL_ERROR,
        UNKNOWN
    };

    WebSocketException(ErrorType type, const std::string& msg)
        : std::runtime_error(msg), error_type_(type) {}

    ErrorType type() const { return error_type_; }
    
    bool is_recoverable() const {
        return error_type_ == ErrorType::BROKEN_PIPE ||
               error_type_ == ErrorType::CONNECTION_RESET ||
               error_type_ == ErrorType::TIMEOUT ||
               error_type_ == ErrorType::NETWORK_UNREACHABLE;
    }

private:
    ErrorType error_type_;
};

class ConnectionPolicy {
public:
    virtual ~ConnectionPolicy() = default;
    virtual int next_backoff_ms(int current_backoff, int retry_count) = 0;
    virtual bool should_retry(int retry_count) = 0;
};

class ExponentialBackoffPolicy : public ConnectionPolicy {
public:
    ExponentialBackoffPolicy(int initial_ms = 1000, int max_ms = 32000, 
                             int max_retries = 10)
        : initial_backoff_ms_(initial_ms)
        , max_backoff_ms_(max_ms)
        , max_retries_(max_retries) {}

    int next_backoff_ms(int current_backoff, int retry_count) override {
        if (retry_count == 0) return initial_backoff_ms_;
        int next = current_backoff * 2;
        return std::min(next, max_backoff_ms_);
    }

    bool should_retry(int retry_count) override {
        return retry_count < max_retries_;
    }

private:
    int initial_backoff_ms_;
    int max_backoff_ms_;
    int max_retries_;
};

class MessageBuffer {
public:
    struct Message {
        std::string data;
        std::chrono::steady_clock::time_point timestamp;
    };

    void push(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push({data, std::chrono::steady_clock::now()});
    }

    bool pop(Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) return false;
        msg = buffer_.front();
        buffer_.pop();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!buffer_.empty()) buffer_.pop();
    }

private:
    std::queue<Message> buffer_;
    mutable std::mutex mutex_;
};

class WebSocketConnection {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        ERROR,
        RECONNECTING
    };

    using ErrorCallback = std::function<void(const WebSocketException&)>;
    using StateCallback = std::function<void(State, State)>;

    WebSocketConnection(const std::string& host, int port,
                       std::unique_ptr<ConnectionPolicy> policy)
        : host_(host)
        , port_(port)
        , sockfd_(-1)
        , state_(State::DISCONNECTED)
        , retry_count_(0)
        , current_backoff_ms_(0)
        , policy_(std::move(policy))
        , should_run_(false) {
        signal(SIGPIPE, SIG_IGN);
    }

    ~WebSocketConnection() {
        disconnect();
    }

    void set_error_callback(ErrorCallback cb) { error_callback_ = cb; }
    void set_state_callback(StateCallback cb) { state_callback_ = cb; }

    bool connect() {
        change_state(State::CONNECTING);

        struct addrinfo hints{}, *result, *rp;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        std::string port_str = std::to_string(port_);
        if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result) != 0) {
            handle_error(WebSocketException::ErrorType::DNS_FAILURE,
                        "DNS resolution failed for " + host_);
            return false;
        }

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            sockfd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd_ < 0) continue;

            // Set timeouts
            struct timeval timeout{5, 0};
            setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            if (::connect(sockfd_, rp->ai_addr, rp->ai_addrlen) == 0) {
                break;
            }

            close(sockfd_);
            sockfd_ = -1;
        }

        freeaddrinfo(result);

        if (sockfd_ < 0) {
            handle_error(WebSocketException::ErrorType::CONNECTION_RESET,
                        "Failed to connect to " + host_ + ":" + std::to_string(port_));
            return false;
        }

        change_state(State::CONNECTED);
        retry_count_ = 0;
        current_backoff_ms_ = 0;
        last_activity_ = std::chrono::steady_clock::now();

        flush_message_buffer();
        return true;
    }

    void disconnect() {
        should_run_ = false;
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
        change_state(State::DISCONNECTED);
    }

    bool send(const std::string& data) {
        if (state_ != State::CONNECTED) {
            message_buffer_.push(data);
            std::cout << "Message buffered (state: " << static_cast<int>(state_) << ")\n";
            return false;
        }

        ssize_t sent = ::send(sockfd_, data.c_str(), data.size(), MSG_NOSIGNAL);

        if (sent < 0) {
            handle_send_error(errno, data);
            return false;
        }

        last_activity_ = std::chrono::steady_clock::now();
        return true;
    }

    std::string receive(size_t max_len = 4096) {
        if (state_ != State::CONNECTED) {
            return "";
        }

        std::vector<char> buffer(max_len);
        ssize_t received = ::recv(sockfd_, buffer.data(), max_len, 0);

        if (received < 0) {
            handle_recv_error(errno);
            return "";
        } else if (received == 0) {
            change_state(State::DISCONNECTED);
            return "";
        }

        last_activity_ = std::chrono::steady_clock::now();
        return std::string(buffer.data(), received);
    }

    bool reconnect() {
        if (!policy_->should_retry(retry_count_)) {
            std::cerr << "Max retries exceeded\n";
            return false;
        }

        change_state(State::RECONNECTING);

        current_backoff_ms_ = policy_->next_backoff_ms(current_backoff_ms_, retry_count_);
        
        std::cout << "Reconnecting in " << current_backoff_ms_ << "ms (attempt "
                  << retry_count_ + 1 << ")...\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(current_backoff_ms_));

        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }

        retry_count_++;

        if (connect()) {
            std::cout << "Reconnected successfully!\n";
            return true;
        }

        return false;
    }

    void run_with_recovery() {
        should_run_ = true;

        if (!connect()) {
            while (should_run_ && !reconnect()) {
                // Keep trying to reconnect
            }
        }

        while (should_run_) {
            if (state_ == State::ERROR || state_ == State::DISCONNECTED) {
                reconnect();
                continue;
            }

            if (state_ == State::CONNECTED) {
                check_connection_health();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    State get_state() const { return state_; }
    size_t buffered_messages() const { return message_buffer_.size(); }

private:
    void change_state(State new_state) {
        State old_state = state_;
        state_ = new_state;
        if (state_callback_) {
            state_callback_(old_state, new_state);
        }
    }

    void handle_error(WebSocketException::ErrorType type, const std::string& msg) {
        WebSocketException ex(type, msg);
        std::cerr << "Error: " << msg << "\n";
        
        if (error_callback_) {
            error_callback_(ex);
        }

        if (ex.is_recoverable()) {
            change_state(State::ERROR);
        }
    }

    void handle_send_error(int error_code, const std::string& data) {
        WebSocketException::ErrorType type;
        std::string msg;

        switch (error_code) {
            case EPIPE:
                type = WebSocketException::ErrorType::BROKEN_PIPE;
                msg = "Broken pipe - remote closed connection";
                message_buffer_.push(data);
                break;
            case ECONNRESET:
                type = WebSocketException::ErrorType::CONNECTION_RESET;
                msg = "Connection reset by peer";
                message_buffer_.push(data);
                break;
            case ETIMEDOUT:
                type = WebSocketException::ErrorType::TIMEOUT;
                msg = "Send timeout";
                message_buffer_.push(data);
                break;
            default:
                type = WebSocketException::ErrorType::UNKNOWN;
                msg = std::string("Send error: ") + strerror(error_code);
                break;
        }

        handle_error(type, msg);
    }

    void handle_recv_error(int error_code) {
        if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
            return; // Temporary, not fatal
        }

        WebSocketException::ErrorType type;
        std::string msg;

        switch (error_code) {
            case ECONNRESET:
                type = WebSocketException::ErrorType::CONNECTION_RESET;
                msg = "Connection reset during receive";
                break;
            case ETIMEDOUT:
                type = WebSocketException::ErrorType::TIMEOUT;
                msg = "Receive timeout";
                break;
            default:
                type = WebSocketException::ErrorType::UNKNOWN;
                msg = std::string("Receive error: ") + strerror(error_code);
                break;
        }

        handle_error(type, msg);
    }

    void flush_message_buffer() {
        MessageBuffer::Message msg;
        while (message_buffer_.pop(msg)) {
            std::cout << "Flushing buffered message\n";
            ::send(sockfd_, msg.data.c_str(), msg.data.size(), MSG_NOSIGNAL);
        }
    }

    void check_connection_health() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_activity_).count();

        if (elapsed > 60) {
            std::cout << "Connection idle for " << elapsed << "s, sending heartbeat\n";
            send("PING");
        }
    }

    std::string host_;
    int port_;
    int sockfd_;
    std::atomic<State> state_;
    int retry_count_;
    int current_backoff_ms_;
    std::unique_ptr<ConnectionPolicy> policy_;
    MessageBuffer message_buffer_;
    ErrorCallback error_callback_;
    StateCallback state_callback_;
    std::chrono::steady_clock::time_point last_activity_;
    std::atomic<bool> should_run_;
};

// Example usage
int main() {
    auto policy = std::make_unique<ExponentialBackoffPolicy>(1000, 32000, 10);
    WebSocketConnection conn("echo.websocket.org", 80, std::move(policy));

    conn.set_error_callback([](const WebSocketException& ex) {
        std::cerr << "Error occurred: " << ex.what() 
                  << " (recoverable: " << ex.is_recoverable() << ")\n";
    });

    conn.set_state_callback([](auto old_state, auto new_state) {
        std::cout << "State changed: " << static_cast<int>(old_state)
                  << " -> " << static_cast<int>(new_state) << "\n";
    });

    // Run in separate thread
    std::thread conn_thread([&conn]() {
        conn.run_with_recovery();
    });

    // Simulate sending messages
    for (int i = 0; i < 100; ++i) {
        std::string msg = "Message " + std::to_string(i);
        conn.send(msg);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    conn.disconnect();
    conn_thread.join();

    return 0;
}