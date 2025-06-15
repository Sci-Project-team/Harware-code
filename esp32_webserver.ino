#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <EEPROM.h>

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

// File path
const char* sent_messages_path = "/sent_messages.json";
const int max_messages = 50; // Maximum number of messages to store

// EEPROM settings
#define EEPROM_SIZE 512
#define MAX_USERS 10
#define USERNAME_LENGTH 20
#define PASSWORD_LENGTH 20

// User structure
struct User {
  char username[USERNAME_LENGTH];
  char password[PASSWORD_LENGTH];
  bool active;
};

// Session settings
String sessionUser = "";
User users[MAX_USERS];
int userCount = 0;


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

// EEPROM functions
void loadUsersFromEEPROM() {
  userCount = EEPROM.read(0);
  if (userCount > MAX_USERS) userCount = 0; // Reset if corrupted
  
  for (int i = 0; i < userCount; i++) {
    int baseAddr = 1 + (i * sizeof(User));
    EEPROM.get(baseAddr, users[i]);
  }
}

void saveUsersToEEPROM() {
   if (userCount > MAX_USERS) return;
  EEPROM.write(0, userCount);
  for (int i = 0; i < userCount; i++) {
    int baseAddr = 1 + (i * sizeof(User));
    EEPROM.put(baseAddr, users[i]);
  }
  EEPROM.commit();
}

// Auth functions
String extractAuthUser(String request) {
  // Extract username from Basic Auth header
  int authIndex = request.indexOf("Authorization: Basic ");
  if (authIndex == -1) return "";
  
  // This is a simplified extraction - in real implementation you'd decode base64
  // For now, we'll use form-based authentication instead
  return "";
}

bool isValidUser(String username) {
  return userExists(username);
}

bool addUser(String username, String password) {
  if (userCount >= MAX_USERS) return false;
  
  username.toCharArray(users[userCount].username, USERNAME_LENGTH);
  password.toCharArray(users[userCount].password, PASSWORD_LENGTH);
  users[userCount].active = true;
  userCount++;
  
  saveUsersToEEPROM();
  return true;
}

bool userExists(String username) {
  for (int i = 0; i < userCount; i++) {
    if (users[i].active && String(users[i].username) == username) {
      return true;
    }
  }
  return false;
}

bool authenticateUser(String username, String password) {
  for (int i = 0; i < userCount; i++) {
    if (users[i].active && 
        String(users[i].username) == username && 
        String(users[i].password) == password) {
      return true;
    }
  }
  return false;
}

String extractFormValue(String body, String fieldName) {
  String field = fieldName + "=";
  int startIndex = body.indexOf(field);
  if (startIndex == -1) {
    Serial.println("Field '" + fieldName + "' not found in: " + body);
    return "";
  }
  
  startIndex += field.length();
  int endIndex = body.indexOf("&", startIndex);
  if (endIndex == -1) endIndex = body.length();
  
  String value = body.substring(startIndex, endIndex);
  
  // URL decode common characters
  value.replace("+", " ");
  value.replace("%20", " ");
  value.replace("%21", "!");
  value.replace("%40", "@");
  value.replace("%23", "#");
  value.replace("%24", "$");
  value.replace("%25", "%");
  value.replace("%5E", "^");
  value.replace("%26", "&");
  value.replace("%2A", "*");
  value.replace("%28", "(");
  value.replace("%29", ")");
  
  // Trim whitespace
  value.trim();
  
  Serial.println("Extracting '" + fieldName + "': raw='" + body.substring(startIndex, endIndex) + "' decoded='" + value + "'");
  return value;
}


