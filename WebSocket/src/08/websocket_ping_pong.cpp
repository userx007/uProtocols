#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    net::steady_timer ping_timer_;
    net::steady_timer pong_timeout_timer_;
    
    static constexpr std::chrono::seconds PING_INTERVAL{30};
    static constexpr std::chrono::seconds PONG_TIMEOUT{10};
    
    bool waiting_for_pong_ = false;
    std::chrono::steady_clock::time_point last_pong_time_;

public:
    explicit WebSocketSession(tcp::socket&& socket)
        : ws_(std::move(socket))
        , ping_timer_(ws_.get_executor())
        , pong_timeout_timer_(ws_.get_executor())
    {
        last_pong_time_ = std::chrono::steady_clock::now();
    }

    void run() {
        // Set control callback for ping/pong handling
        ws_.control_callback(
            [this](websocket::frame_type kind, beast::string_view payload) {
                on_control_frame(kind, payload);
            }
        );
        
        // Accept the WebSocket handshake
        ws_.async_accept(
            beast::bind_front_handler(
                &WebSocketSession::on_accept,
                shared_from_this()
            )
        );
    }

private:
    void on_control_frame(websocket::frame_type kind, beast::string_view payload) {
        boost::ignore_unused(payload);
        
        if (kind == websocket::frame_type::pong) {
            std::cout << "Received pong frame\n";
            last_pong_time_ = std::chrono::steady_clock::now();
            waiting_for_pong_ = false;
            pong_timeout_timer_.cancel();
        } else if (kind == websocket::frame_type::ping) {
            std::cout << "Received ping frame (auto-responding with pong)\n";
            // Beast automatically responds with pong
        }
    }

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "Accept error: " << ec.message() << "\n";
            return;
        }
        
        std::cout << "WebSocket connection accepted\n";
        
        // Start the ping mechanism
        schedule_ping();
        
        // Start reading messages
        do_read();
    }

    void do_read() {
        ws_.async_read(
            buffer_,
            beast::bind_front_handler(
                &WebSocketSession::on_read,
                shared_from_this()
            )
        );
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        if (ec == websocket::error::closed) {
            std::cout << "Connection closed normally\n";
            return;
        }
        
        if (ec) {
            std::cerr << "Read error: " << ec.message() << "\n";
            return;
        }
        
        // Echo the message back
        std::cout << "Received message: " 
                  << beast::make_printable(buffer_.data()) << "\n";
        
        ws_.text(ws_.got_text());
        ws_.async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &WebSocketSession::on_write,
                shared_from_this()
            )
        );
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        if (ec) {
            std::cerr << "Write error: " << ec.message() << "\n";
            return;
        }
        
        // Clear the buffer
        buffer_.consume(buffer_.size());
        
        // Continue reading
        do_read();
    }

    void schedule_ping() {
        ping_timer_.expires_after(PING_INTERVAL);
        ping_timer_.async_wait(
            beast::bind_front_handler(
                &WebSocketSession::on_ping_timer,
                shared_from_this()
            )
        );
    }

    void on_ping_timer(beast::error_code ec) {
        if (ec && ec != net::error::operation_aborted) {
            std::cerr << "Ping timer error: " << ec.message() << "\n";
            return;
        }
        
        if (ec == net::error::operation_aborted) {
            return; // Timer was cancelled
        }
        
        // Check if we're still waiting for a pong
        if (waiting_for_pong_) {
            auto elapsed = std::chrono::steady_clock::now() - last_pong_time_;
            if (elapsed > PONG_TIMEOUT) {
                std::cerr << "Pong timeout - closing connection\n";
                ws_.async_close(
                    websocket::close_code::normal,
                    [self = shared_from_this()](beast::error_code) {}
                );
                return;
            }
        }
        
        // Send ping frame
        std::cout << "Sending ping frame\n";
        waiting_for_pong_ = true;
        
        ws_.async_ping(
            websocket::ping_data{},
            beast::bind_front_handler(
                &WebSocketSession::on_ping_sent,
                shared_from_this()
            )
        );
        
        // Schedule pong timeout check
        pong_timeout_timer_.expires_after(PONG_TIMEOUT);
        pong_timeout_timer_.async_wait(
            beast::bind_front_handler(
                &WebSocketSession::on_pong_timeout,
                shared_from_this()
            )
        );
    }

    void on_ping_sent(beast::error_code ec) {
        if (ec) {
            std::cerr << "Ping send error: " << ec.message() << "\n";
            return;
        }
        
        // Schedule next ping
        schedule_ping();
    }

    void on_pong_timeout(beast::error_code ec) {
        if (ec == net::error::operation_aborted) {
            return; // Pong was received, timer was cancelled
        }
        
        if (waiting_for_pong_) {
            std::cerr << "Pong timeout reached - closing connection\n";
            ws_.async_close(
                websocket::close_code::normal,
                [self = shared_from_this()](beast::error_code) {}
            );
        }
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc))
    {
        beast::error_code ec;
        
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) throw beast::system_error{ec};
        
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) throw beast::system_error{ec};
        
        acceptor_.bind(endpoint, ec);
        if (ec) throw beast::system_error{ec};
        
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) throw beast::system_error{ec};
    }

    void run() {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &Listener::on_accept,
                shared_from_this()
            )
        );
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            std::cerr << "Accept error: " << ec.message() << "\n";
        } else {
            std::cout << "New connection accepted\n";
            std::make_shared<WebSocketSession>(std::move(socket))->run();
        }
        
        // Accept another connection
        do_accept();
    }
};

int main() {
    try {
        auto const address = net::ip::make_address("0.0.0.0");
        auto const port = static_cast<unsigned short>(8080);
        
        net::io_context ioc{1};
        
        std::make_shared<Listener>(ioc, tcp::endpoint{address, port})->run();
        
        std::cout << "WebSocket server listening on port " << port << "\n";
        
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}