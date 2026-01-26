# MQTT Rule Engines and Automation

## Overview

Rule engines and automation in MQTT enable broker-side logic that automatically processes, routes, transforms, or triggers actions based on incoming messages. Instead of requiring external applications to subscribe and process every message, the broker itself can evaluate conditions and execute predefined actions, reducing latency and system complexity.

## Key Concepts

**Rule Engine Components:**
- **Triggers**: Events that initiate rule evaluation (message arrival, time-based, state changes)
- **Conditions**: Logical expressions that must be satisfied for actions to execute
- **Actions**: Operations performed when conditions are met (republish, transform, store, HTTP call, database write)
- **Context**: Shared state and variables accessible across rules

**Common Use Cases:**
- Message routing based on payload content
- Data transformation and enrichment
- Threshold-based alerting
- Protocol bridging (MQTT to HTTP/WebSocket)
- Time-based automation
- Data aggregation and filtering

## Architecture Patterns

Rule engines typically operate in one of two modes:

1. **Embedded Mode**: Rules execute within the broker process (low latency, limited flexibility)
2. **External Mode**: Rules run in separate services (scalable, complex deployment)

## C/C++ Implementation

Here's an example using a custom rule engine with the Mosquitto broker:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <mosquitto_broker.h>
#include <mosquitto_plugin.h>
#include <jansson.h>

// Rule structure
typedef struct {
    char *topic_pattern;
    char *condition;  // JSON path expression
    double threshold;
    char *action_topic;
    char *action_payload;
} mqtt_rule_t;

// Rule engine context
typedef struct {
    mqtt_rule_t *rules;
    int rule_count;
    struct mosquitto *mosq;
} rule_engine_ctx_t;

// Evaluate JSON condition
int evaluate_condition(const char *payload, const char *json_path, double threshold) {
    json_error_t error;
    json_t *root = json_loads(payload, 0, &error);
    
    if (!root) {
        fprintf(stderr, "JSON parse error: %s\n", error.text);
        return 0;
    }
    
    json_t *value = json_object_get(root, json_path);
    if (!json_is_number(value)) {
        json_decref(root);
        return 0;
    }
    
    double num_value = json_number_value(value);
    int result = num_value > threshold;
    
    json_decref(root);
    return result;
}

// Message callback with rule processing
void on_message(struct mosquitto *mosq, void *userdata, 
                const struct mosquitto_message *msg) {
    rule_engine_ctx_t *ctx = (rule_engine_ctx_t *)userdata;
    
    // Iterate through rules
    for (int i = 0; i < ctx->rule_count; i++) {
        mqtt_rule_t *rule = &ctx->rules[i];
        
        // Check topic match
        bool topic_match = false;
        mosquitto_topic_matches_sub(rule->topic_pattern, msg->topic, &topic_match);
        
        if (!topic_match) continue;
        
        // Evaluate condition
        if (evaluate_condition((char *)msg->payload, rule->condition, rule->threshold)) {
            // Execute action: republish to action topic
            printf("Rule triggered: %s -> %s\n", msg->topic, rule->action_topic);
            
            // Transform payload if needed
            char action_payload[512];
            snprintf(action_payload, sizeof(action_payload), 
                    "{\"original_topic\":\"%s\",\"alert\":\"%s\"}", 
                    msg->topic, rule->action_payload);
            
            mosquitto_publish(mosq, NULL, rule->action_topic, 
                            strlen(action_payload), action_payload, 
                            1, false);
        }
    }
}

// Initialize rule engine
rule_engine_ctx_t* init_rule_engine(struct mosquitto *mosq) {
    rule_engine_ctx_t *ctx = malloc(sizeof(rule_engine_ctx_t));
    ctx->mosq = mosq;
    ctx->rule_count = 2;
    ctx->rules = malloc(sizeof(mqtt_rule_t) * ctx->rule_count);
    
    // Rule 1: Temperature threshold alert
    ctx->rules[0].topic_pattern = strdup("sensors/+/temperature");
    ctx->rules[0].condition = strdup("value");
    ctx->rules[0].threshold = 30.0;
    ctx->rules[0].action_topic = strdup("alerts/temperature/high");
    ctx->rules[0].action_payload = strdup("Temperature exceeded threshold");
    
    // Rule 2: Battery low warning
    ctx->rules[1].topic_pattern = strdup("devices/+/battery");
    ctx->rules[1].condition = strdup("level");
    ctx->rules[1].threshold = -80.0;  // Negative for less-than check
    ctx->rules[1].action_topic = strdup("alerts/battery/low");
    ctx->rules[1].action_payload = strdup("Battery level critical");
    
    return ctx;
}

