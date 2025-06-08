#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>

// WiFi credentials
const char* ssid = "realme C35";
const char* password = "NADANADA";

// GSM module pins
#define GSM_RX_PIN 3  
#define GSM_TX_PIN 1
#define gsmSerial Serial2

// Web server
WebServer server(80);

// Logging
String logBuffer = "";
const int MAX_LOG_LENGTH = 5000;

// NTP server settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // GMT+1 for Algeria
const int daylightOffset_sec = 0;  // No daylight saving in Algeria

void setup() {
  Serial.begin(115200);
  gsmSerial.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  
  logMessage("System starting up...");
  
  // Connect WiFi
  WiFi.begin(ssid, password);
  logMessage("Connecting to WiFi: " + String(ssid));
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    logMessage("WiFi connecting...");
  }
  
  logMessage("WiFi connected! IP: " + WiFi.localIP().toString());
  
  // Initialize time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  logMessage("Synchronizing time with NTP server...");
  
  // Wait for time to be set
  struct tm timeinfo;
  int attempts = 0;
  while(!getLocalTime(&timeinfo) && attempts < 10) {
    delay(1000);
    attempts++;
    logMessage("Getting time... attempt " + String(attempts));
  }
  
  if (getLocalTime(&timeinfo)) {
    logMessage("Time synchronized: " + getCurrentDateTime());
  } else {
    logMessage("WARNING: Could not obtain time from NTP server");
  }
  
  // Initialize GSM
  initGSM();
  
  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    logMessage("ERROR: SPIFFS mount failed");
  } else {
    logMessage("SPIFFS mounted successfully");
  }
  
  // Setup web server
  server.enableCORS(true);
  
  // Handle OPTIONS requests for CORS
  server.on("/send-sms", HTTP_OPTIONS, []() { server.send(204); });
  server.on("/messages-sent", HTTP_OPTIONS, []() { server.send(204); });
  
  // Main routes
  server.on("/", HTTP_GET, handleLogs);
  server.on("/logs", HTTP_GET, handleLogs);
  server.on("/send-sms", HTTP_POST, handleSendSMS);
  server.on("/messages-sent", HTTP_GET, handleGetSent);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/gsm-status", HTTP_GET, handleGSMStatus);
  server.on("/clear-logs", HTTP_POST, handleClearLogs);
  
  server.begin();
  logMessage("HTTP server started on port 80");
  logMessage("System initialization complete");
}

void loop() {
  server.handleClient();
}

// Get current date and time as formatted string
String getCurrentDateTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // Fallback to uptime if NTP fails
    unsigned long uptimeSeconds = millis() / 1000;
    unsigned long hours = uptimeSeconds / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    unsigned long seconds = uptimeSeconds % 60;
    return "Uptime: " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
  }
  
  char dateTimeStr[20];
  strftime(dateTimeStr, sizeof(dateTimeStr), "%d/%m/%Y %H:%M", &timeinfo);
  return String(dateTimeStr);
}

// Enhanced logging with real timestamp
void logMessage(String message) {
  String timestamp = getCurrentDateTime();
  String logEntry = timestamp + ": " + message;
  logBuffer += logEntry + "\n";
  
  // Trim buffer if too long
  if (logBuffer.length() > MAX_LOG_LENGTH) {
    logBuffer = logBuffer.substring(1000);
  }
  
  Serial.println(logEntry); // Also print to serial
}

// Enhanced GSM initialization
void initGSM() {
  logMessage("Initializing GSM module...");
  delay(3000);
  
  // Clear any pending data
  while(gsmSerial.available()) {
    gsmSerial.read();
  }
  
  // Test AT command
  logMessage("Testing AT command...");
  gsmSerial.println("AT");
  String response = waitForGSMResponse(2000);
  
  if (response.indexOf("OK") == -1) {
    logMessage("ERROR: GSM module not responding!");
    return;
  }
  
  // Disable echo
  gsmSerial.println("ATE0");
  waitForGSMResponse(2000);
  
  // Set SMS text mode
  logMessage("Setting SMS text mode...");
  gsmSerial.println("AT+CMGF=1");
  response = waitForGSMResponse(2000);
  
  if (response.indexOf("OK") == -1) {
    logMessage("ERROR: Failed to set SMS text mode!");
    return;
  }
  
  // Set character set
  gsmSerial.println("AT+CSCS=\"GSM\"");
  response = waitForGSMResponse(2000);
  
  if (response.indexOf("OK") == -1) {
    logMessage("Trying IRA charset...");
    gsmSerial.println("AT+CSCS=\"IRA\"");
    waitForGSMResponse(2000);
  }
  
  // Check SIM card
  logMessage("Checking SIM card...");
  gsmSerial.println("AT+CPIN?");
  response = waitForGSMResponse(3000);
  
  if (response.indexOf("READY") == -1) {
    logMessage("WARNING: SIM card not ready: " + response);
  }
  
  // Wait for network registration
  logMessage("Waiting for network registration...");
  for (int i = 0; i < 10; i++) {
    gsmSerial.println("AT+CREG?");
    response = waitForGSMResponse(2000);
    
    if (response.indexOf(",1") != -1 || response.indexOf(",5") != -1) {
      logMessage("Network registered successfully");
      break;
    }
    
    logMessage("Network registration attempt " + String(i+1) + "/10");
    delay(2000);
  }
  
  // Check signal strength
  gsmSerial.println("AT+CSQ");
  response = waitForGSMResponse(2000);
  logMessage("Signal strength: " + response);
  
  logMessage("GSM initialization complete");
}

