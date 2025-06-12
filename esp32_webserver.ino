#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>

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

// File paths
const char* sent_messages_path = "/sent_messages.json";
const int max_messages = 50; // Maximum number of messages to store

// NTP Settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; // UTC time
const int daylightOffset_sec = 0; // No daylight saving

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

// --- Initialize SPIFFS ---
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
  
  // Create sent messages file if it doesn't exist
  if (!SPIFFS.exists(sent_messages_path)) {
    File file = SPIFFS.open(sent_messages_path, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to create sent messages file");
      return;
    }
    file.print("[]"); // Empty JSON array
    file.close();
  }
}

// --- Initialize NTP time ---
void initTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println("Time synchronized with NTP server");
}

// --- Get current formatted time (DD/MM/YYYY HH:MM) ---
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "Time unknown";
  }
  
  // Format the time as DD/MM/YYYY HH:MM
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", &timeinfo);
  
  return String(timeStr);
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

// --- Record sent message to SPIFFS ---
void recordSentMessage(String phone, String message) {
  // Create JSON object for the new message
  DynamicJsonDocument newMessage(256);
  newMessage["time"] = getFormattedTime();
  newMessage["phone"] = phone;
  newMessage["message"] = message;
  
  // Read existing messages
  DynamicJsonDocument doc(4096);
  File file = SPIFFS.open(sent_messages_path, FILE_READ);
  if (!file) {
    Serial.println("Failed to open sent messages file for reading");
    return;
  }
  
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.print("Failed to parse sent messages: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Add new message to the array
  JsonArray messages = doc.as<JsonArray>();
  messages.add(newMessage);
  
  // Remove oldest messages if we exceed the limit
  while (messages.size() > max_messages) {
    messages.remove(0);
  }
  
  // Write back to file
  file = SPIFFS.open(sent_messages_path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open sent messages file for writing");
    return;
  }
  
  serializeJson(doc, file);
  file.close();
  
  Serial.println("Message recorded to SPIFFS");
}

// --- Get sent messages from SPIFFS ---
String getSentMessages() {
  File file = SPIFFS.open(sent_messages_path, FILE_READ);
  if (!file) {
    Serial.println("Failed to open sent messages file");
    return "No sent SMS logs found";
  }
  
  size_t size = file.size();
  if (size == 0) {
    file.close();
    return "No sent SMS logs found";
  }
  
  // Parse JSON
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.print("Failed to parse sent messages: ");
    Serial.println(error.c_str());
    return "Error reading message logs";
  }
  
  // Convert to text format for display
  String result = "";
  JsonArray messages = doc.as<JsonArray>();
  
  for (JsonObject msg : messages) {
    result += "=== SMS Sent ===\n";
    result += "Time: " + msg["time"].as<String>() + "\n";
    result += "Phone: " + msg["phone"].as<String>() + "\n";
    result += "Message: " + msg["message"].as<String>() + "\n\n";
  }
  
  return result;
}

// --- Get logs from MQTT ---
String getLogs() {
  return incomingLogs;
}