void setup() {
  Serial.begin(115200);

  // Initialize SPIFFS
  initSPIFFS();

  //Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Load existing users from EEPROM
  loadUsersFromEEPROM();
  
  // Add default admin user if no users exist
  if (userCount == 0) {
    addUser("admin", "password123");
  }

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
    String finalMsg = "To: " + phone + " | Message: " + message;
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

// --- Login Page ---
void sendLoginPage(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  webClient.println(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Login</title>
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
        .container {
          background: white;
          padding: 2em;
          border-radius: 10px;
          box-shadow: 0 0 15px rgba(0,0,0,0.1);
          text-align: center;
          width: 300px;
        }
        input {
          width: 100%;
          padding: 10px;
          margin: 10px 0;
          border: 1px solid #ddd;
          border-radius: 5px;
          box-sizing: border-box;
        }
        button {
          background-color: #4CAF50;
          color: white;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          cursor: pointer;
          width: 100%;
          margin: 5px 0;
        }
        button:hover {
          background-color: #45a049;
        }
        .signup-link {
          background-color: #008CBA;
        }
        .signup-link:hover {
          background-color: #007B9A;
        }
      </style>
    </head>
    <body>
      <div class="container">
        <h1>Login</h1>
        <form action="/login" method="post">
          <input type="text" name="username" placeholder="Username" required>
          <input type="password" name="password" placeholder="Password" required>
          <button type="submit">Login</button>
        </form>
        <button class="signup-link" onclick="window.location.href='/signup'">Sign Up</button>
      </div>
    </body>
    </html>
  )rawliteral");
}

void sendErrorPage(WiFiClient &webClient, String message) {
  webClient.println("HTTP/1.1 400 Bad Request");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  webClient.println(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Error</title>
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
        button {
          background-color: #008CBA;
          color: white;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          cursor: pointer;
        }
      </style>
    </head>
    <body>
      <div class="box">
        <h1>Error</h1>
        <p>)rawliteral" + message + R"rawliteral(</p>
        <button onclick="history.back()">Go Back</button>
      </div>
    </body>
    </html>
  )rawliteral");
}

void sendSuccessPage(WiFiClient &webClient, String message) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  webClient.println(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Success</title>
      <style>
        body {
          background-color: #f0fff0;
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
          border: 2px solid #4CAF50;
          border-radius: 10px;
          text-align: center;
        }
        h1 {
          color: #4CAF50;
        }
        button {
          background-color: #4CAF50;
          color: white;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          cursor: pointer;
        }
      </style>
    </head>
    <body>
      <div class="box">
        <h1>Success!</h1>
        <p>)rawliteral" + message + R"rawliteral(</p>
        <button onclick="window.location.href='/login'">Login Now</button>
      </div>
    </body>
    </html>
  )rawliteral");
}

