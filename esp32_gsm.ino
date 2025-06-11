#include <WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

// GSM UART pins (adjust as needed)
#define GSM_RX 15
#define GSM_TX 14

SoftwareSerial gsmSerial(GSM_RX, GSM_TX);

// WiFi credentials
const char* ssid = "realme C35";
const char* password = "NADANADA";

// MQTT Broker
const char* mqtt_broker = "broker.emqx.io";
const char* mqtt_username = "emqx";
const char* mqtt_password = "public";
const int mqtt_port = 1883;

// MQTT Topics
const char* send_topic = "emqx/esp32/sendmessage";
const char* logs_topic = "logs";

WiFiClient espClient;
PubSubClient client(espClient);

// === GSM Functions ===
void sendATCommand(const char* cmd, int waitMs = 1000) {
  gsmSerial.println(cmd);
  delay(waitMs);
  while (gsmSerial.available()) {
    Serial.write(gsmSerial.read());
  }
}

void sendSMS(const char* number, const char* message) {
  sendATCommand("AT+CMGF=1", 1000); // Set SMS to text mode

  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(number);
  gsmSerial.println("\"");
  delay(1000);
  client.loop();  // Maintain MQTT connection

  gsmSerial.print(message);
  gsmSerial.write(26); // CTRL+Z to send SMS

  unsigned long start = millis();
  while (millis() - start < 5000) {
    while (gsmSerial.available()) {
      Serial.write(gsmSerial.read()); // Print GSM output
    }
    client.loop(); // Keep MQTT alive
  }
}

// === MQTT callback ===
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("[MQTT] Received: " + msg);

  // Parse "To: <number> | Message: <text>"
  int toIndex = msg.indexOf("To: ");
  int pipeIndex = msg.indexOf(" | ");
  if (toIndex == -1 || pipeIndex == -1) {
    client.publish(logs_topic, "Invalid message format");
    return;
  }

  String phone = msg.substring(toIndex + 4, pipeIndex);
  String text = msg.substring(pipeIndex + 10);

  String log1 = "Sending SMS to " + phone;
  client.publish(logs_topic, log1.c_str());

  sendSMS(phone.c_str(), text.c_str());

  String log2 = "SMS sent to " + phone;
  client.publish(logs_topic, log2.c_str());
}

void setup() {
  Serial.begin(115200);
  gsmSerial.begin(9600);
  delay(3000);
  sendATCommand("AT");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");

  // MQTT setup
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);

  while (!client.connected()) {
    String client_id = "esp32-gsm-" + String(WiFi.macAddress());
    Serial.printf("Connecting to MQTT as %s...\n", client_id.c_str());
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Connected to MQTT");
    } else {
      Serial.printf("Failed, rc=%d\n", client.state());
      delay(2000);
    }
  }

  client.subscribe(send_topic);
  client.publish(logs_topic, "ESP32 GSM subscriber connected and ready");
}

void loop() {
  client.loop(); // Always call this frequently
}