// Main application
int main(int argc, char *argv[]) {
    struct mosquitto *mosq;
    rule_engine_ctx_t *ctx;
    
    mosquitto_lib_init();
    
    mosq = mosquitto_new("rule_engine", true, NULL);
    if (!mosq) {
        fprintf(stderr, "Failed to create mosquitto instance\n");
        return 1;
    }
    
    ctx = init_rule_engine(mosq);
    mosquitto_user_data_set(mosq, ctx);
    mosquitto_message_callback_set(mosq, on_message);
    
    if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect to broker\n");
        return 1;
    }
    
    // Subscribe to all sensor topics
    mosquitto_subscribe(mosq, NULL, "sensors/#", 0);
    mosquitto_subscribe(mosq, NULL, "devices/#", 0);
    
    printf("Rule engine started\n");
    mosquitto_loop_forever(mosq, -1, 1);
    
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return 0;
}
```

## Rust Implementation

Here's a comprehensive Rust implementation with a flexible rule engine:

```rust
use rumqttc::{AsyncClient, Event, MqttOptions, Packet, QoS};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tokio::sync::mpsc;
use regex::Regex;
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
struct Rule {
    id: String,
    topic_pattern: String,
    #[serde(default)]
    conditions: Vec<Condition>,
    actions: Vec<Action>,
    #[serde(default)]
    enabled: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum Condition {
    JsonPath { path: String, operator: String, value: Value },
    TopicMatch { pattern: String },
    Payload { contains: String },
    TimeRange { start: String, end: String },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum Action {
    Publish { topic: String, payload: String, qos: u8 },
    Transform { template: String, output_topic: String },
    Log { message: String },
    HttpPost { url: String, body_template: String },
    Store { database: String, collection: String },
}

struct RuleEngine {
    rules: Vec<Rule>,
    client: AsyncClient,
    context: HashMap<String, Value>,
}

impl RuleEngine {
    fn new(client: AsyncClient, rules: Vec<Rule>) -> Self {
        Self {
            rules,
            client,
            context: HashMap::new(),
        }
    }

    async fn process_message(&mut self, topic: &str, payload: &[u8]) {
        let payload_str = String::from_utf8_lossy(payload);
        
        for rule in &self.rules {
            if !rule.enabled {
                continue;
            }

            // Check if topic matches
            if !self.topic_matches(&rule.topic_pattern, topic) {
                continue;
            }

            // Evaluate all conditions
            let mut all_conditions_met = true;
            for condition in &rule.conditions {
                if !self.evaluate_condition(condition, topic, &payload_str).await {
                    all_conditions_met = false;
                    break;
                }
            }

            if all_conditions_met {
                println!("Rule '{}' triggered for topic: {}", rule.id, topic);
                self.execute_actions(&rule.actions, topic, &payload_str).await;
            }
        }
    }

    fn topic_matches(&self, pattern: &str, topic: &str) -> bool {
        // Convert MQTT wildcard to regex
        let regex_pattern = pattern
            .replace("+", "[^/]+")
            .replace("#", ".*");
        
        if let Ok(re) = Regex::new(&format!("^{}$", regex_pattern)) {
            re.is_match(topic)
        } else {
            false
        }
    }

    async fn evaluate_condition(
        &self,
        condition: &Condition,
        topic: &str,
        payload: &str,
    ) -> bool {
        match condition {
            Condition::JsonPath { path, operator, value } => {
                if let Ok(json) = serde_json::from_str::<Value>(payload) {
                    if let Some(extracted) = self.extract_json_path(&json, path) {
                        return self.compare_values(&extracted, operator, value);
                    }
                }
                false
            }
            Condition::TopicMatch { pattern } => {
                self.topic_matches(pattern, topic)
            }
            Condition::Payload { contains } => {
                payload.contains(contains)
            }
            Condition::TimeRange { start, end } => {
                // Simplified time check
                let now = chrono::Local::now().format("%H:%M").to_string();
                now >= *start && now <= *end
            }
        }
    }

    fn extract_json_path(&self, json: &Value, path: &str) -> Option<Value> {
        let parts: Vec<&str> = path.split('.').collect();
        let mut current = json;

        for part in parts {
            current = current.get(part)?;
        }

        Some(current.clone())
    }

    fn compare_values(&self, left: &Value, operator: &str, right: &Value) -> bool {
        match operator {
            ">" => {
                if let (Some(l), Some(r)) = (left.as_f64(), right.as_f64()) {
                    return l > r;
                }
                false
            }
            "<" => {
                if let (Some(l), Some(r)) = (left.as_f64(), right.as_f64()) {
                    return l < r;
                }
                false
            }
            "==" => left == right,
            "!=" => left != right,
            ">=" => {
                if let (Some(l), Some(r)) = (left.as_f64(), right.as_f64()) {
                    return l >= r;
                }
                false
            }
            "<=" => {
                if let (Some(l), Some(r)) = (left.as_f64(), right.as_f64()) {
                    return l <= r;
                }
                false
            }
            _ => false,
        }
    }

    async fn execute_actions(&mut self, actions: &[Action], topic: &str, payload: &str) {
        for action in actions {
            match action {
                Action::Publish { topic: pub_topic, payload: pub_payload, qos } => {
                    let rendered_payload = self.render_template(pub_payload, topic, payload);
                    let qos = match qos {
                        0 => QoS::AtMostOnce,
                        1 => QoS::AtLeastOnce,
                        _ => QoS::ExactlyOnce,
                    };
                    
                    if let Err(e) = self.client.publish(
                        pub_topic,
                        qos,
                        false,
                        rendered_payload.as_bytes(),
                    ).await {
                        eprintln!("Publish error: {}", e);
                    }
                }
                Action::Transform { template, output_topic } => {
                    let transformed = self.render_template(template, topic, payload);
                    if let Err(e) = self.client.publish(
                        output_topic,
                        QoS::AtLeastOnce,
                        false,
                        transformed.as_bytes(),
                    ).await {
                        eprintln!("Transform publish error: {}", e);
                    }
                }
                Action::Log { message } => {
                    let rendered = self.render_template(message, topic, payload);
                    println!("[RULE LOG] {}", rendered);
                }
                Action::HttpPost { url, body_template } => {
                    let body = self.render_template(body_template, topic, payload);
                    // HTTP post implementation would go here
                    println!("HTTP POST to {}: {}", url, body);
                }
                Action::Store { database, collection } => {
                    println!("Store to {}/{}: {}", database, collection, payload);
                }
            }
        }
    }

    fn render_template(&self, template: &str, topic: &str, payload: &str) -> String {
        template
            .replace("{{topic}}", topic)
            .replace("{{payload}}", payload)
            .replace("{{timestamp}}", &chrono::Utc::now().to_rfc3339())
    }
}

#[tokio::main]
async fn main() {
    // Define rules
    let rules = vec![
        Rule {
            id: "temp_alert".to_string(),
            topic_pattern: "sensors/+/temperature".to_string(),
            conditions: vec![
                Condition::JsonPath {
                    path: "value".to_string(),
                    operator: ">".to_string(),
                    value: Value::from(30.0),
                }
            ],
            actions: vec![
                Action::Publish {
                    topic: "alerts/temperature/high".to_string(),
                    payload: r#"{"alert":"High temperature","topic":"{{topic}}","time":"{{timestamp}}"}"#.to_string(),
                    qos: 1,
                },
                Action::Log {
                    message: "Temperature alert triggered on {{topic}}".to_string(),
                }
            ],
            enabled: true,
        },
        Rule {
            id: "humidity_transform".to_string(),
            topic_pattern: "sensors/+/humidity".to_string(),
            conditions: vec![],
            actions: vec![
                Action::Transform {
                    template: r#"{"sensor":"{{topic}}","data":{{payload}},"processed":"{{timestamp}}"}"#.to_string(),
                    output_topic: "processed/humidity".to_string(),
                }
            ],
            enabled: true,
        }
    ];

    // Setup MQTT client
    let mut mqttoptions = MqttOptions::new("rule_engine", "localhost", 1883);
    mqttoptions.set_keep_alive(std::time::Duration::from_secs(30));

    let (client, mut eventloop) = AsyncClient::new(mqttoptions, 10);
    
    // Subscribe to topics
    client.subscribe("sensors/#", QoS::AtMostOnce).await.unwrap();
    
    let mut engine = RuleEngine::new(client.clone(), rules);

    println!("Rule engine started");

    // Process events
    while let Ok(notification) = eventloop.poll().await {
        if let Event::Incoming(Packet::Publish(publish)) = notification {
            engine.process_message(&publish.topic, &publish.payload).await;
        }
    }
}
```

## Summary

**Rule Engines and Automation** in MQTT provide broker-side intelligence for:

- **Automated Message Routing**: Direct messages based on content, not just topics
- **Real-time Processing**: Evaluate conditions and trigger actions with minimal latency
- **Data Transformation**: Convert formats, enrich data, aggregate values
- **Integration**: Bridge MQTT with HTTP, databases, and other protocols
- **Alert Generation**: Threshold-based notifications without external monitoring

**Key Benefits:**
- Reduced network traffic (filtering at source)
- Lower latency (no external round-trip)
- Simplified architecture (centralized logic)
- Consistent behavior across clients

**Implementation Considerations:**
- Rule complexity affects broker performance
- State management for stateful rules
- Rule conflicts and priority handling
- Security and access control for rule modification
- Testing and debugging strategies

Modern MQTT brokers like EMQ X and HiveMQ offer built-in rule engines with SQL-like query languages, while custom implementations provide maximum flexibility for specialized use cases.