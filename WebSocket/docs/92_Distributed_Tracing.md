# Distributed Tracing with WebSockets

## Overview

Distributed tracing is a method for tracking requests as they flow through distributed systems. When applied to WebSocket connections, it enables observability across long-lived, bidirectional communication channels. OpenTelemetry provides a vendor-neutral standard for instrumenting, generating, collecting, and exporting telemetry data including traces, metrics, and logs.

## Core Concepts

**Traces** represent the complete journey of a request through a system. Each trace consists of one or more **spans**, which are named, timed operations. Spans can have parent-child relationships, forming a tree structure that visualizes the request flow.

For WebSockets, key trace points include:
- Connection establishment (handshake)
- Message send/receive operations
- Connection closure
- Error handling

**Context propagation** is critical - trace context must be passed through the WebSocket connection to correlate client and server operations into a single distributed trace.

## OpenTelemetry Integration

OpenTelemetry provides APIs for creating spans, adding attributes, recording events, and propagating context. The typical pattern involves:

1. Starting a span when an operation begins
2. Adding relevant attributes (connection ID, message type, etc.)
3. Recording events or errors
4. Ending the span when the operation completes

## C/C++ Implementation

```c
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>

namespace trace = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace otlp = opentelemetry::exporter::otlp;

// Initialize OpenTelemetry tracer
void init_tracer() {
    auto exporter = std::unique_ptr<trace_sdk::SpanExporter>(
        new otlp::OtlpHttpExporter()
    );
    
    auto processor = std::unique_ptr<trace_sdk::SpanProcessor>(
        new trace_sdk::SimpleSpanProcessor(std::move(exporter))
    );
    
    auto provider = std::shared_ptr<trace::TracerProvider>(
        new trace_sdk::TracerProvider(std::move(processor))
    );
    
    trace::Provider::SetTracerProvider(provider);
}

// WebSocket connection handler with tracing
class TracedWebSocketConnection {
private:
    trace::Tracer* tracer_;
    std::string connection_id_;
    
public:
    TracedWebSocketConnection(const std::string& conn_id) 
        : connection_id_(conn_id) {
        auto provider = trace::Provider::GetTracerProvider();
        tracer_ = provider->GetTracer("websocket-server", "1.0.0").get();
    }
    
    // Trace WebSocket handshake
    void handle_handshake(const std::string& client_ip) {
        auto span = tracer_->StartSpan("websocket.handshake");
        auto scope = tracer_->WithActiveSpan(span);
        
        span->SetAttribute("connection.id", connection_id_);
        span->SetAttribute("client.ip", client_ip);
        span->SetAttribute("protocol", "websocket");
        
        try {
            // Perform handshake logic
            perform_handshake();
            span->SetStatus(trace::StatusCode::kOk);
        } catch (const std::exception& e) {
            span->SetStatus(trace::StatusCode::kError, e.what());
            span->AddEvent("handshake.failed", {
                {"error.message", e.what()}
            });
        }
        
        span->End();
    }
    
    // Trace message receiving
    void receive_message(const std::string& message) {
        auto span = tracer_->StartSpan("websocket.receive");
        auto scope = tracer_->WithActiveSpan(span);
        
        span->SetAttribute("connection.id", connection_id_);
        span->SetAttribute("message.size", static_cast<int>(message.size()));
        span->SetAttribute("message.type", get_message_type(message));
        
        // Extract parent context from message headers if present
        auto context = extract_context_from_message(message);
        
        try {
            process_message(message);
            span->SetAttribute("processing.success", true);
        } catch (const std::exception& e) {
            span->SetStatus(trace::StatusCode::kError);
            span->RecordError(e);
        }
        
        span->End();
    }
    
    // Trace message sending
    void send_message(const std::string& message) {
        auto span = tracer_->StartSpan("websocket.send");
        auto scope = tracer_->WithActiveSpan(span);
        
        span->SetAttribute("connection.id", connection_id_);
        span->SetAttribute("message.size", static_cast<int>(message.size()));
        
        // Inject trace context into message
        auto enriched_message = inject_context_into_message(message);
        
        try {
            transmit_message(enriched_message);
            span->SetStatus(trace::StatusCode::kOk);
        } catch (const std::exception& e) {
            span->SetStatus(trace::StatusCode::kError);
            span->AddEvent("send.failed");
        }
        
        span->End();
    }
    
private:
    void perform_handshake() { /* handshake implementation */ }
    void process_message(const std::string& msg) { /* process implementation */ }
    void transmit_message(const std::string& msg) { /* send implementation */ }
    std::string get_message_type(const std::string& msg) { return "text"; }
    
    std::string inject_context_into_message(const std::string& msg) {
        // Inject current span context into message metadata
        return msg; // Simplified
    }
    
    opentelemetry::context::Context extract_context_from_message(
        const std::string& msg) {
        // Extract parent span context from message metadata
        return opentelemetry::context::Context();
    }
};

// Example usage
int main() {
    init_tracer();
    
    TracedWebSocketConnection conn("conn-12345");
    conn.handle_handshake("192.168.1.100");
    conn.receive_message("{\"type\":\"chat\",\"text\":\"Hello\"}");
    conn.send_message("{\"type\":\"ack\",\"status\":\"received\"}");
    
    return 0;
}
```

