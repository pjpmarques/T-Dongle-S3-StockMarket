#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP_WiFiManager.h>

#include "pin_config.h"

// ------------------------------------------------------------------------------------
const int TFT_FONT = 4;           // Font to use on the TFT
const int BUF_SIZE = 80;
const int DELAY = 2000;           // Display things on the TFT for 2 seconds

TFT_eSPI tft;                     // The TFT object
int black_width;                  // Width of the rectagle that needs to be cleared when stocks update
// ------------------------------------------------------------------------------------

// Given a number convert it to a thousands separated string using a specific separating character
void comma_separator(double num, char *str, char sep, int decimals = 0) {
    char temp[BUF_SIZE];
    int i = 0, j = 0;
    char format[10];
    sprintf(format, "%%0.%df", decimals);
    sprintf(temp, format, num);
    int len = strlen(temp);
    int decimalPos = -1;
    
    // Find decimal point position
    for (i = 0; i < len; i++) {
        if (temp[i] == '.') {
            decimalPos = i;
            break;
        }
    }
    
    // Handle the whole number part
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

// ------------------------------------------------------------------------------------

// Initialize the ESP32
void setup() {
  // Serial port and TFT init
  Serial.begin(115200);
  tft.init();
  tft.setTextFont(7);
  tft.fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BLACK);
  tft.setRotation(1);
  // Turn off LCD backlight
  pinMode(TFT_LEDA_PIN, OUTPUT);  
  digitalWrite(TFT_LEDA_PIN, 0);    
  
  // Write initial diagnose to serial port
  Serial.println("");
  Serial.println("Hello, this is T-Dongle-S3 providing stock market information.");
  Serial.println("I'm alive and well.");
  Serial.println("");

  // Connect to Wi-Fi network
  WiFi.mode(WIFI_STA);
  ESP_WiFiManager wifiManager;  
  //wifiManager.resetSettings();
  
  bool ok = true;
  do {
    Serial.println("Connecting to wifi...");
    ok = wifiManager.autoConnect("T-Dongle-S3");
    if (!ok) {
      Serial.println("Failled to connect to wifi. Retrying.");
      delay(DELAY);
    } else {
      Serial.printf("Connected to wifi <%s>.\n", WiFi.SSID());
    }
  } while (!ok);

  // Inital text screen setup
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("SPX", 0, 0, TFT_FONT);
  tft.drawString("NDX", 0, tft.fontHeight(TFT_FONT), TFT_FONT);
  tft.drawString("T10", 0, tft.fontHeight(TFT_FONT)*2, TFT_FONT);
  tft.setTextDatum(TR_DATUM);
  black_width = tft.textWidth("XXXXXXX");
}

// ------------------------------------------------------------------------------------

// A structure that represents a stock quote with its value, previous close, change and if the market is open
typedef struct {
  double current;
  double previousClose;
  double percentageChange;
  bool marketOpen;
} quote;

// We are going to have three stock quotes (S&P500, NASDAQ100 and T-Bill 10 years)
quote spx, ndx, bnd;

