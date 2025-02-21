/*
 * Smart Parking System using ESP32, Ultrasonic Sensor, MQTT, ThingSpeak, and OM2M
 * 
 * This code detects whether a parking spot is occupied using an ultrasonic sensor.
 * It publishes the parking status to an MQTT broker, ThingSpeak, and an OM2M server.
 */

// Include required libraries
#include <WiFi.h>           // Library for WiFi connectivity
#include <PubSubClient.h>   // Library for MQTT communication
#include <ArduinoJson.h>    // Library for JSON formatting
#include <HTTPClient.h>     // Library for HTTP requests
#include <NewPing.h>        // Library for handling the ultrasonic sensor

// Wi-Fi credentials (Replace with actual credentials)
const char* ssid = "YOUR_WIFI_SSID";      // WiFi SSID
const char* password = "YOUR_WIFI_PASSWORD"; // WiFi Password

// MQTT Broker settings
const char* mqtt_broker = "broker.hivemq.com"; // Public MQTT broker
const char* mqtt_topic_status = "parking/spot/status"; // MQTT topic to publish status
const char* mqtt_topic_control = "parking/spot/control"; // MQTT topic to receive control messages
const int mqtt_port = 1883;  // Default MQTT port

// ThingSpeak settings
const char* thingspeak_server = "api.thingspeak.com"; // ThingSpeak API server
const char* thingspeak_api_key = "YOUR_THINGSPEAK_API_KEY"; // ThingSpeak API Key

// OM2M settings
const char* om2m_server = "http://YOUR_OM2M_SERVER:8080"; // OM2M server address
const char* om2m_app_id = "parking_management"; // OM2M Application ID
const char* om2m_container = "parking_spots"; // OM2M container for storing data
const char* om2m_ae = "parking_finder"; // OM2M Application Entity
const char* om2m_username = "admin"; // OM2M username
const char* om2m_password = "admin"; // OM2M password

// Ultrasonic sensor pins
#define TRIG_PIN 5 // Trigger pin for ultrasonic sensor
#define ECHO_PIN 18 // Echo pin for ultrasonic sensor
#define MAX_DISTANCE 200 // Maximum distance for the sensor in cm

// LED indicators
#define RED_LED 25    // LED for indicating occupied spot
#define GREEN_LED 26  // LED for indicating available spot

// Parking spot ID
const char* spot_id = "SPOT_01"; // Unique identifier for the parking spot
bool is_occupied = false; // Stores the current occupancy status
unsigned long last_publish_time = 0; // Stores the last published timestamp
const unsigned long publish_interval = 30000; // Publish interval in milliseconds (30 seconds)

// Create instances for Wi-Fi, MQTT, and Ultrasonic sensor
WiFiClient wifi_client; // WiFi client instance
PubSubClient mqtt_client(wifi_client); // MQTT client instance
NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE); // Ultrasonic sensor instance

void setup() {
  Serial.begin(115200); // Initialize serial communication for debugging

  pinMode(RED_LED, OUTPUT); // Set RED LED as output
  pinMode(GREEN_LED, OUTPUT); // Set GREEN LED as output

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("."); // Print dots while connecting
  }
  Serial.println("\nWiFi connected");

  // Connect to MQTT broker
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
  connect_mqtt();
  
  // Initialize OM2M container
  initialize_om2m();
}

void loop() {
  if (!mqtt_client.connected()) {
    connect_mqtt(); // Reconnect to MQTT if disconnected
  }
  mqtt_client.loop();

  // Read distance from ultrasonic sensor
  unsigned int distance = sonar.ping_cm();
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  // Determine if parking spot is occupied (Threshold: 50cm)
  bool current_status = (distance > 0 && distance < 50);

  // Publish status if changed or time interval elapsed
  if (current_status != is_occupied || millis() - last_publish_time > publish_interval) {
    is_occupied = current_status;
    update_leds(); // Update LED indicators
    publish_status(); // Publish the updated status
    last_publish_time = millis(); // Update last publish timestamp
  }

  delay(1000); // Wait 1 second before checking again
}

// Connect to MQTT broker
void connect_mqtt() {
  while (!mqtt_client.connected()) {
    String client_id = "esp32-parking-client-" + String(random(0xffff), HEX);
    if (mqtt_client.connect(client_id.c_str())) {
      Serial.println("Connected to MQTT Broker!");
      mqtt_client.subscribe(mqtt_topic_control); // Subscribe to control messages
    } else {
      Serial.print("Failed with state ");
      Serial.println(mqtt_client.state());
      delay(2000);
    }
  }
}

// MQTT message callback function
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
}

// Publish parking status
void publish_status() {
  StaticJsonDocument<200> doc;
  doc["spot_id"] = spot_id;
  doc["status"] = is_occupied ? "occupied" : "available";
  doc["distance"] = sonar.ping_cm();
  doc["timestamp"] = millis();
  
  char mqtt_message[200];
  serializeJson(doc, mqtt_message);
  mqtt_client.publish(mqtt_topic_status, mqtt_message);
  Serial.print("Published status: ");
  Serial.println(mqtt_message);
  
  update_thingspeak(is_occupied);
  update_om2m(mqtt_message);
}

// Update LED indicators
void update_leds() {
  digitalWrite(RED_LED, is_occupied ? HIGH : LOW);
  digitalWrite(GREEN_LED, is_occupied ? LOW : HIGH);
}

// Send data to ThingSpeak
void update_thingspeak(bool occupied) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/update?api_key=" + String(thingspeak_api_key) + "&field1=" + (occupied ? "1" : "0");
    http.begin(url);
    http.GET();
    http.end();
  }
}

// Initialize OM2M container
void initialize_om2m() { /* Code to create AE and container */ }

// Update OM2M server with parking data
void update_om2m(const char* data) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(om2m_server) + "/~/in-cse/in-name/" + String(om2m_ae) + "/" + String(om2m_container);
    String content = "{\"m2m:cin\":{\"con\":\"" + String(data) + "\"}}";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json;ty=4");
    http.addHeader("X-M2M-Origin", String(om2m_username) + ":" + String(om2m_password));
    http.POST(content);
    http.end();
  }
}