## Rust Implementation

```rust
use opentelemetry::{
    global,
    trace::{Span, SpanKind, Status, Tracer, TracerProvider},
    KeyValue,
};
use opentelemetry_sdk::{
    trace::{self, RandomIdGenerator, Sampler},
    Resource,
};
use opentelemetry_otlp::WithExportConfig;
use std::collections::HashMap;

// Initialize OpenTelemetry tracer
fn init_tracer() -> Result<trace::Tracer, Box<dyn std::error::Error>> {
    let exporter = opentelemetry_otlp::new_exporter()
        .tonic()
        .with_endpoint("http://localhost:4317");
    
    let tracer_provider = opentelemetry_otlp::new_pipeline()
        .tracing()
        .with_exporter(exporter)
        .with_trace_config(
            trace::config()
                .with_sampler(Sampler::AlwaysOn)
                .with_id_generator(RandomIdGenerator::default())
                .with_resource(Resource::new(vec![
                    KeyValue::new("service.name", "websocket-server"),
                    KeyValue::new("service.version", "1.0.0"),
                ])),
        )
        .install_batch(opentelemetry_sdk::runtime::Tokio)?;
    
    global::set_tracer_provider(tracer_provider.clone());
    
    Ok(tracer_provider.tracer("websocket-server"))
}

// WebSocket connection with tracing
struct TracedWebSocketConnection {
    tracer: trace::Tracer,
    connection_id: String,
}

impl TracedWebSocketConnection {
    fn new(connection_id: String) -> Self {
        let tracer = global::tracer("websocket-server");
        Self {
            tracer,
            connection_id,
        }
    }
    
    // Trace WebSocket handshake
    async fn handle_handshake(&self, client_ip: &str) -> Result<(), Box<dyn std::error::Error>> {
        let mut span = self.tracer
            .span_builder("websocket.handshake")
            .with_kind(SpanKind::Server)
            .start(&self.tracer);
        
        span.set_attribute(KeyValue::new("connection.id", self.connection_id.clone()));
        span.set_attribute(KeyValue::new("client.ip", client_ip.to_string()));
        span.set_attribute(KeyValue::new("protocol", "websocket"));
        
        match self.perform_handshake().await {
            Ok(_) => {
                span.set_status(Status::Ok);
            }
            Err(e) => {
                span.set_status(Status::error(e.to_string()));
                span.add_event(
                    "handshake.failed",
                    vec![KeyValue::new("error.message", e.to_string())],
                );
                return Err(e);
            }
        }
        
        span.end();
        Ok(())
    }
    
    // Trace message receiving
    async fn receive_message(&self, message: &str) -> Result<(), Box<dyn std::error::Error>> {
        let mut span = self.tracer
            .span_builder("websocket.receive")
            .with_kind(SpanKind::Server)
            .start(&self.tracer);
        
        span.set_attribute(KeyValue::new("connection.id", self.connection_id.clone()));
        span.set_attribute(KeyValue::new("message.size", message.len() as i64));
        span.set_attribute(KeyValue::new("message.type", self.get_message_type(message)));
        
        // Extract parent context from message if present
        let _context = self.extract_context_from_message(message);
        
        match self.process_message(message).await {
            Ok(_) => {
                span.set_attribute(KeyValue::new("processing.success", true));
                span.set_status(Status::Ok);
            }
            Err(e) => {
                span.set_status(Status::error(e.to_string()));
                span.record_error(&*e);
            }
        }
        
        span.end();
        Ok(())
    }
    
    // Trace message sending with context propagation
    async fn send_message(&self, message: &str) -> Result<(), Box<dyn std::error::Error>> {
        let mut span = self.tracer
            .span_builder("websocket.send")
            .with_kind(SpanKind::Client)
            .start(&self.tracer);
        
        span.set_attribute(KeyValue::new("connection.id", self.connection_id.clone()));
        span.set_attribute(KeyValue::new("message.size", message.len() as i64));
        
        // Inject trace context into message
        let enriched_message = self.inject_context_into_message(message);
        
        match self.transmit_message(&enriched_message).await {
            Ok(_) => {
                span.set_status(Status::Ok);
            }
            Err(e) => {
                span.set_status(Status::error(e.to_string()));
                span.add_event("send.failed", vec![]);
                return Err(e);
            }
        }
        
        span.end();
        Ok(())
    }
    
    // Helper methods
    async fn perform_handshake(&self) -> Result<(), Box<dyn std::error::Error>> {
        // Handshake implementation
        Ok(())
    }
    
    async fn process_message(&self, _message: &str) -> Result<(), Box<dyn std::error::Error>> {
        // Message processing implementation
        Ok(())
    }
    
    async fn transmit_message(&self, _message: &str) -> Result<(), Box<dyn std::error::Error>> {
        // Message transmission implementation
        Ok(())
    }
    
    fn get_message_type(&self, _message: &str) -> String {
        "text".to_string()
    }
    
    fn inject_context_into_message(&self, message: &str) -> String {
        // In a real implementation, inject trace context into message headers
        // using opentelemetry::global::get_text_map_propagator()
        message.to_string()
    }
    
    fn extract_context_from_message(&self, _message: &str) -> HashMap<String, String> {
        // In a real implementation, extract trace context from message headers
        HashMap::new()
    }
}

// Example usage
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let _tracer = init_tracer()?;
    
    let conn = TracedWebSocketConnection::new("conn-12345".to_string());
    
    conn.handle_handshake("192.168.1.100").await?;
    conn.receive_message(r#"{"type":"chat","text":"Hello"}"#).await?;
    conn.send_message(r#"{"type":"ack","status":"received"}"#).await?;
    
    // Ensure all spans are exported before shutdown
    global::shutdown_tracer_provider();
    
    Ok(())
}
```

## Key Implementation Considerations

**Context Propagation**: Trace context must be transmitted through WebSocket messages. Common approaches include:
- Adding trace headers to the first message after connection
- Embedding trace context in a message envelope/metadata field
- Using W3C Trace Context format for interoperability

**Span Lifecycles**: WebSocket spans can be short-lived (per-message) or long-lived (per-connection). Choose based on your observability needs.

**Sampling**: For high-throughput WebSocket applications, implement sampling strategies to reduce overhead while maintaining visibility.

**Attributes**: Include relevant attributes like connection ID, message type, payload size, error codes, and latency metrics.

## Summary

Distributed tracing for WebSockets with OpenTelemetry provides end-to-end visibility into bidirectional, persistent connections. By instrumenting handshakes, message flows, and errors with spans, you can diagnose latency issues, track request paths across services, and understand system behavior. The key challenges involve context propagation through WebSocket messages and managing span lifecycles for long-lived connections. Both C/C++ and Rust have robust OpenTelemetry implementations that support these patterns, enabling production-grade observability for WebSocket-based applications.