// Use Yahoo Finance API to get the relevant quotes from the internet
void getQuotes() {
  HTTPClient http;
  
  // Get S&P500 data
  Serial.println("Fetching SPX data...");
  http.begin("https://query2.finance.yahoo.com/v8/finance/chart/^GSPC?interval=1d&range=1d");
  http.addHeader("User-Agent", "Mozilla/5.0");
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Got SPX response");
    
    // Find the meta section
    int metaStart = payload.indexOf("\"meta\":{");
    if (metaStart != -1) {
      int metaEnd = payload.indexOf("}", metaStart);
      if (metaEnd != -1) {
        String metaSection = payload.substring(metaStart, metaEnd + 1);
        Serial.println("Meta section found:");
        Serial.println(metaSection);
        
        // Extract values using string manipulation
        int priceStart = metaSection.indexOf("\"regularMarketPrice\":") + 20;
        int priceEnd = metaSection.indexOf(",", priceStart);
        int prevCloseStart = metaSection.indexOf("\"chartPreviousClose\":") + 20;
        int prevCloseEnd = metaSection.indexOf(",", prevCloseStart);
        
        if (priceStart > 20 && priceEnd > priceStart && prevCloseStart > 20 && prevCloseEnd > prevCloseStart) {
          String priceStr = metaSection.substring(priceStart, priceEnd);
          String prevCloseStr = metaSection.substring(prevCloseStart, prevCloseEnd);
          
          // Clean up the strings by removing any whitespace, quotes, and colons
          priceStr.trim();
          prevCloseStr.trim();
          if (priceStr.startsWith("\"")) priceStr = priceStr.substring(1);
          if (priceStr.endsWith("\"")) priceStr = priceStr.substring(0, priceStr.length() - 1);
          if (prevCloseStr.startsWith("\"")) prevCloseStr = prevCloseStr.substring(1);
          if (prevCloseStr.endsWith("\"")) prevCloseStr = prevCloseStr.substring(0, prevCloseStr.length() - 1);
          if (priceStr.startsWith(":")) priceStr = priceStr.substring(1);
          if (prevCloseStr.startsWith(":")) prevCloseStr = prevCloseStr.substring(1);
          
          Serial.printf("Debug - Price string: '%s', PrevClose string: '%s'\n", priceStr.c_str(), prevCloseStr.c_str());
          
          spx.current = priceStr.toDouble();
          spx.previousClose = prevCloseStr.toDouble();
          spx.percentageChange = ((spx.current - spx.previousClose) / spx.previousClose) * 100.0;
          spx.marketOpen = true;
          Serial.println("SPX data parsed successfully");
        } else {
          Serial.println("SPX data parsing failed - couldn't find values in meta section");
          Serial.printf("Debug - priceStart: %d, priceEnd: %d, prevCloseStart: %d, prevCloseEnd: %d\n", 
                       priceStart, priceEnd, prevCloseStart, prevCloseEnd);
        }
      } else {
        Serial.println("Could not find end of meta section");
      }
    } else {
      Serial.println("Could not find meta section");
    }
  } else {
    Serial.printf("SPX HTTP request failed, error: %d\n", httpCode);
  }
  http.end();
  delay(1000);

  // Get NASDAQ data
  Serial.println("Fetching NDX data...");
  http.begin("https://query2.finance.yahoo.com/v8/finance/chart/^NDX?interval=1d&range=1d");
  http.addHeader("User-Agent", "Mozilla/5.0");
  httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Got NDX response");
    
    // Find the meta section
    int metaStart = payload.indexOf("\"meta\":{");
    if (metaStart != -1) {
      int metaEnd = payload.indexOf("}", metaStart);
      if (metaEnd != -1) {
        String metaSection = payload.substring(metaStart, metaEnd + 1);
        Serial.println("Meta section found:");
        Serial.println(metaSection);
        
        // Extract values using string manipulation
        int priceStart = metaSection.indexOf("\"regularMarketPrice\":") + 20;
        int priceEnd = metaSection.indexOf(",", priceStart);
        int prevCloseStart = metaSection.indexOf("\"chartPreviousClose\":") + 20;
        int prevCloseEnd = metaSection.indexOf(",", prevCloseStart);
        
        if (priceStart > 20 && priceEnd > priceStart && prevCloseStart > 20 && prevCloseEnd > prevCloseStart) {
          String priceStr = metaSection.substring(priceStart, priceEnd);
          String prevCloseStr = metaSection.substring(prevCloseStart, prevCloseEnd);
          
          // Clean up the strings by removing any whitespace, quotes, and colons
          priceStr.trim();
          prevCloseStr.trim();
          if (priceStr.startsWith("\"")) priceStr = priceStr.substring(1);
          if (priceStr.endsWith("\"")) priceStr = priceStr.substring(0, priceStr.length() - 1);
          if (prevCloseStr.startsWith("\"")) prevCloseStr = prevCloseStr.substring(1);
          if (prevCloseStr.endsWith("\"")) prevCloseStr = prevCloseStr.substring(0, prevCloseStr.length() - 1);
          if (priceStr.startsWith(":")) priceStr = priceStr.substring(1);
          if (prevCloseStr.startsWith(":")) prevCloseStr = prevCloseStr.substring(1);
          
          Serial.printf("Debug - Price string: '%s', PrevClose string: '%s'\n", priceStr.c_str(), prevCloseStr.c_str());
          
          ndx.current = priceStr.toDouble();
          ndx.previousClose = prevCloseStr.toDouble();
          ndx.percentageChange = ((ndx.current - ndx.previousClose) / ndx.previousClose) * 100.0;
          ndx.marketOpen = true;
          Serial.println("NDX data parsed successfully");
        } else {
          Serial.println("NDX data parsing failed - couldn't find values in meta section");
          Serial.printf("Debug - priceStart: %d, priceEnd: %d, prevCloseStart: %d, prevCloseEnd: %d\n", 
                       priceStart, priceEnd, prevCloseStart, prevCloseEnd);
        }
      } else {
        Serial.println("Could not find end of meta section");
      }
    } else {
      Serial.println("Could not find meta section");
    }
  } else {
    Serial.printf("NDX HTTP request failed, error: %d\n", httpCode);
  }
  http.end();
  delay(1000);

  // Get 10-year Treasury data
  Serial.println("Fetching T10 data...");
  http.begin("https://query2.finance.yahoo.com/v8/finance/chart/^TNX?interval=1d&range=1d");
  http.addHeader("User-Agent", "Mozilla/5.0");
  httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Got T10 response");
    
    // Find the meta section
    int metaStart = payload.indexOf("\"meta\":{");
    if (metaStart != -1) {
      int metaEnd = payload.indexOf("}", metaStart);
      if (metaEnd != -1) {
        String metaSection = payload.substring(metaStart, metaEnd + 1);
        Serial.println("Meta section found:");
        Serial.println(metaSection);
        
        // Extract values using string manipulation
        int priceStart = metaSection.indexOf("\"regularMarketPrice\":") + 20;
        int priceEnd = metaSection.indexOf(",", priceStart);
        int prevCloseStart = metaSection.indexOf("\"chartPreviousClose\":") + 20;
        int prevCloseEnd = metaSection.indexOf(",", prevCloseStart);
        
        if (priceStart > 20 && priceEnd > priceStart && prevCloseStart > 20 && prevCloseEnd > prevCloseStart) {
          String priceStr = metaSection.substring(priceStart, priceEnd);
          String prevCloseStr = metaSection.substring(prevCloseStart, prevCloseEnd);
          
          // Clean up the strings by removing any whitespace, quotes, and colons
          priceStr.trim();
          prevCloseStr.trim();
          if (priceStr.startsWith("\"")) priceStr = priceStr.substring(1);
          if (priceStr.endsWith("\"")) priceStr = priceStr.substring(0, priceStr.length() - 1);
          if (prevCloseStr.startsWith("\"")) prevCloseStr = prevCloseStr.substring(1);
          if (prevCloseStr.endsWith("\"")) prevCloseStr = prevCloseStr.substring(0, prevCloseStr.length() - 1);
          if (priceStr.startsWith(":")) priceStr = priceStr.substring(1);
          if (prevCloseStr.startsWith(":")) prevCloseStr = prevCloseStr.substring(1);
          
          Serial.printf("Debug - Price string: '%s', PrevClose string: '%s'\n", priceStr.c_str(), prevCloseStr.c_str());
          
          bnd.current = priceStr.toDouble();
          bnd.previousClose = prevCloseStr.toDouble();
          bnd.percentageChange = ((bnd.current - bnd.previousClose) / bnd.previousClose) * 100.0;
          bnd.marketOpen = true;
          Serial.println("T10 data parsed successfully");
        } else {
          Serial.println("T10 data parsing failed - couldn't find values in meta section");
          Serial.printf("Debug - priceStart: %d, priceEnd: %d, prevCloseStart: %d, prevCloseEnd: %d\n", 
                       priceStart, priceEnd, prevCloseStart, prevCloseEnd);
        }
      } else {
        Serial.println("Could not find end of meta section");
      }
    } else {
      Serial.println("Could not find meta section");
    }
  } else {
    Serial.printf("T10 HTTP request failed, error: %d\n", httpCode);
  }
  http.end();

  Serial.println("--------------------------------------------");
  Serial.printf("SPX \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", spx.current, spx.previousClose, spx.percentageChange, spx.marketOpen);
  Serial.printf("NDX \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", ndx.current, ndx.previousClose, ndx.percentageChange, ndx.marketOpen);
  Serial.printf("T10 \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", bnd.current, bnd.previousClose, bnd.percentageChange, bnd.marketOpen);
}