void setup() {
  Serial.begin(115200);

  // Initialize SPIFFS
  initSPIFFS();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi: " + WiFi.localIP().toString());

  // Initialize and get time
  initTime();

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

void handleSendMessage(WiFiClient &webClient, String requestBody) {
  String phone = getFormValue(requestBody, "phone");
  String message = getFormValue(requestBody, "message");
  
  if (phone.length() > 0 && message.length() > 0) {
    String finalMsg = "To: " + phone + " | Message" + message;
    client.publish(send_topic, finalMsg.c_str());
    client.publish(logs_topic, ("Published message: " + finalMsg).c_str());
    recordSentMessage(phone, message);
    Serial.println("Published: " + finalMsg);
    
    // Send success response
    webClient.println("HTTP/1.1 200 OK");
    webClient.println("Content-Type: text/plain");
    webClient.println("Connection: close");
    webClient.println();
    webClient.println("Message sent successfully");
  } else {
    // Send error response
    webClient.println("HTTP/1.1 400 Bad Request");
    webClient.println("Content-Type: text/plain");
    webClient.println("Connection: close");
    webClient.println();
    webClient.println("Invalid phone or message");
  }
}

void handleGetSentMessages(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: text/plain");
  webClient.println("Connection: close");
  webClient.println();
  
  String messages = getSentMessages();
  webClient.println(messages);
}

void handleRoot(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-type:text/html");
  webClient.println("Connection: close");
  webClient.println();

  // Send the HTML page
  webClient.println(R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 SMS Gateway</title>
  <link href="https://cdn.jsdelivr.net/npm/tailwindcss@2.2.19/dist/tailwind.min.css" rel="stylesheet">
</head>
<body class="bg-gray-100">
  <nav class="bg-blue-600 text-white shadow-lg">
    <div class="container mx-auto px-4 py-3">
      <div class="flex justify-between items-center">
        <h1 class="text-xl font-bold">ESP32 SMS Gateway</h1>
        <div class="flex space-x-4">
          <a href="/" class="hover:bg-blue-700 px-3 py-2 rounded">Envoyer SMS</a>
          <a href="/sent" class="hover:bg-blue-700 px-3 py-2 rounded">Historique</a>
          <a href="/received" class="hover:bg-blue-700 px-3 py-2 rounded">Messages Reçus</a>
          <a href="/logs" class="hover:bg-blue-700 px-3 py-2 rounded">Logs Système</a>
        </div>
      </div>
    </div>
  </nav>

  <div class="container mx-auto px-4 py-8">
    <div class="bg-white rounded-lg shadow-md p-6 max-w-md mx-auto">
      <h2 class="text-2xl font-bold text-gray-800 mb-6">Envoyer un SMS</h2>
      
      <form id="smsForm" class="space-y-4">
        <div>
          <label class="block text-gray-700 text-sm font-semibold mb-2">Numéro de téléphone</label>
          <input 
            type="text" 
            name="phone" 
            placeholder="Ex: 0782819451 ou +213782819451" 
            class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-300"
            required
          >
          <p class="text-xs text-gray-500 mt-1">Format accepté: 07XXXXXXXX ou +213XXXXXXXXX</p>
        </div>
        
        <div>
          <label class="block text-gray-700 text-sm font-semibold mb-2">Message</label>
          <textarea 
            name="message" 
            rows="4" 
            placeholder="Votre message ici..." 
            class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-300"
            required
          ></textarea>
        </div>
        
        <div id="errorMessage" class="hidden p-3 bg-red-50 border-l-4 border-red-500 text-red-700 rounded"></div>
        <div id="successMessage" class="hidden p-3 bg-green-50 border-l-4 border-green-500 text-green-700 rounded"></div>
        
        <button 
          type="submit" 
          class="w-full bg-blue-600 hover:bg-blue-700 text-white font-bold py-2 px-4 rounded-md transition-colors"
        >
          Envoyer SMS
        </button>
      </form>
    </div>
  </div>

  <script>
    document.getElementById('smsForm').addEventListener('submit', async function(e) {
      e.preventDefault();
      
      const form = e.target;
      const errorDiv = document.getElementById('errorMessage');
      const successDiv = document.getElementById('successMessage');
      
      errorDiv.classList.add('hidden');
      successDiv.classList.add('hidden');
      
      const formData = new FormData(form);
      const phone = formData.get('phone').trim();
      const message = formData.get('message').trim();
      
      // Basic validation
      if (!/^(\+213|0)[5-7]\d{8}$/.test(phone)) {
        errorDiv.textContent = "Numéro invalide. Doit commencer par 0 ou +213 suivi de 9 chiffres.";
        errorDiv.classList.remove('hidden');
        return;
      }
      
      if (message.length === 0) {
        errorDiv.textContent = "Le message ne peut pas être vide.";
        errorDiv.classList.remove('hidden');
        return;
      }
      
      try {
        const response = await fetch('/send', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
          },
          body: `phone=${encodeURIComponent(phone)}&message=${encodeURIComponent(message)}`
        });
        
        if (response.ok) {
          successDiv.textContent = "Message envoyé avec succès!";
          successDiv.classList.remove('hidden');
          form.reset();
        } else {
          const error = await response.text();
          throw new Error(error || 'Erreur lors de l\'envoi');
        }
      } catch (err) {
        errorDiv.textContent = err.message || "Erreur de connexion au serveur";
        errorDiv.classList.remove('hidden');
        console.error('Error:', err);
      }
    });
  </script>
</body>
</html>
)=====");
}

