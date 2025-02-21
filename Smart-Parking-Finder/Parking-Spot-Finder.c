#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NewPing.h>

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// MQTT Broker settings
const char* mqtt_broker = "broker.hivemq.com"; // Public broker for testing
const char* mqtt_topic_status = "parking/spot/status";
const char* mqtt_topic_control = "parking/spot/control";
const int mqtt_port = 1883;

// ThingSpeak settings
const char* thingspeak_server = "api.thingspeak.com";
const char* thingspeak_api_key = "YOUR_THINGSPEAK_API_KEY";

// OM2M settings
const char* om2m_server = "http://YOUR_OM2M_SERVER:8080";
const char* om2m_app_id = "parking_management";
const char* om2m_container = "parking_spots";
const char* om2m_ae = "parking_finder";
const char* om2m_username = "admin";
const char* om2m_password = "admin";

// Ultrasonic sensor pins
#define TRIG_PIN 5
#define ECHO_PIN 18
#define MAX_DISTANCE 200 // Maximum distance in cm

// LED indicators
#define RED_LED 25    // Occupied indicator
#define GREEN_LED 26  // Available indicator

// Parking spot ID
const char* spot_id = "SPOT_01";
bool is_occupied = false;
unsigned long last_publish_time = 0;
const unsigned long publish_interval = 30000; // 30 seconds

// Instances
WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE);

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
  
  // Connect to MQTT broker
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
  connect_mqtt();
  
  // Initialize the OM2M application entity
  initialize_om2m();
}

void loop() {
  if (!mqtt_client.connected()) {
    connect_mqtt();
  }
  mqtt_client.loop();
  
  // Measure distance from ultrasonic sensor
  unsigned int distance = sonar.ping_cm();
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");
  
  // Determine if spot is occupied (object detected within 50cm)
  bool current_status = (distance > 0 && distance < 50);
  
  // If status changed or publish interval elapsed
  if (current_status != is_occupied || millis() - last_publish_time > publish_interval) {
    is_occupied = current_status;
    update_leds();
    publish_status();
    last_publish_time = millis();
  }
  
  delay(1000); // Check every second
}

void connect_mqtt() {
  while (!mqtt_client.connected()) {
    String client_id = "esp32-parking-client-";
    client_id += String(random(0xffff), HEX);
    
    if (mqtt_client.connect(client_id.c_str())) {
      Serial.println("Connected to MQTT Broker!");
      mqtt_client.subscribe(mqtt_topic_control);
    } else {
      Serial.print("Failed with state ");
      Serial.println(mqtt_client.state());
      delay(2000);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  
  // Handle control messages
  if (String(topic) == mqtt_topic_control) {
    // Process any control commands here
    // For example, remote LED control or configuration updates
  }
}

void publish_status() {
  // Create JSON document
  StaticJsonDocument<200> doc;
  doc["spot_id"] = spot_id;
  doc["status"] = is_occupied ? "occupied" : "available";
  doc["distance"] = sonar.ping_cm();
  doc["timestamp"] = millis();
  
  // Serialize JSON to string
  char mqtt_message[200];
  serializeJson(doc, mqtt_message);
  
  // Publish to MQTT
  mqtt_client.publish(mqtt_topic_status, mqtt_message);
  Serial.print("Published status: ");
  Serial.println(mqtt_message);
  
  // Send data to ThingSpeak
  update_thingspeak(is_occupied);
  
  // Update OM2M platform
  update_om2m(mqtt_message);
}

void update_leds() {
  digitalWrite(RED_LED, is_occupied ? HIGH : LOW);
  digitalWrite(GREEN_LED, is_occupied ? LOW : HIGH);
}

void update_thingspeak(bool occupied) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/update?api_key=";
    url += thingspeak_api_key;
    url += "&field1=";
    url += occupied ? "1" : "0";
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("ThingSpeak response: " + payload);
    } else {
      Serial.println("Error on ThingSpeak request");
    }
    
    http.end();
  }
}

void initialize_om2m() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Create Application Entity if it doesn't exist
    String url = String(om2m_server) + "/~/in-cse/in-name";
    String content = "{\"m2m:ae\":{\"rn\":\"" + String(om2m_ae) + 
                     "\",\"api\":\"parking-app\",\"rr\":true,\"poa\":[\"http://example.com\"]}}";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json;ty=2");
    http.addHeader("X-M2M-Origin", String(om2m_username) + ":" + String(om2m_password));
    http.addHeader("X-M2M-RI", "123456");
    http.addHeader("X-M2M-NM", om2m_ae);
    
    int httpCode = http.POST(content);
    http.end();
    
    // Create container
    url = String(om2m_server) + "/~/in-cse/in-name/" + String(om2m_ae);
    content = "{\"m2m:cnt\":{\"rn\":\"" + String(om2m_container) + "\"}}";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json;ty=3");
    http.addHeader("X-M2M-Origin", String(om2m_username) + ":" + String(om2m_password));
    http.addHeader("X-M2M-RI", "123456");
    http.addHeader("X-M2M-NM", om2m_container);
    
    httpCode = http.POST(content);
    http.end();
    
    Serial.println("OM2M initialization completed");
  }
}

void update_om2m(const char* data) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Create a content instance with parking status
    String url = String(om2m_server) + "/~/in-cse/in-name/" + 
                 String(om2m_ae) + "/" + String(om2m_container);
    String content = "{\"m2m:cin\":{\"con\":\"" + String(data) + "\"}}";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json;ty=4");
    http.addHeader("X-M2M-Origin", String(om2m_username) + ":" + String(om2m_password));
    http.addHeader("X-M2M-RI", "123456");
    
    int httpCode = http.POST(content);
    if (httpCode > 0) {
      Serial.println("OM2M update successful");
    } else {
      Serial.println("OM2M update failed");
    }
    
    http.end();
  }
}