// Write a stock quote to the TFT screen at a certain vertical position.
// The separator char is use to provide scaling in case of showing thousands or millis
void drawQuote(const quote& symbol, int pos, char sep) {
  // Set drawing colour according to market state and if the stock is up or down
  if (symbol.marketOpen == false) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  } else if (symbol.current > symbol.previousClose) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } else if (symbol.current == symbol.previousClose) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }

  // Actually write the stock value to the TFT
  char buf[BUF_SIZE];
  if (pos == 2) { // T10
    comma_separator(symbol.current, buf, sep, 4);
  } else { // SPX and NDX
    comma_separator(symbol.current, buf, sep, 0);
  }
  tft.drawString(buf, TFT_HEIGHT, tft.fontHeight(TFT_FONT)*pos, TFT_FONT);
}

void drawPercentChange(const quote& symbol, int pos) {
  // Set drawing colour according to market state and if the stock is up or down
  if (symbol.marketOpen == false) { 
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  } else if (symbol.percentageChange > 0.0) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } else if (abs(symbol.percentageChange) < 0.001) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }

  // Actually write the stock percentage change from the previous day to the TFT
  char buf[BUF_SIZE];
  sprintf(buf, "%+.1f%%", symbol.percentageChange);
  tft.drawString(buf, TFT_HEIGHT, tft.fontHeight(TFT_FONT)*pos, TFT_FONT);
}

// Main looop showing the quotes on the TFT screen
void loop() {
  getQuotes();

  tft.fillRect(TFT_HEIGHT-black_width, 0, black_width, TFT_HEIGHT, TFT_BLACK);
  drawQuote(spx, 0, ',');
  drawQuote(ndx, 1, ',');
  drawQuote(bnd, 2, '.');

  delay(DELAY);

  tft.fillRect(TFT_HEIGHT-black_width, 0, black_width, TFT_HEIGHT, TFT_BLACK);
  drawPercentChange(spx, 0);
  drawPercentChange(ndx, 1);
  drawPercentChange(bnd, 2);

  delay(DELAY);
}