void handleSentMessagesPage(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-type:text/html");
  webClient.println("Connection: close");
  webClient.println();

  webClient.println(R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Historique des SMS - ESP32</title>
  <link href="https://cdn.jsdelivr.net/npm/tailwindcss@2.2.19/dist/tailwind.min.css" rel="stylesheet">
</head>
<body class="bg-gray-100">
  <nav class="bg-blue-600 text-white shadow-lg">
    <div class="container mx-auto px-4 py-3">
      <div class="flex justify-between items-center">
        <h1 class="text-xl font-bold">ESP32 SMS Gateway</h1>
        <div class="flex space-x-4">
          <a href="/" class="hover:bg-blue-700 px-3 py-2 rounded">Envoyer SMS</a>
          <a href="/sent" class="hover:bg-blue-700 px-3 py-2 rounded">Historique</a>
          <a href="/received" class="hover:bg-blue-700 px-3 py-2 rounded">Messages Reçus</a>
          <a href="/logs" class="hover:bg-blue-700 px-3 py-2 rounded">Logs Système</a>
        </div>
      </div>
    </div>
  </nav>

  <div class="container mx-auto px-4 py-8">
    <div class="bg-white rounded-lg shadow-md p-6">
      <div class="flex justify-between items-center mb-6">
        <h2 class="text-2xl font-bold text-gray-800">Historique des SMS Envoyés</h2>
        <button id="refreshBtn" class="bg-blue-500 hover:bg-blue-600 text-white px-4 py-2 rounded-lg">
          Actualiser
        </button>
      </div>
      
      <div id="loading" class="text-center py-8">
        <div class="animate-spin rounded-full h-12 w-12 border-t-2 border-b-2 border-blue-500 mx-auto"></div>
        <p class="mt-4 text-gray-600">Chargement de l'historique...</p>
      </div>
      
      <div id="messagesContainer" class="space-y-4 hidden">
        <!-- Messages will be inserted here by JavaScript -->
      </div>
      
      <div id="noMessages" class="text-center py-8 hidden">
        <svg class="w-12 h-12 mx-auto text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 19l9 2-9-18-9 18 9-2zm0 0v-8"></path>
        </svg>
        <p class="mt-4 text-gray-600">Aucun message envoyé pour le moment</p>
      </div>
    </div>
  </div>

  <script>
    async function fetchSentMessages() {
      try {
        document.getElementById('loading').classList.remove('hidden');
        document.getElementById('messagesContainer').classList.add('hidden');
        document.getElementById('noMessages').classList.add('hidden');
        
        const response = await fetch('/messages-sent');
        if (!response.ok) throw new Error('Network response was not ok');
        
        const text = await response.text();
        const messages = parseMessages(text);
        
        const container = document.getElementById('messagesContainer');
        container.innerHTML = '';
        
        if (messages.length > 0) {
          messages.forEach(msg => {
            const messageDiv = document.createElement('div');
            messageDiv.className = 'bg-blue-50 border border-blue-200 rounded-lg p-4';
            messageDiv.innerHTML = `
              <div class="flex justify-between items-center mb-2">
                <div class="font-medium text-blue-700">${msg.phone}</div>
                <div class="text-sm text-gray-500">${msg.time}</div>
              </div>
              <div class="bg-white p-3 rounded border border-gray-200">
                ${msg.message}
              </div>
            `;
            container.appendChild(messageDiv);
          });
          
          document.getElementById('messagesContainer').classList.remove('hidden');
        } else {
          document.getElementById('noMessages').classList.remove('hidden');
        }
      } catch (error) {
        console.error('Error fetching messages:', error);
        alert('Erreur lors du chargement des messages');
      } finally {
        document.getElementById('loading').classList.add('hidden');
      }
    }
    
    function parseMessages(text) {
      if (!text || text.includes('No sent SMS logs found')) return [];
      
      const messages = [];
      const messageBlocks = text.split('=== SMS Sent ===').filter(block => block.trim());
      
      for (const block of messageBlocks) {
        const lines = block.trim().split('\n');
        const message = { time: '', phone: '', message: '' };
        
        for (const line of lines) {
          if (line.startsWith('Time: ')) message.time = line.substring(6).trim();
          else if (line.startsWith('Phone: ')) message.phone = line.substring(7).trim();
          else if (line.startsWith('Message: ')) message.message = line.substring(9).trim();
        }
        
        if (message.phone && message.message) {
          messages.push(message);
        }
      }
      
      return messages;
    }
    
    document.getElementById('refreshBtn').addEventListener('click', fetchSentMessages);
    document.addEventListener('DOMContentLoaded', fetchSentMessages);
  </script>
</body>
</html>
)=====");
}

void handleGetReceivedMessages(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: application/json");
  webClient.println("Connection: close");
  webClient.println();
  
  webClient.println("{\"success\": true, \"messages\": []}"); // Placeholder
}

