// MQTT C/C++ Example using Eclipse Paho MQTT C Library
// Compile: gcc -o mqtt_example mqtt_example.c -lpaho-mqtt3c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

// Configuration constants
#define BROKER_ADDRESS "tcp://localhost:1883"
#define CLIENT_ID "CPP_Example_Client"
#define TOPIC "home/sensor/temperature"
#define QOS 1
#define TIMEOUT 10000L

// Callback when connection is lost
void connlost(void *context, char *cause) {
    printf("\nConnection lost\n");
    printf("Cause: %s\n", cause);
}

// Callback when message arrives
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    printf("Message arrived on topic: %s\n", topicName);
    printf("Message: %.*s\n", message->payloadlen, (char*)message->payload);
    printf("QoS: %d\n", message->qos);
    printf("Retained: %s\n\n", message->retained ? "yes" : "no");
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// Callback when delivery is complete
void delivered(void *context, MQTTClient_deliveryToken dt) {
    printf("Message delivery confirmed (token: %d)\n", dt);
}

// Publisher function
int mqtt_publish_example() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    // Create MQTT client
    MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    
    // Set Last Will and Testament
    MQTTClient_willOptions will_opts = MQTTClient_willOptions_initializer;
    will_opts.topicName = "home/sensor/status";
    will_opts.message = "offline";
    will_opts.retained = 1;
    will_opts.qos = 1;
    conn_opts.will = &will_opts;

    // Connect to broker
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }
    printf("Connected to broker successfully\n");

    // Publish messages
    for (int i = 0; i < 5; i++) {
        char payload[50];
        sprintf(payload, "Temperature: %.1f°C", 20.0 + i * 0.5);
        
        pubmsg.payload = payload;
        pubmsg.payloadlen = strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        
        MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        printf("Publishing: %s\n", payload);
        
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        printf("Message delivered (token: %d)\n\n", token);
        
        sleep(1);
    }

    // Disconnect
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}

// Subscriber function
int mqtt_subscribe_example() {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    // Create client
    MQTTClient_create(&client, BROKER_ADDRESS, "CPP_Subscriber",
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    // Connection options
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    // Connect
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    }
    printf("Subscriber connected to broker\n");

    // Subscribe to topic with wildcards
    printf("Subscribing to topic: home/sensor/#\n");
    MQTTClient_subscribe(client, "home/sensor/#", QOS);

    // Keep listening for messages
    printf("Waiting for messages...\n");
    for (int i = 0; i < 30; i++) {
        sleep(1);
    }

    // Unsubscribe and disconnect
    MQTTClient_unsubscribe(client, "home/sensor/#");
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return rc;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s [pub|sub]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "pub") == 0) {
        return mqtt_publish_example();
    } else if (strcmp(argv[1], "sub") == 0) {
        return mqtt_subscribe_example();
    } else {
        printf("Invalid argument. Use 'pub' or 'sub'\n");
        return 1;
    }
}