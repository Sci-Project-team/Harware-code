#include <WiFi.h>
#include <PubSubClient.h>

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
WiFiServer server(80);

String incomingLogs = "";
String header = "";

// --- MQTT callback ---
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (String(topic) == logs_topic) {
    incomingLogs += msg + "<br>";
    Serial.println("[LOG] " + msg);
  }
}

// --- Utility to extract form values ---
String getFormValue(String body, String name) {
  int start = body.indexOf(name + "=");
  if (start == -1) return "";
  start += name.length() + 1;
  int end = body.indexOf("&", start);
  if (end == -1) end = body.length();
  return urlDecode(body.substring(start, end));
}

// --- URL decode ---
String urlDecode(String input) {
  String result = "";
  char temp[] = "0x00";
  for (int i = 0; i < input.length(); i++) {
    if (input[i] == '+') result += ' ';
    else if (input[i] == '%' && i + 2 < input.length()) {
      temp[2] = input[i + 1];
      temp[3] = input[i + 2];
      result += (char)strtol(temp, NULL, 16);
      i += 2;
    } else result += input[i];
  }
  return result;
}

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi: " + WiFi.localIP().toString());

  // Start web server
  server.begin();

  // MQTT setup
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);

  while (!client.connected()) {
    String client_id = "esp32-web-" + String(WiFi.macAddress());
    Serial.printf("Connecting to MQTT as %s...\n", client_id.c_str());
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Connected to MQTT");
    } else {
      Serial.printf("Failed, rc=%d\n", client.state());
      delay(2000);
    }
  }

  client.subscribe(logs_topic);
  client.publish(logs_topic, "ESP32-S3 web interface ready");
}

void loop() {
  client.loop();  // Always keep MQTT alive

  WiFiClient webClient = server.available();
  if (webClient) {
    header = "";
    String requestBody = "";
    unsigned long timeout = millis();

    while (webClient.connected() && millis() - timeout < 1000) {
      client.loop(); // keep MQTT alive
      if (webClient.available()) {
        char c = webClient.read();
        header += c;

        // Check for end of headers
        if (header.endsWith("\r\n\r\n")) {
          // Read POST body if it's a form submission
          if (header.indexOf("POST /sendmessage") >= 0) {
            while (webClient.available() == 0) {
              client.loop();  // allow MQTT during wait
              delay(1);
            }

            while (webClient.available()) {
              requestBody += (char)webClient.read();
              client.loop();  // continue MQTT
            }

            String phone = getFormValue(requestBody, "phone");
            String message = getFormValue(requestBody, "message");
            String finalMsg = "To: " + phone + " | Message: " + message;

            client.publish(send_topic, finalMsg.c_str());
            client.publish(logs_topic, ("Published message: " + finalMsg).c_str());
            Serial.println("Published: " + finalMsg);
          }

          // Respond to HTTP client
          webClient.println("HTTP/1.1 200 OK");
          webClient.println("Content-type:text/html");
          webClient.println("Connection: close");
          webClient.println();

          webClient.println("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32 MQTT</title></head><body>");

          if (header.indexOf("GET /logs") >= 0) {
            webClient.println("<h2>MQTT Logs</h2>");
            webClient.println("<div style='font-family:monospace;'>" + incomingLogs + "</div>");
            webClient.println("<br><a href='/'>Go Back</a>");
          } else {
            webClient.println("<h2>Send Message</h2>");
            webClient.println("<form action='/sendmessage' method='POST'>");
            webClient.println("Phone: <input name='phone'><br><br>");
            webClient.println("Message: <textarea name='message'></textarea><br><br>");
            webClient.println("<input type='submit' value='Send Message'>");
            webClient.println("</form>");
            webClient.println("<br><a href='/logs'>View Logs</a>");
          }

          webClient.println("</body></html>");
          break;
        }
      }
    }

    delay(1);
    webClient.stop();
  }
}