void handleReceivedMessagesPage(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-type:text/html");
  webClient.println("Connection: close");
  webClient.println();

  webClient.println(R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Messages Reçus - ESP32</title>
  <link href="https://cdn.jsdelivr.net/npm/tailwindcss@2.2.19/dist/tailwind.min.css" rel="stylesheet">
</head>
<body class="bg-gray-100">
  <nav class="bg-blue-600 text-white shadow-lg">
    <div class="container mx-auto px-4 py-3">
      <div class="flex justify-between items-center">
        <h1 class="text-xl font-bold">ESP32 SMS Gateway</h1>
        <div class="flex space-x-4">
          <a href="/" class="hover:bg-blue-700 px-3 py-2 rounded">Envoyer SMS</a>
          <a href="/sent" class="hover:bg-blue-700 px-3 py-2 rounded">Historique</a>
          <a href="/received" class="hover:bg-blue-700 px-3 py-2 rounded">Messages Reçus</a>
          <a href="/logs" class="hover:bg-blue-700 px-3 py-2 rounded">Logs Système</a>
        </div>
      </div>
    </div>
  </nav>

  <div class="container mx-auto px-4 py-8">
    <div class="bg-white rounded-lg shadow-md p-6">
      <div class="flex justify-between items-center mb-6">
        <h2 class="text-2xl font-bold text-gray-800">Messages Reçus</h2>
        <button id="refreshBtn" class="bg-blue-500 hover:bg-blue-600 text-white px-4 py-2 rounded-lg">
          Actualiser
        </button>
      </div>
      
      <div id="loading" class="text-center py-8">
        <div class="animate-spin rounded-full h-12 w-12 border-t-2 border-b-2 border-blue-500 mx-auto"></div>
        <p class="mt-4 text-gray-600">Chargement des messages...</p>
      </div>
      
      <div id="messagesContainer" class="space-y-4 hidden">
        <!-- Messages will be inserted here by JavaScript -->
      </div>
      
      <div id="noMessages" class="text-center py-8 hidden">
        <svg class="w-12 h-12 mx-auto text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2M4 13h2m6-8v2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v2M9 7h6"></path>
        </svg>
        <p class="mt-4 text-gray-600">Aucun message reçu pour le moment</p>
      </div>
    </div>
  </div>

  <script>
    async function fetchReceivedMessages() {
      try {
        document.getElementById('loading').classList.remove('hidden');
        document.getElementById('messagesContainer').classList.add('hidden');
        document.getElementById('noMessages').classList.add('hidden');
        
        const response = await fetch('/messages-received');
        if (!response.ok) throw new Error('Network response was not ok');
        
        const data = await response.json();
        
        const container = document.getElementById('messagesContainer');
        container.innerHTML = '';
        
        if (data.messages && data.messages.length > 0) {
          data.messages.forEach(msg => {
            const messageDiv = document.createElement('div');
            messageDiv.className = 'bg-green-50 border border-green-200 rounded-lg p-4';
            messageDiv.innerHTML = `
              <div class="flex justify-between items-center mb-2">
                <div class="font-medium text-green-700">${msg.numero}</div>
                <div class="text-sm text-gray-500">${formatTime(msg.date)}</div>
              </div>
              <div class="bg-white p-3 rounded border border-gray-200">
                ${msg.texte}
              </div>
            `;
            container.appendChild(messageDiv);
          });
          
          document.getElementById('messagesContainer').classList.remove('hidden');
        } else {
          document.getElementById('noMessages').classList.remove('hidden');
        }
      } catch (error) {
        console.error('Error fetching messages:', error);
        alert('Erreur lors du chargement des messages');
      } finally {
        document.getElementById('loading').classList.add('hidden');
      }
    }
    
    function formatTime(timestamp) {
      if (!timestamp) return 'Date inconnue';
      return timestamp;
    }
    
    document.getElementById('refreshBtn').addEventListener('click', fetchReceivedMessages);
    document.addEventListener('DOMContentLoaded', fetchReceivedMessages);
  </script>
</body>
</html>
)=====");
}

