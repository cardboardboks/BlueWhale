#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>

// Define your custom Wi-Fi network credentials
const char *ssid = "ESP32C3_Host";
const char *password = "12345678"; // Must be at least 8 characters

// Start a web server on port 80
WiFiServer server(80);

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nConfiguring Access Point...");

  // Set the Wi-Fi mode to Access Point
  WiFi.mode(WIFI_AP);

  // Start the Access Point
  // Remove the 'password' parameter if you want an open network
  if (WiFi.softAP(ssid, password)) {
    Serial.println("Access Point successfully started!");
  } else {
    Serial.println("Access Point configuration failed.");
  }

  // Get and print the host IP address (Default is usually 192.168.4.1)
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP Address: ");
  Serial.println(myIP);

  // Start the server to listen for incoming connections
  server.begin();
  Serial.println("Server started.");
}

void loop() {
  // Listen for incoming clients (e.g., your phone browsing to the IP)
  WiFiClient client = server.available(); 
  
  if (client) {                             
    Serial.println("New Client connected.");           
    String currentLine = "";                
    
    while (client.connected()) {            
      if (client.available()) {             
        char c = client.read();             
        Serial.write(c);                    
        
        if (c == '\n') {                    
          // If the current line is blank, you got two newline characters in a row.
          // That's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // The content of the HTTP response
            client.print("<h1>Hello from ESP32-C3 Host!</h1>");
            client.print("<p>Connected clients count: ");
            client.print(WiFi.softAPgetStationNum()); // Prints number of connected devices
            client.print("</p>");
            
            // The HTTP response ends with another blank line
            client.println();
            break;
          } else {    
            currentLine = "";
          }
        } else if (c != '\r') {  
          currentLine += c;      
        }
      }
    }
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
  }
}