#include <WiFi.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "wifi_name";
const char* password = "wifi_password";

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

WiFiServer server(80);
User users[MAX_USERS];
int userCount = 0;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  
  // Load existing users from EEPROM
  loadUsersFromEEPROM();
  
  // Add default admin user if no users exist
  if (userCount == 0) {
    addUser("admin", "password123");
  }
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("ESP32 IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();
  } else {
    Serial.println("\nFailed to connect to WiFi. Restarting...");
    ESP.restart();
  }
}

void handle404(WiFiClient &client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 - Page Not Found</h1></body></html>");
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("📡 New client connected");
    String request = "";
    String body = "";
    bool isBody = false;
    int contentLength = 0;
    
    while (client.connected()) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        
        if (line.startsWith("Content-Length:")) {
          contentLength = line.substring(16).toInt();
          Serial.println("Content Length: " + String(contentLength));
        }
        
        if (line == "\r") {
          isBody = true;
          break;
        }
        
        request += line + "\n";
      }
    }
    
    // Read the body if POST request
    if (contentLength > 0) {
      while (client.available() && body.length() < contentLength) {
        body += (char)client.read();
      }
      Serial.println("POST body: " + body);
    }

    // Parse the request
    String method = request.substring(0, request.indexOf(' '));
    String path = request.substring(request.indexOf(' ') + 1);
    path = path.substring(0, path.indexOf(' '));
    
    Serial.println("Method: " + method + ", Path: " + path);
    
    // Handle different routes
    if (path == "/") {
      handleRoot(client, request);
    } else if (path == "/signup") {
      handleSignupNew(client, request, method, body);
    } else if (path == "/login") {
      handleLoginNew(client, request, method, body);
    } else {
      handle404(client);
    }

    delay(1);
    client.stop();
    Serial.println("Client disconnected");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost WiFi connection. Restarting...");
    delay(2000);
    ESP.restart();
  }
}

void handleRoot(WiFiClient &client, String request) {
  // Check if user is authenticated
  String authUser = extractAuthUser(request);
  if (authUser != "" && isValidUser(authUser)) {
    sendWelcomePage(client, authUser);
  } else {
    sendLoginPage(client);
  }
}

void handleSignup(WiFiClient &client, String request, String method) {
  if (method == "GET") {
    sendSignupPage(client);
  } else if (method == "POST") {
    processSignup(client, request);
  }
}

void handleLogin(WiFiClient &client, String request, String method) {
  if (method == "GET") {
    sendLoginPage(client);
  } else if (method == "POST") {
    processLogin(client, request);
  }
}

void sendLoginPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println(R"rawliteral(
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

void sendSignupPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println(R"rawliteral(
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

void sendWelcomePage(WiFiClient &client, String username) {
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
        <h1>Welcome, )rawliteral" + username + R"rawliteral(!</h1>
        <p>You have successfully logged in to the ESP32 interface.</p>
        <p>Current registered users: )rawliteral" + String(userCount) + R"rawliteral(</p>
      </div>
    </body>
    </html>
  )rawliteral");
}

void processSignup(WiFiClient &client, String request) {
  // Parse POST data
  String body = request.substring(request.indexOf("\r\n\r\n") + 4);
  String username = extractFormValue(body, "username");
  String password = extractFormValue(body, "password");
  String confirmPassword = extractFormValue(body, "confirm_password");
  
  // Debug output
  Serial.println("=== SIGNUP DEBUG ===");
  Serial.println("Full request body: " + body);
  Serial.println("Extracted username: '" + username + "' (length: " + String(username.length()) + ")");
  Serial.println("Extracted password: '" + password + "' (length: " + String(password.length()) + ")");
  Serial.println("Extracted confirm: '" + confirmPassword + "' (length: " + String(confirmPassword.length()) + ")");
  Serial.println("==================");
  
  // Validate input
  if (username.length() < 3 || password.length() < 6) {
    sendErrorPage(client, "Username must be at least 3 characters and password at least 6 characters.");
    return;
  }
  
  if (password != confirmPassword) {
    sendErrorPage(client, "Passwords do not match.");
    return;
  }
  
  if (userExists(username)) {
    sendErrorPage(client, "Username already exists.");
    return;
  }
  
  if (userCount >= MAX_USERS) {
    sendErrorPage(client, "Maximum number of users reached.");
    return;
  }
  
  // Add user
  if (addUser(username, password)) {
    sendSuccessPage(client, "Account created successfully! You can now login.");
  } else {
    sendErrorPage(client, "Failed to create account.");
  }
}

void processLogin(WiFiClient &client, String request) {
  String body = request.substring(request.indexOf("\r\n\r\n") + 4);
  String username = extractFormValue(body, "username");
  String password = extractFormValue(body, "password");
  
  if (authenticateUser(username, password)) {
    // Set a simple session (in real implementation, you'd use proper session management)
    sendWelcomePage(client, username);
  } else {
    sendErrorPage(client, "Invalid username or password.");
  }
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

void sendErrorPage(WiFiClient &client, String message) {
  client.println("HTTP/1.1 400 Bad Request");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println(R"rawliteral(
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

void sendSuccessPage(WiFiClient &client, String message) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println(R"rawliteral(
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

void handleSignupNew(WiFiClient &client, String request, String method, String body) {
  if (method == "GET") {
    sendSignupPage(client);
  } else if (method == "POST") {
    processSignupNew(client, body);
  }
}

void handleLoginNew(WiFiClient &client, String request, String method, String body) {
  if (method == "GET") {
    sendLoginPage(client);
  } else if (method == "POST") {
    processLoginNew(client, body);
  }
}

void processSignupNew(WiFiClient &client, String body) {
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
    sendErrorPage(client, "Username must be at least 3 characters and password at least 6 characters. Got: username='" + username + "' password='" + password + "'");
    return;
  }
  
  if (password != confirmPassword) {
    sendErrorPage(client, "Passwords do not match.");
    return;
  }
  
  if (userExists(username)) {
    sendErrorPage(client, "Username already exists.");
    return;
  }
  
  if (userCount >= MAX_USERS) {
    sendErrorPage(client, "Maximum number of users reached.");
    return;
  }
  
  // Add user
  if (addUser(username, password)) {
    sendSuccessPage(client, "Account created successfully! You can now login.");
  } else {
    sendErrorPage(client, "Failed to create account.");
  }
}

void processLoginNew(WiFiClient &client, String body) {
  String username = extractFormValue(body, "username");
  String password = extractFormValue(body, "password");
  
  if (authenticateUser(username, password)) {
    sendWelcomePage(client, username);
  } else {
    sendErrorPage(client, "Invalid username or password.");
  }
}

void loadUsersFromEEPROM() {
  userCount = EEPROM.read(0);
  if (userCount > MAX_USERS) userCount = 0; // Reset if corrupted
  
  for (int i = 0; i < userCount; i++) {
    int baseAddr = 1 + (i * sizeof(User));
    EEPROM.get(baseAddr, users[i]);
  }
}

void saveUsersToEEPROM() {
  EEPROM.write(0, userCount);
  for (int i = 0; i < userCount; i++) {
    int baseAddr = 1 + (i * sizeof(User));
    EEPROM.put(baseAddr, users[i]);
  }
  EEPROM.commit();
}
