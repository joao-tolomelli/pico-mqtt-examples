#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"

// Wi-Fi Configuration
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// MQTT Configuration
#define MQTT_BROKER "broker.emqx.io"
#define MQTT_BROKER_PORT 1883
#define MQTT_TOPIC "embedded/status"

// Button Configuration
#define BUTTON_PIN 5

// Global Variables
static mqtt_client_t *mqttClient;
static ip_addr_t brokerIp;
static bool isMqttConnected = false;

// Function Prototypes
static void onMqttConnection(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
void publishStatus(bool isButtonPressed, float temperatureC);
float readInternalTemperature();
void onDnsResolved(const char *name, const ip_addr_t *ipaddr, void *callback_arg);

// Main Function
int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("\n=== Starting MQTT Button + Temperature ===\n");

    // Initialize Wi-Fi
    if (cyw43_arch_init()) {
        printf("Wi-Fi initialization error\n");
        return -1;
    }
    cyw43_arch_enable_sta_mode();

    printf("[Wi-Fi] Connecting...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("[Wi-Fi] Failed to connect to Wi-Fi\n");
        return -1;
    } else {
        printf("[Wi-Fi] Connected successfully!\n");
    }

    // Configure button GPIO
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN); // Pull-up enabled

    // Initialize ADC for internal temperature sensor
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    // Initialize MQTT client
    mqttClient = mqtt_client_new();

    // Resolve MQTT broker DNS
    err_t err = dns_gethostbyname(MQTT_BROKER, &brokerIp, onDnsResolved, NULL);
    if (err == ERR_OK) {
        onDnsResolved(MQTT_BROKER, &brokerIp, NULL);
    } else if (err == ERR_INPROGRESS) {
        printf("[DNS] Resolving...\n");
    } else {
        printf("[DNS] DNS resolution error: %d\n", err);
        return -1;
    }

    // Main loop
    while (true) {
        // Handle network tasks
        cyw43_arch_poll();

        // Read button state
        bool isButtonPressed = !gpio_get(BUTTON_PIN); // Inverted due to pull-up

        // Read temperature
        float temperatureC = readInternalTemperature();
        printf("[TEMP] Current temperature: %.2f °C\n", temperatureC);

        // Publish status
        publishStatus(isButtonPressed, temperatureC);

        // Wait 1 second
        sleep_ms(1000);
    }

    cyw43_arch_deinit();
    return 0;
}

// MQTT connection callback
static void onMqttConnection(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("[MQTT] Connected to broker!\n");
        isMqttConnected = true;
    } else {
        printf("[MQTT] MQTT connection failed. Code: %d\n", status);
        isMqttConnected = false;
    }
}

// Publish button and temperature status
void publishStatus(bool isButtonPressed, float temperatureC) {
    if (!isMqttConnected) {
        printf("[MQTT] Not connected, skipping publish\n");
        return;
    }

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"button\":\"%s\",\"temperature\":%.2f}",
             isButtonPressed ? "ON" : "OFF",
             temperatureC);

    printf("[MQTT] Publishing: topic='%s', message='%s'\n", MQTT_TOPIC, payload);

    err_t err = mqtt_publish(mqttClient, MQTT_TOPIC, payload, strlen(payload), 0, 0, NULL, NULL);
    if (err == ERR_OK) {
        printf("[MQTT] Publish successful\n");
    } else {
        printf("[MQTT] Publish error: %d\n", err);
    }
}

// Read internal temperature
float readInternalTemperature() {
    uint16_t raw = adc_read();
    const float conversionFactor = 3.3f / (1 << 12);
    float voltage = raw * conversionFactor;
    float temperatureC = 27.0f - (voltage - 0.706f) / 0.001721f;
    return temperatureC;
}

// DNS resolution callback
void onDnsResolved(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr != NULL) {
        brokerIp = *ipaddr;
        printf("[DNS] Resolved: %s -> %s\n", name, ipaddr_ntoa(ipaddr));

        struct mqtt_connect_client_info_t clientInfo = {
            .client_id = "pico-client",
            .keep_alive = 60,
            .client_user = NULL,
            .client_pass = NULL,
            .will_topic = NULL,
            .will_msg = NULL,
            .will_qos = 0,
            .will_retain = 0
        };

        printf("[MQTT] Connecting to broker...\n");
        mqtt_client_connect(mqttClient, &brokerIp, MQTT_BROKER_PORT, onMqttConnection, NULL, &clientInfo);
    } else {
        printf("[DNS] Failed to resolve DNS for %s\n", name);
    }
}