// Enhanced SMS sending with better error handling
bool sendSMS(String phoneNumber, String message) {
  logMessage("Sending SMS to " + phoneNumber);
  
  // Clear buffer
  while(gsmSerial.available()) {
    gsmSerial.read();
  }
  
  // Ensure text mode
  gsmSerial.println("AT+CMGF=1");
  String modeResponse = waitForGSMResponse(2000);
  
  if (modeResponse.indexOf("OK") == -1) {
    logMessage("ERROR: Failed to set text mode");
    return false;
  }
  
  // Format phone number
  if (!phoneNumber.startsWith("+")) {
    phoneNumber = "+213" + phoneNumber;
  }
  
  // Set SMS recipient
  gsmSerial.println("AT+CMGS=\"" + phoneNumber + "\"");
  String response1 = waitForGSMResponse(10000);
  
  // Handle ERROR response
  if (response1.indexOf("ERROR") != -1) {
    logMessage("CMGS command failed, trying local format");
    String altNumber = phoneNumber.substring(4); // Remove +213
    gsmSerial.println("AT+CMGS=\"" + altNumber + "\"");
    response1 = waitForGSMResponse(10000);
  }
  
  if (response1.indexOf(">") == -1) {
    logMessage("ERROR: No SMS prompt received: " + response1);
    gsmSerial.println((char)27); // ESC to cancel
    return false;
  }
  
  logMessage("Got SMS prompt, sending message");
  
  // Send message content
  gsmSerial.print(message);
  delay(500);
  
  // Send Ctrl+Z
  gsmSerial.write(26);
  logMessage("Sent Ctrl+Z, waiting for response");
  
  // Wait for final response
  String response2 = waitForGSMResponse(30000);
  logMessage("SMS Response: " + response2);
  
  if (response2.indexOf("+CMGS:") != -1 || response2.indexOf("OK") != -1) {
    logMessage("SMS sent successfully!");
    return true;
  } else {
    logMessage("ERROR: SMS send failed: " + response2);
    return false;
  }
}

// Enhanced response waiting
String waitForGSMResponse(unsigned long timeout) {
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
      
      // Check for response endings
      if (response.endsWith("OK\r\n") || 
          response.endsWith("ERROR\r\n") || 
          response.endsWith("> ") ||
          response.indexOf("+CMGS:") != -1) {
        break;
      }
    }
    delay(10);
  }
  
  response.trim();
  return response;
}

// Check GSM status
bool checkGSMStatus() {
  while(gsmSerial.available()) gsmSerial.read();
  
  gsmSerial.println("AT");
  String response = waitForGSMResponse(2000);
  
  if (response.indexOf("OK") == -1) return false;
  
  gsmSerial.println("AT+CPIN?");
  response = waitForGSMResponse(3000);
  
  if (response.indexOf("READY") == -1) return false;
  
  gsmSerial.println("AT+CREG?");
  response = waitForGSMResponse(2000);
  
  return (response.indexOf(",1") != -1 || response.indexOf(",5") != -1);
}

// File operations
bool writeToFile(String filename, String data) {
  if (!filename.startsWith("/")) filename = "/" + filename;
  
  File file = SPIFFS.open(filename, FILE_APPEND);
  if (!file) {
    logMessage("ERROR: Failed to open file: " + filename);
    return false;
  }
  
  file.println(data);
  file.close();
  return true;
}

String readFromFile(String filename) {
  if (!filename.startsWith("/")) filename = "/" + filename;
  
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) return "";
  
  String content = file.readString();
  file.close();
  return content;
}

// Save SMS log with real timestamp
void saveSMSLog(String phoneNumber, String message) {
  String datetime = getCurrentDateTime();
  String logEntry = "=== SMS Sent ===\n";
  logEntry += "Time: " + datetime + "\n";
  logEntry += "Phone: " + phoneNumber + "\n";
  logEntry += "Message: " + message + "\n";
  logEntry += "------------------------\n";
  
  writeToFile("/sent_sms.log", logEntry);
}

// Web handlers
void handleLogs() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>ESP32 GSM Logs</title>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<style>body{font-family:monospace;background:#000;color:#0f0;padding:20px;}";
  html += "pre{white-space:pre-wrap;}</style></head><body>";
  html += "<h2>ESP32 GSM Live Logs</h2>";
  html += "<p>Current Time: " + getCurrentDateTime() + "</p>";
  html += "<p>Auto-refresh: 5s | <a href='/clear-logs' onclick='fetch(\"/clear-logs\", {method:\"POST\"});location.reload();'>Clear</a></p>";
  html += "<pre>" + logBuffer + "</pre></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSendSMS() {
  logMessage("SMS send request received");
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(1024);
  
  if (deserializeJson(doc, body)) {
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
    saveSMSLog(phoneNumber, message);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"SMS sent successfully\"}");
  } else {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send SMS\"}");
  }
}

void handleGetSent() {
  String sentLogs = readFromFile("/sent_sms.log");
  server.send(200, "text/plain", sentLogs.length() > 0 ? sentLogs : "No sent SMS logs found");
}

void handleStatus() {
  String response = "{\"status\":\"running\",\"wifi\":" +
                   String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                   ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                   ",\"current_time\":\"" + getCurrentDateTime() + "\"" +
                   ",\"uptime\":\"" + String(millis() / 1000) + "s\"" +
                   ",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
  
  server.send(200, "application/json", response);
}

void handleGSMStatus() {
  bool status = checkGSMStatus();
  String response = "{\"gsm_status\":" + String(status ? "true" : "false") + ",\"current_time\":\"" + getCurrentDateTime() + "\"}";
  server.send(200, "application/json", response);
}

void handleClearLogs() {
  logBuffer = "";
  logMessage("Log buffer cleared");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Logs cleared\"}");
}