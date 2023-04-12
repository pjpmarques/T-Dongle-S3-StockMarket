
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
void comma_separator(int num, char *str, char sep) {
    char temp[BUF_SIZE];
    int i = 0, j = 0;
    sprintf(temp, "%d", num);
    int len = strlen(temp);
    int k = len % 3;
    if (k == 0) {
        k = 3;
    }
    while (temp[i] != '\0') {
        if (i == k) {
            str[j++] = sep;
            k += 3;
        }
        str[j++] = temp[i++];
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

// Use Yahoo Finance to get the relevant quotes from the internet
void getQuotes() {
  // Use Yahoo Finance API to get the current value of S&P500 and NASDAQ
  HTTPClient http;
  http.begin("https://query1.finance.yahoo.com/v7/finance/quote?symbols=%5ESPX,%5ENDX,%5ETNX");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    // Parse JSON data
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      Serial.println("--------------------------------------------");
      spx.current = doc["quoteResponse"]["result"][0]["regularMarketPrice"];
      spx.previousClose = doc["quoteResponse"]["result"][0]["regularMarketPreviousClose"];
      spx.percentageChange = doc["quoteResponse"]["result"][0]["regularMarketChangePercent"];
      spx.marketOpen = strcmp(doc["quoteResponse"]["result"][0]["marketState"], "REGULAR") == 0 ? true : false;

      ndx.current = doc["quoteResponse"]["result"][1]["regularMarketPrice"];
      ndx.previousClose = doc["quoteResponse"]["result"][1]["regularMarketPreviousClose"];
      ndx.percentageChange = doc["quoteResponse"]["result"][1]["regularMarketChangePercent"];
      ndx.marketOpen = strcmp(doc["quoteResponse"]["result"][1]["marketState"], "REGULAR") == 0 ? true : false;
      
      bnd.current = (double)(doc["quoteResponse"]["result"][2]["regularMarketPrice"]) * 1000.0;
      bnd.previousClose = (double)(doc["quoteResponse"]["result"][2]["regularMarketPreviousClose"]) * 1000.0;
      bnd.percentageChange = doc["quoteResponse"]["result"][2]["regularMarketChangePercent"];
      bnd.marketOpen = strcmp(doc["quoteResponse"]["result"][2]["marketState"], "REGULAR") == 0 ? true : false;
      
      Serial.printf("SPX \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", spx.current, spx.previousClose, spx.percentageChange, spx.marketOpen);
      Serial.printf("NDX \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", ndx.current, ndx.previousClose, ndx.percentageChange, ndx.marketOpen);
      Serial.printf("T10 \t %8.1f from %8.1f \t (%+.1f%%) MarketOpen=%d\n", bnd.current, bnd.previousClose, bnd.percentageChange, bnd.marketOpen);
    } else {
      Serial.println("Error deserializing data: ");
      Serial.println(error.f_str());
    }    
  } else {
    Serial.println("Error getting data from Yahoo.");
  }

  http.end();
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
  comma_separator(symbol.current, buf, sep);
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