// --- Handle logs page request ---
void handleLogsPage(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-type:text/html");
  webClient.println("Connection: close");
  webClient.println();

  webClient.println(R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>System Logs - ESP32</title>
  <link href="https://cdn.jsdelivr.net/npm/tailwindcss@2.2.19/dist/tailwind.min.css" rel="stylesheet">
</head>
<body class="bg-gray-100">
  <nav class="bg-blue-600 text-white shadow-lg">
    <div class="container mx-auto px-4 py-3">
      <div class="flex justify-between items-center">
        <h1 class="text-xl font-bold">ESP32 SMS Gateway</h1>
        <div class="flex space-x-4">
          <a href="/" class="hover:bg-blue-700 px-3 py-2 rounded">Envoyer SMS</a>
          <a href="/sent" class="hover:bg-blue-700 px-3 py-2 rounded">Historique</a>
          <a href="/received" class="hover:bg-blue-700 px-3 py-2 rounded">Messages Reçus</a>
          <a href="/logs" class="hover:bg-blue-700 px-3 py-2 rounded">Logs Système</a>
        </div>
      </div>
    </div>
  </nav>

  <div class="container mx-auto px-4 py-8">
    <div class="bg-white rounded-lg shadow-md p-6">
      <div class="flex justify-between items-center mb-6">
        <h2 class="text-2xl font-bold text-gray-800">Logs Système</h2>
        <button id="refreshBtn" class="bg-blue-500 hover:bg-blue-600 text-white px-4 py-2 rounded-lg">
          Actualiser
        </button>
      </div>
      
      <div id="loading" class="text-center py-8">
        <div class="animate-spin rounded-full h-12 w-12 border-t-2 border-b-2 border-blue-500 mx-auto"></div>
        <p class="mt-4 text-gray-600">Chargement des logs...</p>
      </div>
      
      <div id="logsContainer" class="bg-gray-800 text-green-400 p-4 rounded-lg font-mono text-sm overflow-auto max-h-96 hidden">
        <!-- Logs will be inserted here by JavaScript -->
      </div>
      
      <div id="noLogs" class="text-center py-8 hidden">
        <svg class="w-12 h-12 mx-auto text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z"></path>
        </svg>
        <p class="mt-4 text-gray-600">Aucun log disponible pour le moment</p>
      </div>
    </div>
  </div>

  <script>
    async function fetchLogs() {
      try {
        document.getElementById('loading').classList.remove('hidden');
        document.getElementById('logsContainer').classList.add('hidden');
        document.getElementById('noLogs').classList.add('hidden');
        
        const response = await fetch('/get-logs');
        if (!response.ok) throw new Error('Network response was not ok');
        
        const logs = await response.text();
        const container = document.getElementById('logsContainer');
        
        if (logs && logs.trim().length > 0) {
          container.innerHTML = logs;
          document.getElementById('logsContainer').classList.remove('hidden');
        } else {
          document.getElementById('noLogs').classList.remove('hidden');
        }
      } catch (error) {
        console.error('Error fetching logs:', error);
        alert('Erreur lors du chargement des logs');
      } finally {
        document.getElementById('loading').classList.add('hidden');
      }
    }
    
    document.getElementById('refreshBtn').addEventListener('click', fetchLogs);
    document.addEventListener('DOMContentLoaded', fetchLogs);
    
    // Auto-refresh every 5 seconds
    setInterval(fetchLogs, 5000);
  </script>
</body>
</html>
)=====");
}

// --- Handle get logs request ---
void handleGetLogs(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  
  webClient.println(getLogs());
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
          if (header.indexOf("POST /send") >= 0) {
            while (webClient.available() == 0) {
              client.loop();  // allow MQTT during wait
              delay(1);
            }

            while (webClient.available()) {
              requestBody += (char)webClient.read();
              client.loop();  // continue MQTT
            }

            handleSendMessage(webClient, requestBody);
          }

          // Handle different routes
          if (header.indexOf("GET / ") >= 0 || header.indexOf("GET / ") >= 0) {
            handleRoot(webClient);
          } 
          else if (header.indexOf("GET /messages-sent") >= 0) {
            handleGetSentMessages(webClient);
          }
          else if (header.indexOf("GET /sent") >= 0) {
            handleSentMessagesPage(webClient);
          }
          else if (header.indexOf("GET /messages-received") >= 0) {
            handleGetReceivedMessages(webClient);
          }
          else if (header.indexOf("GET /received") >= 0) {
            handleReceivedMessagesPage(webClient);
          }
          else if (header.indexOf("GET /get-logs") >= 0) {
            handleGetLogs(webClient);
          }
          else if (header.indexOf("GET /logs") >= 0) {
            handleLogsPage(webClient);
          }
          else {
            // Default response
            webClient.println("HTTP/1.1 200 OK");
            webClient.println("Content-type:text/html");
            webClient.println("Connection: close");
            webClient.println();
            webClient.println("<!DOCTYPE html><html><body><h1>ESP32 Web Server</h1><p>Endpoint not found</p></body></html>");
          }
          break;
        }
      }
    }

    delay(1);
    webClient.stop();
  }
}