void handleWelcomePage(WiFiClient &webClient, String username) {
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

void handleLogout() {
  sessionUser = "";
}

void handleRoot(WiFiClient &webClient) {  
  if (!sessionUser.isEmpty()) {
    handleWelcomePage(webClient, sessionUser);
  } else {
    sendLoginPage(webClient);
  }
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

// --- SignUp Page ---
void sendSignupPage(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  webClient.println(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Sign Up</title>
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
        .container {
          background: white;
          padding: 2em;
          border-radius: 10px;
          box-shadow: 0 0 15px rgba(0,0,0,0.1);
          text-align: center;
          width: 300px;
        }
        input {
          width: 100%;
          padding: 10px;
          margin: 10px 0;
          border: 1px solid #ddd;
          border-radius: 5px;
          box-sizing: border-box;
        }
        button {
          background-color: #4CAF50;
          color: white;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          cursor: pointer;
          width: 100%;
          margin: 5px 0;
        }
        button:hover {
          background-color: #45a049;
        }
        .login-link {
          background-color: #008CBA;
        }
        .login-link:hover {
          background-color: #007B9A;
        }
      </style>
    </head>
    <body>
      <div class="container">
        <h1>Sign Up</h1>
        <form action="/signup" method="post">
          <input type="text" name="username" placeholder="Username" required minlength="3" maxlength="19">
          <input type="password" name="password" placeholder="Password" required minlength="6" maxlength="19">
          <input type="password" name="confirm_password" placeholder="Confirm Password" required>
          <button type="submit">Sign Up</button>
        </form>
        <button class="login-link" onclick="window.location.href='/login'">Back to Login</button>
      </div>
    </body>
    </html>
  )rawliteral");
}

// --- Handle get logs request ---
void handleGetLogs(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  
  webClient.println(getLogs());
}

// --- Handle 404 request ---
void handle404(WiFiClient &webClient) {
  webClient.println("HTTP/1.1 404 Not Found");
  webClient.println("Content-Type: text/html");
  webClient.println("Connection: close");
  webClient.println();
  webClient.println("<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 - Page Not Found</h1></body></html>");
}

void processSignupNew(WiFiClient &webClient, String body) {
  String username = extractFormValue(body, "username");
  String password = extractFormValue(body, "password");
  String confirmPassword = extractFormValue(body, "confirm_password");
  
  // Debug output
  Serial.println("=== SIGNUP DEBUG ===");
  Serial.println("POST body: '" + body + "'");
  Serial.println("Username: '" + username + "' (len: " + String(username.length()) + ")");
  Serial.println("Password: '" + password + "' (len: " + String(password.length()) + ")");
  Serial.println("==================");
  
  // Validate input
  if (username.length() < 3 || password.length() < 6) {
    sendErrorPage(webClient, "Username must be at least 3 characters and password at least 6 characters. Got: username='" + username + "' password='" + password + "'");
    return;
  }
  
  if (password != confirmPassword) {
    sendErrorPage(webClient, "Passwords do not match.");
    return;
  }
  
  if (userExists(username)) {
    sendErrorPage(webClient, "Username already exists.");
    return;
  }
  
  if (userCount >= MAX_USERS) {
    sendErrorPage(webClient, "Maximum number of users reached.");
    return;
  }
  
  // Add user
  if (addUser(username, password)) {
    sendSuccessPage(webClient, "Account created successfully! You can now login.");
  } else {
    sendErrorPage(webClient, "Failed to create account.");
  }
}


void handleSignupNew(WiFiClient &webClient, String request, String method, String body) {
  if (method == "GET") {
    sendSignupPage(webClient);
  } else if (method == "POST") {
    processSignupNew(webClient, body);
  }
}

void processLoginNew(WiFiClient &webClient, String body) {
  String username = extractFormValue(body, "username");
  String password = extractFormValue(body, "password");
  
  if (authenticateUser(username, password)) {
    sessionUser = username; 
    handleWelcomePage(webClient, username);
  } else {
    sendErrorPage(webClient, "Invalid username or password.");
  }
}

void handleLoginNew(WiFiClient &webClient, String request, String method, String body) {
  if (method == "GET") {
    sendLoginPage(webClient);
  } else if (method == "POST") {
    processLoginNew(webClient, body);
  }
}

void loop() {
  client.loop(); // Handle MQTT

  WiFiClient webClient = server.available();
  if (!webClient) return;

  unsigned long timeout = millis() + 5000;

  // 1. Read Request Line
  String req = webClient.readStringUntil('\r');
  webClient.readStringUntil('\n'); // Discard leftover newline
  Serial.println("Request: " + req);

  // Parse method and path
  int methodEnd = req.indexOf(' ');
  int pathEnd = req.indexOf(' ', methodEnd + 1);
  if (methodEnd == -1 || pathEnd == -1) {
    webClient.stop();
    return;
  }

  String method = req.substring(0, methodEnd);
  String path = req.substring(methodEnd + 1, pathEnd);
  Serial.printf("[%s] %s\n", method.c_str(), path.c_str());

  // Read Headers and Body (for POST)
  String body;
  int contentLength = 0;
  
  // Read headers
  while (true) {
    String line = webClient.readStringUntil('\r');
    webClient.readStringUntil('\n'); // Consume the newline
    
    if (line.length() == 0) break; // End of headers
    
    Serial.println("Header: " + line);
    
    if (method == "POST" && line.startsWith("Content-Length:")) {
      contentLength = line.substring(15).toInt();
      Serial.println("Content-Length: " + String(contentLength));
    }
  }

  // Read body if POST and content-length > 0
  if (method == "POST" && contentLength > 0) {
    Serial.println("Reading body...");
    unsigned long startTime = millis();
    
    while (webClient.available() < contentLength && millis() - startTime < 2000) {
      delay(10);
    }
    
    for (int i = 0; i < contentLength; i++) {
      if (webClient.available()) {
        body += (char)webClient.read();
      } else {
        break;
      }
    }
    Serial.println("Body: " + body);
  }

  // Route Handling 
  if (sessionUser.isEmpty()) {
    // --- Unauthenticated Routes ---
    if (path == "/") {
      handleRoot(webClient);
    } 
    else if (path == "/login" && method == "GET") {
      sendLoginPage(webClient);
    }
    else if (path == "/login" && method == "POST") {
      processLoginNew(webClient, body);
    }
    else if (path == "/signup" && method == "GET") {
      sendSignupPage(webClient);
    }
    else if (path == "/signup" && method == "POST") {
      processSignupNew(webClient, body);
    }
    else {
      handle404(webClient);
    }
  } 
  else {
    // --- Authenticated Routes ---
    if (path == "/") {
      handleWelcomePage(webClient, sessionUser);
    }
    else if (path == "/send" && method == "POST") {
      handleSendMessage(webClient, body);
    }
    else if (path == "/messages-sent") {
      handleGetSentMessages(webClient);
    }
    else if (path == "/sent") {
      handleSentMessagesPage(webClient);
    }
    else if (path == "/get-logs") {
      handleGetLogs(webClient);
    }
    else if (path == "/logs") {
      handleLogsPage(webClient);
    }
    else if (path == "/logout") {
      handleLogout();
      sendLoginPage(webClient);
    }
    else {
      handle404(webClient);
    }
  }

  webClient.stop();
}