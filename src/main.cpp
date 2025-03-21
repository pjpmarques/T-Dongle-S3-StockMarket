#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP_WiFiManager.h>

#include "pin_config.h"

// ------------------------------------------------------------------------------------
// Constants
const int TFT_FONT = 4;           // Font to use on the TFT
const int BUF_SIZE = 80;          // Buffer size for string operations
const int DELAY = 2000;           // Display things on the TFT for 2 seconds (2000ms)
const char* USER_AGENT = "Mozilla/5.0";  // User agent for HTTP requests

// Global objects
TFT_eSPI tft;                     // The TFT object
int black_width;                  // Width of the rectangle that needs to be cleared when stocks update

// ------------------------------------------------------------------------------------
// Data structures

// A structure that represents a stock quote with its value, previous close, change and if the market is open
typedef struct {
  double current;
  double previousClose;
  double percentageChange;
  bool marketOpen;
} quote;

// Stock quotes (S&P500, NASDAQ100 and T-Bill 10 years)
quote spx, ndx, bnd;

// ------------------------------------------------------------------------------------
// Helper functions

// Format a number with thousands separator and optional decimal places
void comma_separator(double num, char *str, char sep, int decimals = 0) {
    char temp[BUF_SIZE];
    int i = 0, j = 0;
    
    // Create format string and format the number
    char format[10];
    sprintf(format, "%%0.%df", decimals);
    sprintf(temp, format, num);
    
    // Find decimal point position
    int len = strlen(temp);
    int decimalPos = -1;
    for (i = 0; i < len; i++) {
        if (temp[i] == '.') {
            decimalPos = i;
            break;
        }
    }
    
    // Handle the whole number part with thousands separators
    i = 0;
    int k = (decimalPos != -1 ? decimalPos : len) % 3;
    if (k == 0) k = 3;
    
    while (i < (decimalPos != -1 ? decimalPos : len)) {
        if (i == k) {
            str[j++] = sep;
            k += 3;
        }
        str[j++] = temp[i++];
    }
    
    // Add decimal part if needed
    if (decimalPos != -1) {
        str[j++] = '.';
        i = decimalPos + 1;
        while (i < len && (i - decimalPos) <= decimals) {
            str[j++] = temp[i++];
        }
    }
    
    str[j] = '\0';
}

// Extract a numeric value from JSON response
double extractValue(const String& payload, const char* field) {
  int start = payload.indexOf(field) + strlen(field);
  if (start > strlen(field)) {
    int end = payload.indexOf(",", start);
    if (end > start) {
      String value = payload.substring(start, end);
      value.trim();
      if (value.startsWith(":")) value = value.substring(1);
      return value.toDouble();
    }
  }
  return 0.0;
}

// Fetch and parse quote data from Yahoo Finance
bool fetchQuote(HTTPClient& http, WiFiClientSecure& client, const char* symbol, quote& data) {
  Serial.printf("Fetching %s data...\n", symbol);
  
  // Build URL and initialize HTTP request
  char url[128];
  snprintf(url, sizeof(url), "https://query2.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=1d", symbol);
  
  if (!http.begin(client, url)) {
    Serial.printf("Failed to begin HTTP request for %s\n", symbol);
    return false;
  }
  
  http.addHeader("User-Agent", USER_AGENT);
  int httpCode = http.GET();
  
  // Process response
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.printf("Got %s response\n", symbol);
    
    // Extract data from the meta section
    int metaStart = payload.indexOf("\"meta\":{");
    if (metaStart != -1) {
      int metaEnd = payload.indexOf("}", metaStart);
      if (metaEnd != -1) {
        String metaSection = payload.substring(metaStart, metaEnd + 1);
        
        // Parse the required fields
        data.current = extractValue(metaSection, "\"regularMarketPrice\":");
        data.previousClose = extractValue(metaSection, "\"chartPreviousClose\":");
        
        // Calculate percentage change
        if (data.previousClose != 0.0) {
          data.percentageChange = ((data.current - data.previousClose) / data.previousClose) * 100.0;
        } else {
          data.percentageChange = 0.0;
        }
        
        data.marketOpen = true;
        Serial.printf("%s data parsed successfully\n", symbol);
        return true;
      }
    }
  }
  
  Serial.printf("Failed to get data for %s (HTTP code: %d)\n", symbol, httpCode);
  return false;
}

