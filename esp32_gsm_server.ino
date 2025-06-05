#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "realme C35";
const char* password = "NADANADA";

// GSM module pins (using hardware UART2)
#define GSM_RX_PIN 3  
#define GSM_TX_PIN 1

// Use hardware Serial2 for GSM (more reliable than software serial)
#define gsmSerial Serial2

// Web server on port 80
WebServer server(80);

// Logging system
String logBuffer = "";
const int MAX_LOG_LENGTH = 5000;
const int TRIM_LENGTH = 1000;

// Log levels
enum LogLevel {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR
};

// Log components
enum LogComponent {
  COMP_SYSTEM,
  COMP_WIFI,
  COMP_GSM,
  COMP_SMS,
  COMP_SERVER
};

void setup() {
  Serial.begin(115200);
  
  // Initialize hardware UART2 for GSM with custom pins
  gsmSerial.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
 
  logMessage(LOG_INFO, COMP_SYSTEM, "ESP32 CAM starting up...");
  logMessage(LOG_INFO, COMP_GSM, "Hardware UART2 initialized on pins RX:" + String(GSM_RX_PIN) + " TX:" + String(GSM_TX_PIN));
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  logMessage(LOG_INFO, COMP_WIFI, "Connecting to WiFi: " + String(ssid));
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    logMessage(LOG_DEBUG, COMP_WIFI, "WiFi connection attempt...");
  }
 
  logMessage(LOG_INFO, COMP_WIFI, "WiFi connected! IP: " + WiFi.localIP().toString());
 
  // Initialize GSM module
  initGSM();
  
  // Enable CORS
  server.enableCORS(true);

  server.on("/send-sms", HTTP_OPTIONS, []() {
    server.send(204);
  });
 
  // Setup web server routes
  server.on("/", HTTP_GET, handleLogs);           // View logs
  server.on("/logs", HTTP_GET, handleLogs);       // Alternative logs endpoint
  server.on("/send-sms", HTTP_POST, handleSendSMS);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/clear-logs", HTTP_POST, handleClearLogs);
 
  server.begin();
  logMessage(LOG_INFO, COMP_SERVER, "HTTP server started on port 80");
  logMessage(LOG_INFO, COMP_SYSTEM, "System initialization complete");
}

void loop() {
  server.handleClient();
  delay(10);
}

// Logging functions
String getTimestamp() {
  return String(millis() / 1000) + "s";
}

String logLevelToString(LogLevel level) {
  switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO: return "INFO";
    case LOG_WARNING: return "WARN";
    case LOG_ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

String logComponentToString(LogComponent component) {
  switch (component) {
    case COMP_SYSTEM: return "SYSTEM";
    case COMP_WIFI: return "WIFI";
    case COMP_GSM: return "GSM";
    case COMP_SMS: return "SMS";
    case COMP_SERVER: return "SERVER";
    default: return "UNKNOWN";
  }
}

void logMessage(LogLevel level, LogComponent component, String message) {
  String logEntry = getTimestamp() + " [" + logLevelToString(level) + "] [" + 
                   logComponentToString(component) + "] " + message;
  
  // Add to buffer (keep manageable size)
  logBuffer += logEntry + "\n";
  
  // Trim buffer if it gets too long
  if (logBuffer.length() > MAX_LOG_LENGTH) {
    logBuffer = logBuffer.substring(TRIM_LENGTH);
  }
  
  // Also print to Serial for development
  Serial.println(logEntry);
  
  // Handle any pending web requests
  server.handleClient();
}

// Web server handlers
void handleLogs() {
  String html = "<!DOCTYPE html><html><head><title>ESP32 CAM Logs</title>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:monospace;background:#000;color:#0f0;padding:20px;}";
  html += "pre{white-space:pre-wrap;word-wrap:break-word;}</style></head><body>";
  html += "<h2>ESP32 CAM Live Logs</h2>";
  html += "<p>Auto-refresh every 5 seconds | <a href='/clear-logs' onclick='fetch(\"/clear-logs\", {method:\"POST\"});location.reload();'>Clear Logs</a></p>";
  html += "<pre>" + logBuffer + "</pre></body></html>";
  
  server.send(200, "text/html", html);
  logMessage(LOG_DEBUG, COMP_SERVER, "Logs page accessed");
}

void handleClearLogs() {
  logBuffer = "";
  logMessage(LOG_INFO, COMP_SERVER, "Log buffer cleared");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Logs cleared\"}");
}

