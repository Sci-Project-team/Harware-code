#include <WiFi.h>

// WiFi credentials
const char* ssid = "ALHN-A013";
const char* password = "wifi_password";

// Authentication credentials
const char* username = "admin";
const char* pass = "password123";

// Base64-encoded "admin:password123"
const char* auth_base64 = "Basic YWRtaW46cGFzc3dvcmQxMjM=";

WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  // Connect to Wi-Fi
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n Connected to WiFi!");
    Serial.print("ESP32 IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();
  } else {
    Serial.println("\n Failed to connect to WiFi. Restarting...");
    ESP.restart();
  }
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("📡 New client connected");
    String request = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        // End of HTTP request
        if (request.endsWith("\r\n\r\n")) break;
      }
    }

    // Check Authorization
    if (request.indexOf(auth_base64) >= 0) {
      // Authenticated
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      client.println(R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <title>Welcome</title>
          <style>
            body {
              background-color: #f0f0f0;
              font-family: Arial, sans-serif;
              display: flex;
              justify-content: center;
              align-items: center;
              height: 100vh;
              margin: 0;
            }
            .box {
              background: white;
              padding: 2em;
              border-radius: 10px;
              box-shadow: 0 0 15px rgba(0,0,0,0.1);
              text-align: center;
            }
            h1 {
              color: #4CAF50;
            }
          </style>
        </head>
        <body>
          <div class="box">
            <h1>Authentication Successful!</h1>
            <p>Welcome to the secure ESP32 interface.</p>
          </div>
        </body>
        </html>
      )rawliteral");
    } else {
      // Unauthorized - ask for login
      client.println("HTTP/1.1 401 Unauthorized");
      client.println("WWW-Authenticate: Basic realm=\"ESP32\"");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      client.println(R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <title>401 Unauthorized</title>
          <style>
            body {
              background-color: #fff3f3;
              font-family: Arial, sans-serif;
              display: flex;
              justify-content: center;
              align-items: center;
              height: 100vh;
              margin: 0;
            }
            .box {
              background: white;
              padding: 2em;
              border: 2px solid #ff4d4d;
              border-radius: 10px;
              text-align: center;
            }
            h1 {
              color: #ff4d4d;
            }
          </style>
        </head>
        <body>
          <div class="box">
            <h1>Access Denied</h1>
            <p>Authentication is required to access this page.</p>
          </div>
        </body>
        </html>
      )rawliteral");
    }

    delay(1); // Allow the client to receive data
    client.stop();
    Serial.println("Client disconnected");
  }

  // Optional: Disconnect logic if Wi-Fi drops
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost WiFi connection. Restarting...");
    delay(2000);
    ESP.restart();
  }
}