// Fetch all quotes from Yahoo Finance
void getQuotes() {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification
  
  // Fetch each quote with error handling
  bool spxSuccess = fetchQuote(http, client, "^SPX", spx);
  http.end();
  delay(1000);
  
  bool ndxSuccess = fetchQuote(http, client, "^NDX", ndx);
  http.end();
  delay(1000);
  
  bool bndSuccess = fetchQuote(http, client, "^TNX", bnd);
  http.end();

  // Print results to serial
  if (spxSuccess && ndxSuccess && bndSuccess) {
    Serial.println("--------------------------------------------");
    Serial.printf("SPX \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", spx.current, spx.previousClose, spx.percentageChange, spx.marketOpen);
    Serial.printf("NDX \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", ndx.current, ndx.previousClose, ndx.percentageChange, ndx.marketOpen);
    Serial.printf("T10 \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", bnd.current, bnd.previousClose, bnd.percentageChange, bnd.marketOpen);
  } else {
    Serial.println("Failed to fetch some or all quotes");
  }
}

// Display a stock quote on the TFT at the specified position
void drawQuote(const quote& symbol, int pos, char sep) {
  // Set color based on market state and price movement
  if (!symbol.marketOpen) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  } else if (symbol.current > symbol.previousClose) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } else if (symbol.current == symbol.previousClose) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }

  // Format and display the value
  char buf[BUF_SIZE];
  int decimals = (pos == 2) ? 4 : 0; // 4 decimal places for T10, 0 for others
  comma_separator(symbol.current, buf, sep, decimals);
  tft.drawString(buf, TFT_HEIGHT, tft.fontHeight(TFT_FONT) * pos, TFT_FONT);
}

// Display the percentage change on the TFT at the specified position
void drawPercentChange(const quote& symbol, int pos) {
  // Set color based on market state and change direction
  if (!symbol.marketOpen) { 
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  } else if (symbol.percentageChange > 0.0) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } else if (abs(symbol.percentageChange) < 0.001) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }

  // Format and display the percentage change
  char buf[BUF_SIZE];
  sprintf(buf, "%+.1f%%", symbol.percentageChange);
  tft.drawString(buf, TFT_HEIGHT, tft.fontHeight(TFT_FONT) * pos, TFT_FONT);
}

// ------------------------------------------------------------------------------------
// Setup function - runs once at startup
void setup() {
  // Initialize serial and TFT
  Serial.begin(115200);
  tft.init();
  tft.setTextFont(7);
  tft.fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BLACK);
  tft.setRotation(1);
  
  // Turn off LCD backlight
  pinMode(TFT_LEDA_PIN, OUTPUT);  
  digitalWrite(TFT_LEDA_PIN, 0);    
  
  // Print welcome message
  Serial.println("");
  Serial.println("Hello, this is T-Dongle-S3 providing stock market information.");
  Serial.println("I'm alive and well.");
  Serial.println("");

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  ESP_WiFiManager wifiManager;  
  //wifiManager.resetSettings(); // Uncomment to reset saved WiFi credentials
  
  bool connected = false;
  do {
    Serial.println("Connecting to WiFi...");
    connected = wifiManager.autoConnect("T-Dongle-S3");
    if (!connected) {
      Serial.println("Failed to connect to WiFi. Retrying.");
      delay(DELAY);
    } else {
      Serial.printf("Connected to WiFi <%s>.\n", WiFi.SSID().c_str());
    }
  } while (!connected);

  // Setup TFT display
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("SPX", 0, 0, TFT_FONT);
  tft.drawString("NDX", 0, tft.fontHeight(TFT_FONT), TFT_FONT);
  tft.drawString("T10", 0, tft.fontHeight(TFT_FONT) * 2, TFT_FONT);
  tft.setTextDatum(TR_DATUM);
  black_width = tft.textWidth("XXXXXXX");
}

// ------------------------------------------------------------------------------------
// Main loop
void loop() {
  // Get stock quotes
  getQuotes();

  // Display stock values
  tft.fillRect(TFT_HEIGHT - black_width, 0, black_width, TFT_HEIGHT, TFT_BLACK);
  drawQuote(spx, 0, ',');
  drawQuote(ndx, 1, ',');
  drawQuote(bnd, 2, '.');

  delay(DELAY);

  // Display percentage changes
  tft.fillRect(TFT_HEIGHT - black_width, 0, black_width, TFT_HEIGHT, TFT_BLACK);
  drawPercentChange(spx, 0);
  drawPercentChange(ndx, 1);
  drawPercentChange(bnd, 2);

  delay(DELAY);
}