void initGSM() {
  logMessage(LOG_INFO, COMP_GSM, "Initializing GSM module...");
  delay(3000);
 
  // Test AT command with response check
  logMessage(LOG_DEBUG, COMP_GSM, "Testing AT command...");
  gsmSerial.println("AT");
  String response = waitForGSMResponse(2000);
  logMessage(LOG_DEBUG, COMP_GSM, "AT Response: " + (response.length() > 0 ? response : "NO RESPONSE"));
  
  if (response.indexOf("OK") == -1) {
    logMessage(LOG_ERROR, COMP_GSM, "GSM module not responding to AT command!");
    return;
  }
 
  // Set SMS text mode
  logMessage(LOG_DEBUG, COMP_GSM, "Setting SMS text mode...");
  gsmSerial.println("AT+CMGF=1");
  response = waitForGSMResponse(2000);
  logMessage(LOG_DEBUG, COMP_GSM, "CMGF Response: " + (response.length() > 0 ? response : "NO RESPONSE"));
 
  // Set SMS character set
  logMessage(LOG_DEBUG, COMP_GSM, "Setting SMS character set...");
  gsmSerial.println("AT+CSCS=\"GSM\"");
  response = waitForGSMResponse(2000);
  logMessage(LOG_DEBUG, COMP_GSM, "CSCS Response: " + (response.length() > 0 ? response : "NO RESPONSE"));
  
  // Check SIM card status
  logMessage(LOG_DEBUG, COMP_GSM, "Checking SIM card status...");
  gsmSerial.println("AT+CPIN?");
  response = waitForGSMResponse(2000);
  logMessage(LOG_DEBUG, COMP_GSM, "CPIN Response: " + (response.length() > 0 ? response : "NO RESPONSE"));
  
  // Check network registration
  logMessage(LOG_DEBUG, COMP_GSM, "Checking network registration...");
  gsmSerial.println("AT+CREG?");
  response = waitForGSMResponse(2000);
  logMessage(LOG_DEBUG, COMP_GSM, "CREG Response: " + (response.length() > 0 ? response : "NO RESPONSE"));
  
  // Check signal strength
  logMessage(LOG_DEBUG, COMP_GSM, "Checking signal strength...");
  gsmSerial.println("AT+CSQ");
  response = waitForGSMResponse(2000);
  logMessage(LOG_DEBUG, COMP_GSM, "CSQ Response: " + (response.length() > 0 ? response : "NO RESPONSE"));
 
  logMessage(LOG_INFO, COMP_GSM, "GSM initialization complete");
}

void handleSendSMS() {
  logMessage(LOG_INFO, COMP_SERVER, "SMS send request received");
  
  // Parse JSON body
  String body = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, body);
 
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
 
  String phoneNumber = doc["phone"];
  String message = doc["text"];
 
  if (phoneNumber.isEmpty() || message.isEmpty()) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Phone and text required\"}");
    return;
  }
 
  bool success = sendSMS(phoneNumber, message);
  if (success) {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"SMS sent successfully\"}");
  } else {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send SMS\"}");
  }
}

void handleStatus() {
  logMessage(LOG_DEBUG, COMP_SERVER, "Status request received");
  
  String response = "{\"status\":\"ESP32 is running\",\"wifi_connected\":" +
                   String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                   ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                   ",\"uptime\":\"" + getTimestamp() + "\"" +
                   ",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
  
  server.send(200, "application/json", response);
}

// Helper function to wait for and capture GSM responses
String waitForGSMResponse(unsigned long timeout) {
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (gsmSerial.available()) {
      response += gsmSerial.readString();
      // If we got some response, wait a bit more for complete response
      delay(100);
    }
    delay(10);
  }
  
  response.trim(); // Remove whitespace
  return response;
}

bool sendSMS(String phoneNumber, String message) {
  logMessage(LOG_DEBUG, COMP_SMS, "Starting SMS send process");
  
  // Set SMS recipient
  gsmSerial.println("AT+CMGS=\"" + phoneNumber + "\"");
  String response1 = waitForGSMResponse(3000);
  logMessage(LOG_DEBUG, COMP_SMS, "CMGS Response: " + (response1.length() > 0 ? response1 : "NO RESPONSE"));
  
  if (response1.indexOf(">") == -1) {
    logMessage(LOG_ERROR, COMP_SMS, "GSM module didn't prompt for message content");
    return false;
  }
 
  // Send message content
  gsmSerial.print(message);
  delay(100);
  logMessage(LOG_DEBUG, COMP_SMS, "Sent message content");
 
  // Send Ctrl+Z to send SMS
  gsmSerial.println((char)26);
  logMessage(LOG_DEBUG, COMP_SMS, "Sent Ctrl+Z to complete SMS");
  
  // Wait for final response
  String response2 = waitForGSMResponse(15000); // Longer timeout for SMS sending
  logMessage(LOG_DEBUG, COMP_SMS, "Final SMS Response: " + (response2.length() > 0 ? response2 : "NO RESPONSE"));
  
  if (response2.indexOf("OK") != -1) {
    logMessage(LOG_INFO, COMP_SMS, "GSM responded: OK - SMS sent successfully");
    return true;
  } else if (response2.indexOf("ERROR") != -1) {
    logMessage(LOG_ERROR, COMP_SMS, "GSM responded: ERROR - SMS send failed");
    return false;
  } else {
    logMessage(LOG_ERROR, COMP_SMS, "SMS send timeout or unexpected response");
    return false;
  }
}