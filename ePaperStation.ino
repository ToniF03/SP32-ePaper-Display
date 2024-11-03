#include <ArduinoJson.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <CTBot.h>
#include <GxEPD2_3C.h>
#include <InterpolationLib.h>
#include <math.h>
#include <time.h>
#include <TimeLib.h>
#include <WiFi.h>
#include "resources/icons/icons.h"
#include "resources/fonts/fonts.h"

#define host "api.openweathermap.org"
#define TELEGRAM_KEY "YOUR_TELEGRAM_KEY"
#define SEGMENTS 20
#define vRef 0.193446275

GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / 2> display(GxEPD2_750c_Z08(SS, 17, 16, 4));
WiFiClient client;
CTBot myBot;
Adafruit_ADS1115 ads;

float batPercentage = 0.0;
float Voltage = 0.0;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;
const int daylightOffset_sec = 3600 * 0;

const char *ssid = "PP WeatherStation";
const char *password = "FqJMFw851IMBgdsv";

void setupWiFi() {
  WiFi.setHostname("PixelPioneer ePaper");
  WiFi.begin("YOUR_SSID", "YOUR_PASS");
  float connectionBegin = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    if (millis() - connectionBegin >= 60000) {
      failedConnection();
      goDeepSleep();
    }
  }
  Serial.println();
  Serial.println("--------------------------------");
  Serial.println();
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("Connected as ");
  Serial.println(WiFi.getHostname());
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC-Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connection Strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.println();
  Serial.println("--------------------------------");
  delay(10);
}

void setup() {
  Serial.begin(115200);
  delay(10);

  display.init();
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);

  display.firstPage();

  //failedConnection();
  setupWiFi();

  ads.setGain(GAIN_TWOTHIRDS);
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    goDeepSleep();
  }
  Serial.println("ADS started");
  int16_t adc0;

  adc0 = ads.readADC_SingleEnded(0);
  Voltage = (adc0 * vRef) / 1000;
  batPercentage = calcBatPercent(Voltage, 3.27, 4.2);

  Serial.print("AIN0: ");
  Serial.print(adc0);
  Serial.print("\tVoltage: ");
  Serial.print(Voltage, 7);
  Serial.print("\tPercentage: ");
  Serial.println(calcBatPercent(Voltage, 3.27, 4.2));
  Serial.println();
  myBot.setTelegramToken(TELEGRAM_KEY);
  if (batPercentage <= 10)
    lowBattery();

  adjustTime(7200);
}

void fillPolygon(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, int16_t x4, int16_t y4, uint16_t color) {
  display.fillTriangle(x1, y1, x2, y2, x3, y3, color);
  display.fillTriangle(x2, y2, x3, y3, x4, y4, color);
}

void loop() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    failedTimeFetch();
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");


  if (!client.connect(host, 80))
    failedDataFetch();
  Serial.println("Verbunden mit OpenWeatherMap");
  client.print(String("GET /data/2.5/weather?lat=49.630638&lon=8.357910&units=metric&appid=YOUR_APP_ID") + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");

  String response = "";
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += client.readString();  // Antwort als String speichern
    }
  }
  client.stop();

  // JSON aus der Antwort parsen
  int jsonStart = response.indexOf("\r\n\r\n");  // JSON-Daten beginnen nach dem Header
  String jsonData = response.substring(jsonStart);

  // ArduinoJson: JsonDokument erstellen und Wetterdaten extrahieren
  DynamicJsonDocument doc(50000);
  DeserializationError error = deserializeJson(doc, jsonData);

  if (error)
    Serial.println("Error occured!");
  float temp = doc["main"]["temp"];
  float feelsLike = doc["main"]["feels_like"];
  int picId = doc["weather"][0]["id"];
  int clouds = doc["clouds"]["all"];
  float wind_speed = doc["wind"]["speed"];  // m/s
  float wind_gust = doc["wind"]["gust"];
  int wind_deg = doc["wind"]["deg"];
  int humidity = doc["main"]["humidity"];
  String city = doc["name"];
  time_t sunriseEpochTime = doc["sys"]["sunrise"];
  time_t sunsetEpochTime = doc["sys"]["sunset"];
  float visibility = doc["visibility"];
  visibility = visibility / 1000;

  if (!client.connect(host, 80))
    failedDataFetch();
  client.print(String("GET /data/2.5/uvi?lat=49.630638&lon=8.357910&units=metric&appid=YOUR_APP_ID") + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");

  response = "";
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += client.readString();  // Antwort als String speichern
    }
  }
  client.stop();

  jsonStart = response.indexOf("\r\n\r\n");  // JSON-Daten beginnen nach dem Header
  jsonData = response.substring(jsonStart);

  error = deserializeJson(doc, jsonData);

  if (error)
    Serial.println("Error occured!");

  float uvi = doc["value"];

  if (!client.connect(host, 80))
    failedDataFetch();
  client.print(String("GET /data/2.5/forecast?lat=49.630638&lon=8.357910&units=metric&appid=YOUR_APP_ID") + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");

  response = "";
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += client.readString();  // Antwort als String speichern
    }
  }
  client.stop();

  jsonStart = response.indexOf("\r\n\r\n");  // JSON-Daten beginnen nach dem Header
  jsonData = response.substring(jsonStart);

  error = deserializeJson(doc, jsonData);

  if (error)
    Serial.println("Error occured while fetching forecast!");

  float forecast[2][9];
  // pic ID, min, max, clouds, wind speed, wind gust
  float dailyForecast[6][4];
  float minForecast;
  float maxForecast;
  float diagramStep;
  float wind_speed_sum;
  float wind_gust_sum;
  float clouds_sum;
  int a = 1;

  while (String(doc["list"][a]["dt_txt"]).substring(0, 10) == String(doc["list"][0]["dt_txt"]).substring(0, 10))
    a++;

  int dict[2][4];
  for (int picIndex = 0; picIndex < 4; picIndex++) {
    dict[0][picIndex] = 0;
    dict[1][picIndex] = 0;
  }

  for (int i = 0; i < 4; i++) {
    for (int entry = a + i * 8; entry < a + i * 8 + 8; entry++) {
      if (entry == a + i * 8) {
        dailyForecast[1][i] = doc["list"][entry]["main"]["temp"];
        dailyForecast[2][i] = doc["list"][entry]["main"]["temp"];
        clouds_sum = (float)doc["list"][entry]["clouds"]["all"];
        wind_speed_sum = (float)doc["list"][entry]["wind"]["speed"];
        wind_gust_sum = (float)doc["list"][entry]["wind"]["gust"];
      } else {
        clouds_sum += (float)doc["list"][entry]["clouds"]["all"];
        wind_speed_sum += (float)doc["list"][entry]["wind"]["speed"];
        wind_gust_sum += (float)doc["list"][entry]["wind"]["gust"];
      }

      if (dailyForecast[1][i] > doc["list"][entry]["main"]["temp"])
        dailyForecast[1][i] = doc["list"][entry]["main"]["temp"];

      if (dailyForecast[2][i] < doc["list"][entry]["main"]["temp"])
        dailyForecast[2][i] = doc["list"][entry]["main"]["temp"];

      for (int picIndex = 0; picIndex < 4; picIndex++) {
        if (dict[0][picIndex] == 0 || dict[0][picIndex] == (int)doc["list"][entry]["weather"][0]["id"]) {
          dict[0][picIndex] = (int)doc["list"][entry]["weather"][0]["id"];
          dict[1][picIndex] += 1;
          break;
        }
      }
    }

    dailyForecast[3][i] = (float)clouds_sum / 4;
    dailyForecast[4][i] = (float)wind_speed_sum / 4;
    dailyForecast[5][i] = (float)wind_gust_sum / 4;
    int index = 0;
    for (int picIndex = 0; picIndex < 4; picIndex++) {
      if (dict[1][picIndex] > dict[1][index])
        index = picIndex;
    }
    dailyForecast[0][i] = dict[0][index];
    for (int picIndex = 0; picIndex < 4; picIndex++) {
      dict[0][picIndex] = 0;
      dict[1][picIndex] = 0;
    }
  }

  for (int i = 0; i < 9; i++) {
    forecast[0][i] = doc["list"][i]["pop"];
    forecast[1][i] = doc["list"][i]["main"]["temp"];
    forecast[1][i] = round(forecast[1][i]);
    if (i == 0) {
      minForecast = forecast[1][0];
      maxForecast = forecast[1][0];
    }
    minForecast = minForecast > forecast[1][i] ? forecast[1][i] : minForecast;
    maxForecast = maxForecast < forecast[1][i] ? forecast[1][i] : maxForecast;
  }
  diagramStep = (maxForecast - minForecast) / 4;

  const int numValues = 9;
  double xValues[9];
  double yValues[9];
  for (int i = 0; i < 9; i++) {
    xValues[i] = (double)(389 + 376 / 8 * i);
    yValues[i] = (double)round(430 - 200 * (forecast[1][i] - minForecast + diagramStep) / (maxForecast - minForecast + diagramStep));
  }

  do {
    // Lower Bar
    display.setFont(&Roboto_Regular_6pt8b);
    display.fillScreen(GxEPD_WHITE);
    int rssi = WiFi.RSSI();
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(String(batPercentage, 0) + "% (" + String(Voltage, 2) + "v)", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(790 - tbw, 472 + (tbh / 2));
    display.print(String(batPercentage, 0) + "% (" + String(Voltage, 2) + "v)");
    display.fillRect(769 - tbw, 466, 16, 16, GxEPD_BLACK);
    display.drawBitmap(769 - tbw, 466, getBatBitmap16(batPercentage), 16, 16, GxEPD_WHITE);
    String s = getWiFidesc(rssi);
    s += " (";
    s += rssi;
    s += " dBm)";
    display.getTextBounds(s, 749 - tbw, tby, &tbx, &tby, &tbw, &tbh);
    display.setCursor(tbx - tbw, 468 + (tbh / 2));
    display.print(s);
    display.fillRect(tbx - tbw - 21, 466, 16, 16, GxEPD_BLACK);
    display.drawBitmap(tbx - tbw - 21, 466, getWiFiBitmap16(rssi), 16, 16, GxEPD_WHITE);
    char timeString[20];
    char dateString[30];
    strftime(dateString, 10, "%D", &timeinfo);
    strftime(timeString, 10, "%R", &timeinfo);
    display.getTextBounds(String(dateString) + " " + String(timeString), tbx - tbw - 37, tby, &tbx, &tby, &tbw, &tbh);
    display.setCursor(tbx - tbw, 465 + (tbh / 2));
    display.println(String(dateString) + " " + String(timeString));
    display.fillRect(tbx - tbw - 21, 466, 16, 16, GxEPD_BLACK);
    display.drawBitmap(tbx - tbw - 21, 466, wi_refresh_16x16, 16, 16, GxEPD_WHITE);

    // City Name
    display.setFont(&Roboto_Regular_20pt8b);
    display.getTextBounds(city, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(797 - tbw, tbh + 3);
    display.print(city);
    strftime(dateString, 30, "%A, %B %d", &timeinfo);
    display.setFont(&Roboto_Regular_14pt8b);
    display.getTextBounds(dateString, 0, tbh + 3, &tbx, &tby, &tbw, &tbh);
    display.setCursor(797 - tbw, tby + tbh * 2 + 3);
    display.print(dateString);



    display.setFont(&Roboto_Regular_48pt8b_temperature);
    String tempString = String(temp, 0) + char(176);
    display.getTextBounds(tempString, 0, tby, &tbx, &tby, &tbw, &tbh);
    tempString = String(temp, 0);
    display.setCursor(211, 78 + (tbh / 2));
    display.fillRect(211 + tbw / 3 * 2, 78 - tbh / 3 * 2, 64, 64, GxEPD_BLACK);
    display.drawBitmap(211 + tbw / 3 * 2, 78 - tbh / 3 * 2, wi_celsius_64x64, 64, 64, GxEPD_WHITE);
    display.print(tempString);

    display.setFont(&Roboto_Regular_10pt8b);
    String feelsLikeString = "Feels like " + String(feelsLike, 0) + char(176);
    display.getTextBounds(feelsLikeString, 201 + (tbw / 2), 78 + tbh, &tbx, &tby, &tbw, &tbh);
    display.setCursor(tbx - (tbw / 2), tby);
    display.print(feelsLikeString);


    display.fillRect(5, 5, 196, 196, GxEPD_BLACK);
    display.drawBitmap(5, 5, getForecastBitmap196(picId, clouds, wind_speed, wind_gust), 196, 196, GxEPD_WHITE);

    // Diagram
    display.setFont(&Roboto_Regular_6pt8b);
    for (int i = 0; i < 9; i++) {
      display.drawLine(389 + 376 / 8 * i, 430, 389 + 376 / 8 * i, 433, GxEPD_BLACK);
      int hour = timeinfo.tm_hour + 3 * i;
      if (i != 0) {
        hour = timeinfo.tm_hour != hour && hour % 3 == 0 ? hour : hour - hour % 3;
        hour = hour >= 24 ? hour - 24 : hour;
      }
      display.getTextBounds(String(hour), 0, 0, &tbx, &tby, &tbw, &tbh);
      display.setCursor(389 - tbw / 3 * 2 + 376 / 8 * i, 436 + tbh);
      display.print(String(hour));

      if (i < 8)
        if (forecast[0][i] != 0)
          for (int x = 389 + 376 / 8 * i; x <= 389 + 376 / 8 * i + 47; x++) {
            for (int y = 430 - 200 * forecast[0][i]; y <= 430; y++) {
              display.drawPixel(x, y, y % 2 == 0 ? (x % 2 == 0 ? GxEPD_BLACK : GxEPD_WHITE) : (x % 2 == 0 ? GxEPD_WHITE : GxEPD_BLACK));
            }
          }
    }
    display.drawLine(389, 430, 765, 430, GxEPD_BLACK);

    display.getTextBounds("0%", 770, 430, &tbx, &tby, &tbw, &tbh);
    display.setCursor(770, tby + tbh / 3 * 4);
    display.print("0%");
    for (int i = 1; i <= 5; i++) {
      drawDarkGreyLine(387, 430 - 40 * i, 765, 430 - 40 * i);
      display.getTextBounds(String(i * 20) + "%", 770, 430 - 40 * i, &tbx, &tby, &tbw, &tbh);
      display.setCursor(770, tby + tbh / 3 * 4);
      display.print(String(i * 20) + "%");

      display.getTextBounds(String(minForecast + diagramStep * (i - 1), 1) + char(176), 770, 430 - 40 * i, &tbx, &tby, &tbw, &tbh);
      display.setCursor(378 - tbw, tby + tbh / 3 * 4);
      display.print(String(minForecast + diagramStep * (i - 1), 1) + char(176));
    }

    for (float xValue = 389; xValue < 765; xValue += 1) {
      display.drawLine((int)xValue, (int)round(Interpolation::CatmullSpline(xValues, yValues, numValues, xValue)), (int)xValue + 1, (int)round(Interpolation::CatmullSpline(xValues, yValues, numValues, xValue + 1)), GxEPD_RED);
      display.drawLine((int)xValue, (int)round(Interpolation::CatmullSpline(xValues, yValues, numValues, xValue)) - 1, (int)xValue + 1, (int)round(Interpolation::CatmullSpline(xValues, yValues, numValues, xValue + 1)) - 1, GxEPD_RED);
    }

    // Upcoming Days
    for (int i = 0; i < 4; i++) {
      display.fillRect(688 - 112 * i, 92, 96, 96, GxEPD_BLACK);
      display.drawBitmap(688 - 112 * i, 92, getForecastBitmap96(dailyForecast[0][3 - i], dailyForecast[3][3 - i], dailyForecast[4][3 - i], dailyForecast[5][3 - i]), 96, 96, GxEPD_WHITE);
      display.setFont(&Roboto_Regular_8pt8b);
      display.getTextBounds(String(dailyForecast[1][3 - i]) + char(176) + "|" + String(dailyForecast[2][3 - i]) + char(176), 0, 0, &tbx, &tby, &tbw, &tbh);
      display.setCursor(736 - 112 * i - (tbw / 3 * 1), 180 + tbh);
      display.print(String(dailyForecast[1][3 - i], 0) + char(176) + "|" + String(dailyForecast[2][3 - i], 0) + char(176));

      String weekday;
      int weekday_int = timeinfo.tm_wday + 3 - i;
      if (weekday_int > 6) weekday_int -= 6;
      switch (weekday_int) {
        case 0:
          weekday = "Sun";
          break;
        case 1:
          weekday = "Mon";
          break;
        case 2:
          weekday = "Tue";
          break;
        case 3:
          weekday = "Wed";
          break;
        case 4:
          weekday = "Thu";
          break;
        case 5:
          weekday = "Fri";
          break;
        case 6:
          weekday = "Sat";
          break;
      }
      display.getTextBounds(weekday, 0, 0, &tbx, &tby, &tbw, &tbh);
      display.setCursor(736 - 112 * i - (tbw / 3 * 1), 106 - tbh * 1.5);
      display.print(weekday);
    }

    // Sunrise
    display.fillRect(0, 196, 64, 64, GxEPD_BLACK);
    display.drawBitmap(0, 196, wi_sunrise_64x64, 64, 64, GxEPD_WHITE);
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds("Sunrise", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(64, 196 + tbh);
    display.print("Sunrise");
    tmElements_t tm;
    breakTime(sunriseEpochTime + 7200, tm);
    sprintf(timeString, "%02d:%02d", tm.Hour, tm.Minute);
    display.setFont(&Roboto_Regular_16pt8b);
    display.getTextBounds(timeString, 0, tbh / 3 * 4 + 5, &tbx, &tby, &tbw, &tbh);
    display.setCursor(64, 196 + tby + tbh * 2);
    display.print(timeString);

    // Sunset
    display.fillRect(175, 196, 64, 64, GxEPD_BLACK);
    display.drawBitmap(175, 196, wi_sunset_64x64, 64, 64, GxEPD_WHITE);
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds("Sunset", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(239, 196 + tbh);
    display.print("Sunset");
    breakTime(sunsetEpochTime + 7200, tm);
    sprintf(timeString, "%02d:%02d", tm.Hour, tm.Minute);
    display.setFont(&Roboto_Regular_16pt8b);
    display.getTextBounds(timeString, 0, tbh / 3 * 4 + 5, &tbx, &tby, &tbw, &tbh);
    display.setCursor(239, 196 + tby + tbh * 2);
    display.print(timeString);

    // Wind
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds("Wind", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(64, 265 + tbh);
    display.print("Wind");
    display.fillRect(0, 265, 64, 64, GxEPD_BLACK);
    display.fillRect(64, 275 + tbh, 32, 32, GxEPD_BLACK);
    display.drawBitmap(0, 265, wi_strong_wind_64x64, 64, 64, GxEPD_WHITE);
    display.drawBitmap(64, 275 + tbh, getWindBitmap32((int)wind_deg), 32, 32, GxEPD_WHITE);
    display.setFont(&Roboto_Regular_12pt8b);
    display.getTextBounds(String(wind_speed, 1) + "m/s", 0, tbh / 3 * 4 + 5, &tbx, &tby, &tbw, &tbh);
    display.setCursor(96, 275 + tby + tbh * 1.5);
    display.print(String(wind_speed, 1) + "m/s");

    // Humidity
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds("Humidity", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(239, 265 + tbh);
    display.print("Humidity");
    display.setFont(&Roboto_Regular_16pt8b);
    display.getTextBounds(String(humidity, 0) + "%", 0, tbh / 3 * 4 + 5, &tbx, &tby, &tbw, &tbh);
    display.setCursor(239, 265 + tby + tbh * 2);
    display.print(String(humidity) + "%");
    display.fillRect(175, 265, 64, 64, GxEPD_BLACK);
    display.drawBitmap(175, 265, wi_humidity_64x64, 64, 64, GxEPD_WHITE);

    // UVI
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds("UV Index", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(64, 334 + tbh);
    display.print("UV Index");
    display.fillRect(0, 334, 64, 64, GxEPD_BLACK);
    display.drawBitmap(0, 334, wi_day_sunny_64x64, 64, 64, GxEPD_WHITE);
    display.setFont(&Roboto_Regular_16pt8b);
    display.getTextBounds(String(uvi, 0), 0, tbh / 3 * 4 + 5, &tbx, &tby, &tbw, &tbh);
    display.setCursor(55, 334 + tby + tbh * 2);
    display.print(String(uvi, 0));
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds(getUVIdesc(uvi), tbw, 334 + tby + tbh * 2, &tbx, &tby, &tbw, &tbh);
    display.setCursor(64 + tbx, tby + tbh);
    display.print(getUVIdesc(uvi));

    // Visibility
    display.setFont(&Roboto_Regular_8pt8b);
    display.getTextBounds("Humidity", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(239, 334 + tbh);
    display.print("Visibility");
    display.setFont(&Roboto_Regular_16pt8b);
    display.getTextBounds(String(visibility, 0) + " km", 0, tbh / 3 * 4 + 5, &tbx, &tby, &tbw, &tbh);
    display.setCursor(239, 334 + tby + tbh * 2);
    display.print(String(visibility, 0) + " km");
    display.fillRect(175, 334, 64, 64, GxEPD_BLACK);
    display.drawBitmap(175, 334, visibility_icon_64x64, 64, 64, GxEPD_WHITE);
  } while (display.nextPage());


  goDeepSleep();
}

void failedTimeFetch() {
  display.setFont(&Roboto_Regular_11pt8b);
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(302, 142, 196, 196, GxEPD_BLACK);
    display.drawBitmap(302, 142, wi_time_4_196x196, 196, 196, GxEPD_WHITE);

    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds("Failed To Fetch", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), 368);  // 30 px top margin
    display.print("Failed To Fetch");
    display.getTextBounds("The Time", 0, 368 + tbh + 30, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), tby);
    display.print("The Time");
  } while (display.nextPage());
  goDeepSleep();
}

void failedConnection() {
  display.setFont(&Roboto_Regular_11pt8b);
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(302, 142, 196, 196, GxEPD_BLACK);
    display.fillRect(430, 142, 68, 98, GxEPD_RED);
    display.drawBitmap(302, 142, wifi_x_196x196, 196, 196, GxEPD_WHITE);
    
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds("WiFi Connection", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), 368);  // 30 px top margin
    display.print("WiFi Connection");
    display.getTextBounds("failed", 0, 368 + tbh + 30, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), tby);
    display.print("failed");
  } while (display.nextPage());
  goDeepSleep();
}

void failedDataFetch() {
  display.setFont(&Roboto_Regular_11pt8b);
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(302, 142, 196, 196, GxEPD_BLACK);
    display.fillRect(371, 234, 63, 104, GxEPD_RED);
    display.drawBitmap(302, 142, wi_cloud_down_196x196, 196, 196, GxEPD_WHITE);

    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds("Failed To Fetch", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), 368);  // 30 px top margin
    display.print("Failed To Fetch");
    display.getTextBounds("The Data", 0, 368 + tbh + 30, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), tby);
    display.print("The Data");
  } while (display.nextPage());
  goDeepSleep();
}

void lowBattery() {
  display.setFont(&Roboto_Regular_11pt8b);
  myBot.sendMessage(6843856669, "Battery is nearly empty. Please charge!");
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRoundRect(288, 184, 225, 112, 10, GxEPD_BLACK);
    display.fillRoundRect(503, 220, 20, 40, 5, GxEPD_BLACK);
    display.fillRoundRect(298, 194, 205, 92, 10, GxEPD_WHITE);

    fillPolygon(359, 306, 352, 306, 424, 178, 417, 178, GxEPD_WHITE);
    fillPolygon(425, 178, 440, 178, 360, 306, 375, 306, GxEPD_RED);

    display.fillTriangle(376, 306, 383, 306, 441, 178, GxEPD_WHITE);
    display.fillTriangle(448, 178, 383, 306, 441, 178, GxEPD_WHITE);

    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds("Low Battery", 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(400 - (tbw / 2), 350);
    display.print("Low Battery");
  } while (display.nextPage());
  goDeepSleep();
}

// Max ist 4,2
uint32_t calcBatPercent(uint32_t v, uint32_t minv, uint32_t maxv) {
  uint32_t p = 105 - (105 / (1 + pow(1.724 * (v - minv) / (maxv - minv), 5.5)));
  return p >= 100 ? 100 : p;
}

const char *getWiFidesc(int rssi) {
  if (rssi == 0) {
    return "No Connection";
  } else if (rssi >= -50) {
    return "Excellent";
  } else if (rssi >= -60) {
    return "Good";
  } else if (rssi >= -70) {
    return "Fair";
  } else {  // rssi < -70
    return "Weak";
  }
}

const char *getUVIdesc(float uvi) {
  if (uvi <= 2) {
    return "Low";
  } else if (uvi <= 5) {
    return "Moderate";
  } else if (uvi <= 7) {
    return "High";
  } else if (uvi <= 10) {
    return "Very High";
  } else  // uvi >= 11
  {
    return "Extreme";
  }
}

void goDeepSleep() {
  esp_sleep_enable_timer_wakeup(1800000000);
  //esp_sleep_enable_timer_wakeup(120000000);
  Serial.println("ESP goes to deep sleep now");
  Serial.flush();
  esp_deep_sleep_start();
}

const uint8_t *getWiFiBitmap16(int rssi) {
  if (rssi == 0) {
    return wifi_x_16x16;
  } else if (rssi >= -50) {
    return wifi_16x16;
  } else if (rssi >= -60) {
    return wifi_3_bar_16x16;
  } else if (rssi >= -70) {
    return wifi_2_bar_16x16;
  } else {  // rssi < -70
    return wifi_1_bar_16x16;
  }
}

const uint8_t *getWindBitmap32(int deg) {
  switch (deg) {
    case 0:
      return wind_direction_meteorological_0deg_32x32;
      break;
    case 1:
      return wind_direction_meteorological_1deg_32x32;
      break;
    case 2:
      return wind_direction_meteorological_2deg_32x32;
      break;
    case 3:
      return wind_direction_meteorological_3deg_32x32;
      break;
    case 4:
      return wind_direction_meteorological_4deg_32x32;
      break;
    case 5:
      return wind_direction_meteorological_5deg_32x32;
      break;
    case 6:
      return wind_direction_meteorological_6deg_32x32;
      break;
    case 7:
      return wind_direction_meteorological_7deg_32x32;
      break;
    case 8:
      return wind_direction_meteorological_8deg_32x32;
      break;
    case 9:
      return wind_direction_meteorological_9deg_32x32;
      break;
    case 10:
      return wind_direction_meteorological_10deg_32x32;
      break;
    case 11:
      return wind_direction_meteorological_11deg_32x32;
      break;
    case 12:
      return wind_direction_meteorological_12deg_32x32;
      break;
    case 13:
      return wind_direction_meteorological_13deg_32x32;
      break;
    case 14:
      return wind_direction_meteorological_14deg_32x32;
      break;
    case 15:
      return wind_direction_meteorological_15deg_32x32;
      break;
    case 16:
      return wind_direction_meteorological_16deg_32x32;
      break;
    case 17:
      return wind_direction_meteorological_17deg_32x32;
      break;
    case 18:
      return wind_direction_meteorological_18deg_32x32;
      break;
    case 19:
      return wind_direction_meteorological_19deg_32x32;
      break;
    case 20:
      return wind_direction_meteorological_20deg_32x32;
      break;
    case 21:
      return wind_direction_meteorological_21deg_32x32;
      break;
    case 22:
      return wind_direction_meteorological_22deg_32x32;
      break;
    case 23:
      return wind_direction_meteorological_23deg_32x32;
      break;
    case 24:
      return wind_direction_meteorological_24deg_32x32;
      break;
    case 25:
      return wind_direction_meteorological_25deg_32x32;
      break;
    case 26:
      return wind_direction_meteorological_26deg_32x32;
      break;
    case 27:
      return wind_direction_meteorological_27deg_32x32;
      break;
    case 28:
      return wind_direction_meteorological_28deg_32x32;
      break;
    case 29:
      return wind_direction_meteorological_29deg_32x32;
      break;
    case 30:
      return wind_direction_meteorological_30deg_32x32;
      break;
    case 31:
      return wind_direction_meteorological_31deg_32x32;
      break;
    case 32:
      return wind_direction_meteorological_32deg_32x32;
      break;
    case 33:
      return wind_direction_meteorological_33deg_32x32;
      break;
    case 34:
      return wind_direction_meteorological_34deg_32x32;
      break;
    case 35:
      return wind_direction_meteorological_35deg_32x32;
      break;
    case 36:
      return wind_direction_meteorological_36deg_32x32;
      break;
    case 37:
      return wind_direction_meteorological_37deg_32x32;
      break;
    case 38:
      return wind_direction_meteorological_38deg_32x32;
      break;
    case 39:
      return wind_direction_meteorological_39deg_32x32;
      break;
    case 40:
      return wind_direction_meteorological_40deg_32x32;
      break;
    case 41:
      return wind_direction_meteorological_41deg_32x32;
      break;
    case 42:
      return wind_direction_meteorological_42deg_32x32;
      break;
    case 43:
      return wind_direction_meteorological_43deg_32x32;
      break;
    case 44:
      return wind_direction_meteorological_44deg_32x32;
      break;
    case 45:
      return wind_direction_meteorological_45deg_32x32;
      break;
    case 46:
      return wind_direction_meteorological_46deg_32x32;
      break;
    case 47:
      return wind_direction_meteorological_47deg_32x32;
      break;
    case 48:
      return wind_direction_meteorological_48deg_32x32;
      break;
    case 49:
      return wind_direction_meteorological_49deg_32x32;
      break;
    case 50:
      return wind_direction_meteorological_50deg_32x32;
      break;
    case 51:
      return wind_direction_meteorological_51deg_32x32;
      break;
    case 52:
      return wind_direction_meteorological_52deg_32x32;
      break;
    case 53:
      return wind_direction_meteorological_53deg_32x32;
      break;
    case 54:
      return wind_direction_meteorological_54deg_32x32;
      break;
    case 55:
      return wind_direction_meteorological_55deg_32x32;
      break;
    case 56:
      return wind_direction_meteorological_56deg_32x32;
      break;
    case 57:
      return wind_direction_meteorological_57deg_32x32;
      break;
    case 58:
      return wind_direction_meteorological_58deg_32x32;
      break;
    case 59:
      return wind_direction_meteorological_59deg_32x32;
      break;
    case 60:
      return wind_direction_meteorological_60deg_32x32;
      break;
    case 61:
      return wind_direction_meteorological_61deg_32x32;
      break;
    case 62:
      return wind_direction_meteorological_62deg_32x32;
      break;
    case 63:
      return wind_direction_meteorological_63deg_32x32;
      break;
    case 64:
      return wind_direction_meteorological_64deg_32x32;
      break;
    case 65:
      return wind_direction_meteorological_65deg_32x32;
      break;
    case 66:
      return wind_direction_meteorological_66deg_32x32;
      break;
    case 67:
      return wind_direction_meteorological_67deg_32x32;
      break;
    case 68:
      return wind_direction_meteorological_68deg_32x32;
      break;
    case 69:
      return wind_direction_meteorological_69deg_32x32;
      break;
    case 70:
      return wind_direction_meteorological_70deg_32x32;
      break;
    case 71:
      return wind_direction_meteorological_71deg_32x32;
      break;
    case 72:
      return wind_direction_meteorological_72deg_32x32;
      break;
    case 73:
      return wind_direction_meteorological_73deg_32x32;
      break;
    case 74:
      return wind_direction_meteorological_74deg_32x32;
      break;
    case 75:
      return wind_direction_meteorological_75deg_32x32;
      break;
    case 76:
      return wind_direction_meteorological_76deg_32x32;
      break;
    case 77:
      return wind_direction_meteorological_77deg_32x32;
      break;
    case 78:
      return wind_direction_meteorological_78deg_32x32;
      break;
    case 79:
      return wind_direction_meteorological_79deg_32x32;
      break;
    case 80:
      return wind_direction_meteorological_80deg_32x32;
      break;
    case 81:
      return wind_direction_meteorological_81deg_32x32;
      break;
    case 82:
      return wind_direction_meteorological_82deg_32x32;
      break;
    case 83:
      return wind_direction_meteorological_83deg_32x32;
      break;
    case 84:
      return wind_direction_meteorological_84deg_32x32;
      break;
    case 85:
      return wind_direction_meteorological_85deg_32x32;
      break;
    case 86:
      return wind_direction_meteorological_86deg_32x32;
      break;
    case 87:
      return wind_direction_meteorological_87deg_32x32;
      break;
    case 88:
      return wind_direction_meteorological_88deg_32x32;
      break;
    case 89:
      return wind_direction_meteorological_89deg_32x32;
      break;
    case 90:
      return wind_direction_meteorological_90deg_32x32;
      break;
    case 91:
      return wind_direction_meteorological_91deg_32x32;
      break;
    case 92:
      return wind_direction_meteorological_92deg_32x32;
      break;
    case 93:
      return wind_direction_meteorological_93deg_32x32;
      break;
    case 94:
      return wind_direction_meteorological_94deg_32x32;
      break;
    case 95:
      return wind_direction_meteorological_95deg_32x32;
      break;
    case 96:
      return wind_direction_meteorological_96deg_32x32;
      break;
    case 97:
      return wind_direction_meteorological_97deg_32x32;
      break;
    case 98:
      return wind_direction_meteorological_98deg_32x32;
      break;
    case 99:
      return wind_direction_meteorological_99deg_32x32;
      break;
    case 100:
      return wind_direction_meteorological_100deg_32x32;
      break;
    case 101:
      return wind_direction_meteorological_101deg_32x32;
      break;
    case 102:
      return wind_direction_meteorological_102deg_32x32;
      break;
    case 103:
      return wind_direction_meteorological_103deg_32x32;
      break;
    case 104:
      return wind_direction_meteorological_104deg_32x32;
      break;
    case 105:
      return wind_direction_meteorological_105deg_32x32;
      break;
    case 106:
      return wind_direction_meteorological_106deg_32x32;
      break;
    case 107:
      return wind_direction_meteorological_107deg_32x32;
      break;
    case 108:
      return wind_direction_meteorological_108deg_32x32;
      break;
    case 109:
      return wind_direction_meteorological_109deg_32x32;
      break;
    case 110:
      return wind_direction_meteorological_110deg_32x32;
      break;
    case 111:
      return wind_direction_meteorological_111deg_32x32;
      break;
    case 112:
      return wind_direction_meteorological_112deg_32x32;
      break;
    case 113:
      return wind_direction_meteorological_113deg_32x32;
      break;
    case 114:
      return wind_direction_meteorological_114deg_32x32;
      break;
    case 115:
      return wind_direction_meteorological_115deg_32x32;
      break;
    case 116:
      return wind_direction_meteorological_116deg_32x32;
      break;
    case 117:
      return wind_direction_meteorological_117deg_32x32;
      break;
    case 118:
      return wind_direction_meteorological_118deg_32x32;
      break;
    case 119:
      return wind_direction_meteorological_119deg_32x32;
      break;
    case 120:
      return wind_direction_meteorological_120deg_32x32;
      break;
    case 121:
      return wind_direction_meteorological_121deg_32x32;
      break;
    case 122:
      return wind_direction_meteorological_122deg_32x32;
      break;
    case 123:
      return wind_direction_meteorological_123deg_32x32;
      break;
    case 124:
      return wind_direction_meteorological_124deg_32x32;
      break;
    case 125:
      return wind_direction_meteorological_125deg_32x32;
      break;
    case 126:
      return wind_direction_meteorological_126deg_32x32;
      break;
    case 127:
      return wind_direction_meteorological_127deg_32x32;
      break;
    case 128:
      return wind_direction_meteorological_128deg_32x32;
      break;
    case 129:
      return wind_direction_meteorological_129deg_32x32;
      break;
    case 130:
      return wind_direction_meteorological_130deg_32x32;
      break;
    case 131:
      return wind_direction_meteorological_131deg_32x32;
      break;
    case 132:
      return wind_direction_meteorological_132deg_32x32;
      break;
    case 133:
      return wind_direction_meteorological_133deg_32x32;
      break;
    case 134:
      return wind_direction_meteorological_134deg_32x32;
      break;
    case 135:
      return wind_direction_meteorological_135deg_32x32;
      break;
    case 136:
      return wind_direction_meteorological_136deg_32x32;
      break;
    case 137:
      return wind_direction_meteorological_137deg_32x32;
      break;
    case 138:
      return wind_direction_meteorological_138deg_32x32;
      break;
    case 139:
      return wind_direction_meteorological_139deg_32x32;
      break;
    case 140:
      return wind_direction_meteorological_140deg_32x32;
      break;
    case 141:
      return wind_direction_meteorological_141deg_32x32;
      break;
    case 142:
      return wind_direction_meteorological_142deg_32x32;
      break;
    case 143:
      return wind_direction_meteorological_143deg_32x32;
      break;
    case 144:
      return wind_direction_meteorological_144deg_32x32;
      break;
    case 145:
      return wind_direction_meteorological_145deg_32x32;
      break;
    case 146:
      return wind_direction_meteorological_146deg_32x32;
      break;
    case 147:
      return wind_direction_meteorological_147deg_32x32;
      break;
    case 148:
      return wind_direction_meteorological_148deg_32x32;
      break;
    case 149:
      return wind_direction_meteorological_149deg_32x32;
      break;
    case 150:
      return wind_direction_meteorological_150deg_32x32;
      break;
    case 151:
      return wind_direction_meteorological_151deg_32x32;
      break;
    case 152:
      return wind_direction_meteorological_152deg_32x32;
      break;
    case 153:
      return wind_direction_meteorological_153deg_32x32;
      break;
    case 154:
      return wind_direction_meteorological_154deg_32x32;
      break;
    case 155:
      return wind_direction_meteorological_155deg_32x32;
      break;
    case 156:
      return wind_direction_meteorological_156deg_32x32;
      break;
    case 157:
      return wind_direction_meteorological_157deg_32x32;
      break;
    case 158:
      return wind_direction_meteorological_158deg_32x32;
      break;
    case 159:
      return wind_direction_meteorological_159deg_32x32;
      break;
    case 160:
      return wind_direction_meteorological_160deg_32x32;
      break;
    case 161:
      return wind_direction_meteorological_161deg_32x32;
      break;
    case 162:
      return wind_direction_meteorological_162deg_32x32;
      break;
    case 163:
      return wind_direction_meteorological_163deg_32x32;
      break;
    case 164:
      return wind_direction_meteorological_164deg_32x32;
      break;
    case 165:
      return wind_direction_meteorological_165deg_32x32;
      break;
    case 166:
      return wind_direction_meteorological_166deg_32x32;
      break;
    case 167:
      return wind_direction_meteorological_167deg_32x32;
      break;
    case 168:
      return wind_direction_meteorological_168deg_32x32;
      break;
    case 169:
      return wind_direction_meteorological_169deg_32x32;
      break;
    case 170:
      return wind_direction_meteorological_170deg_32x32;
      break;
    case 171:
      return wind_direction_meteorological_171deg_32x32;
      break;
    case 172:
      return wind_direction_meteorological_172deg_32x32;
      break;
    case 173:
      return wind_direction_meteorological_173deg_32x32;
      break;
    case 174:
      return wind_direction_meteorological_174deg_32x32;
      break;
    case 175:
      return wind_direction_meteorological_175deg_32x32;
      break;
    case 176:
      return wind_direction_meteorological_176deg_32x32;
      break;
    case 177:
      return wind_direction_meteorological_177deg_32x32;
      break;
    case 178:
      return wind_direction_meteorological_178deg_32x32;
      break;
    case 179:
      return wind_direction_meteorological_179deg_32x32;
      break;
    case 180:
      return wind_direction_meteorological_180deg_32x32;
      break;
    case 181:
      return wind_direction_meteorological_181deg_32x32;
      break;
    case 182:
      return wind_direction_meteorological_182deg_32x32;
      break;
    case 183:
      return wind_direction_meteorological_183deg_32x32;
      break;
    case 184:
      return wind_direction_meteorological_184deg_32x32;
      break;
    case 185:
      return wind_direction_meteorological_185deg_32x32;
      break;
    case 186:
      return wind_direction_meteorological_186deg_32x32;
      break;
    case 187:
      return wind_direction_meteorological_187deg_32x32;
      break;
    case 188:
      return wind_direction_meteorological_188deg_32x32;
      break;
    case 189:
      return wind_direction_meteorological_189deg_32x32;
      break;
    case 190:
      return wind_direction_meteorological_190deg_32x32;
      break;
    case 191:
      return wind_direction_meteorological_191deg_32x32;
      break;
    case 192:
      return wind_direction_meteorological_192deg_32x32;
      break;
    case 193:
      return wind_direction_meteorological_193deg_32x32;
      break;
    case 194:
      return wind_direction_meteorological_194deg_32x32;
      break;
    case 195:
      return wind_direction_meteorological_195deg_32x32;
      break;
    case 196:
      return wind_direction_meteorological_196deg_32x32;
      break;
    case 197:
      return wind_direction_meteorological_197deg_32x32;
      break;
    case 198:
      return wind_direction_meteorological_198deg_32x32;
      break;
    case 199:
      return wind_direction_meteorological_199deg_32x32;
      break;
    case 200:
      return wind_direction_meteorological_200deg_32x32;
      break;
    case 201:
      return wind_direction_meteorological_201deg_32x32;
      break;
    case 202:
      return wind_direction_meteorological_202deg_32x32;
      break;
    case 203:
      return wind_direction_meteorological_203deg_32x32;
      break;
    case 204:
      return wind_direction_meteorological_204deg_32x32;
      break;
    case 205:
      return wind_direction_meteorological_205deg_32x32;
      break;
    case 206:
      return wind_direction_meteorological_206deg_32x32;
      break;
    case 207:
      return wind_direction_meteorological_207deg_32x32;
      break;
    case 208:
      return wind_direction_meteorological_208deg_32x32;
      break;
    case 209:
      return wind_direction_meteorological_209deg_32x32;
      break;
    case 210:
      return wind_direction_meteorological_210deg_32x32;
      break;
    case 211:
      return wind_direction_meteorological_211deg_32x32;
      break;
    case 212:
      return wind_direction_meteorological_212deg_32x32;
      break;
    case 213:
      return wind_direction_meteorological_213deg_32x32;
      break;
    case 214:
      return wind_direction_meteorological_214deg_32x32;
      break;
    case 215:
      return wind_direction_meteorological_215deg_32x32;
      break;
    case 216:
      return wind_direction_meteorological_216deg_32x32;
      break;
    case 217:
      return wind_direction_meteorological_217deg_32x32;
      break;
    case 218:
      return wind_direction_meteorological_218deg_32x32;
      break;
    case 219:
      return wind_direction_meteorological_219deg_32x32;
      break;
    case 220:
      return wind_direction_meteorological_220deg_32x32;
      break;
    case 221:
      return wind_direction_meteorological_221deg_32x32;
      break;
    case 222:
      return wind_direction_meteorological_222deg_32x32;
      break;
    case 223:
      return wind_direction_meteorological_223deg_32x32;
      break;
    case 224:
      return wind_direction_meteorological_224deg_32x32;
      break;
    case 225:
      return wind_direction_meteorological_225deg_32x32;
      break;
    case 226:
      return wind_direction_meteorological_226deg_32x32;
      break;
    case 227:
      return wind_direction_meteorological_227deg_32x32;
      break;
    case 228:
      return wind_direction_meteorological_228deg_32x32;
      break;
    case 229:
      return wind_direction_meteorological_229deg_32x32;
      break;
    case 230:
      return wind_direction_meteorological_230deg_32x32;
      break;
    case 231:
      return wind_direction_meteorological_231deg_32x32;
      break;
    case 232:
      return wind_direction_meteorological_232deg_32x32;
      break;
    case 233:
      return wind_direction_meteorological_233deg_32x32;
      break;
    case 234:
      return wind_direction_meteorological_234deg_32x32;
      break;
    case 235:
      return wind_direction_meteorological_235deg_32x32;
      break;
    case 236:
      return wind_direction_meteorological_236deg_32x32;
      break;
    case 237:
      return wind_direction_meteorological_237deg_32x32;
      break;
    case 238:
      return wind_direction_meteorological_238deg_32x32;
      break;
    case 239:
      return wind_direction_meteorological_239deg_32x32;
      break;
    case 240:
      return wind_direction_meteorological_240deg_32x32;
      break;
    case 241:
      return wind_direction_meteorological_241deg_32x32;
      break;
    case 242:
      return wind_direction_meteorological_242deg_32x32;
      break;
    case 243:
      return wind_direction_meteorological_243deg_32x32;
      break;
    case 244:
      return wind_direction_meteorological_244deg_32x32;
      break;
    case 245:
      return wind_direction_meteorological_245deg_32x32;
      break;
    case 246:
      return wind_direction_meteorological_246deg_32x32;
      break;
    case 247:
      return wind_direction_meteorological_247deg_32x32;
      break;
    case 248:
      return wind_direction_meteorological_248deg_32x32;
      break;
    case 249:
      return wind_direction_meteorological_249deg_32x32;
      break;
    case 250:
      return wind_direction_meteorological_250deg_32x32;
      break;
    case 251:
      return wind_direction_meteorological_251deg_32x32;
      break;
    case 252:
      return wind_direction_meteorological_252deg_32x32;
      break;
    case 253:
      return wind_direction_meteorological_253deg_32x32;
      break;
    case 254:
      return wind_direction_meteorological_254deg_32x32;
      break;
    case 255:
      return wind_direction_meteorological_255deg_32x32;
      break;
    case 256:
      return wind_direction_meteorological_256deg_32x32;
      break;
    case 257:
      return wind_direction_meteorological_257deg_32x32;
      break;
    case 258:
      return wind_direction_meteorological_258deg_32x32;
      break;
    case 259:
      return wind_direction_meteorological_259deg_32x32;
      break;
    case 260:
      return wind_direction_meteorological_260deg_32x32;
      break;
    case 261:
      return wind_direction_meteorological_261deg_32x32;
      break;
    case 262:
      return wind_direction_meteorological_262deg_32x32;
      break;
    case 263:
      return wind_direction_meteorological_263deg_32x32;
      break;
    case 264:
      return wind_direction_meteorological_264deg_32x32;
      break;
    case 265:
      return wind_direction_meteorological_265deg_32x32;
      break;
    case 266:
      return wind_direction_meteorological_266deg_32x32;
      break;
    case 267:
      return wind_direction_meteorological_267deg_32x32;
      break;
    case 268:
      return wind_direction_meteorological_268deg_32x32;
      break;
    case 269:
      return wind_direction_meteorological_269deg_32x32;
      break;
    case 270:
      return wind_direction_meteorological_270deg_32x32;
      break;
    case 271:
      return wind_direction_meteorological_271deg_32x32;
      break;
    case 272:
      return wind_direction_meteorological_272deg_32x32;
      break;
    case 273:
      return wind_direction_meteorological_273deg_32x32;
      break;
    case 274:
      return wind_direction_meteorological_274deg_32x32;
      break;
    case 275:
      return wind_direction_meteorological_275deg_32x32;
      break;
    case 276:
      return wind_direction_meteorological_276deg_32x32;
      break;
    case 277:
      return wind_direction_meteorological_277deg_32x32;
      break;
    case 278:
      return wind_direction_meteorological_278deg_32x32;
      break;
    case 279:
      return wind_direction_meteorological_279deg_32x32;
      break;
    case 280:
      return wind_direction_meteorological_280deg_32x32;
      break;
    case 281:
      return wind_direction_meteorological_281deg_32x32;
      break;
    case 282:
      return wind_direction_meteorological_282deg_32x32;
      break;
    case 283:
      return wind_direction_meteorological_283deg_32x32;
      break;
    case 284:
      return wind_direction_meteorological_284deg_32x32;
      break;
    case 285:
      return wind_direction_meteorological_285deg_32x32;
      break;
    case 286:
      return wind_direction_meteorological_286deg_32x32;
      break;
    case 287:
      return wind_direction_meteorological_287deg_32x32;
      break;
    case 288:
      return wind_direction_meteorological_288deg_32x32;
      break;
    case 289:
      return wind_direction_meteorological_289deg_32x32;
      break;
    case 290:
      return wind_direction_meteorological_290deg_32x32;
      break;
    case 291:
      return wind_direction_meteorological_291deg_32x32;
      break;
    case 292:
      return wind_direction_meteorological_292deg_32x32;
      break;
    case 293:
      return wind_direction_meteorological_293deg_32x32;
      break;
    case 294:
      return wind_direction_meteorological_294deg_32x32;
      break;
    case 295:
      return wind_direction_meteorological_295deg_32x32;
      break;
    case 296:
      return wind_direction_meteorological_296deg_32x32;
      break;
    case 297:
      return wind_direction_meteorological_297deg_32x32;
      break;
    case 298:
      return wind_direction_meteorological_298deg_32x32;
      break;
    case 299:
      return wind_direction_meteorological_299deg_32x32;
      break;
    case 300:
      return wind_direction_meteorological_300deg_32x32;
      break;
    case 301:
      return wind_direction_meteorological_301deg_32x32;
      break;
    case 302:
      return wind_direction_meteorological_302deg_32x32;
      break;
    case 303:
      return wind_direction_meteorological_303deg_32x32;
      break;
    case 304:
      return wind_direction_meteorological_304deg_32x32;
      break;
    case 305:
      return wind_direction_meteorological_305deg_32x32;
      break;
    case 306:
      return wind_direction_meteorological_306deg_32x32;
      break;
    case 307:
      return wind_direction_meteorological_307deg_32x32;
      break;
    case 308:
      return wind_direction_meteorological_308deg_32x32;
      break;
    case 309:
      return wind_direction_meteorological_309deg_32x32;
      break;
    case 310:
      return wind_direction_meteorological_310deg_32x32;
      break;
    case 311:
      return wind_direction_meteorological_311deg_32x32;
      break;
    case 312:
      return wind_direction_meteorological_312deg_32x32;
      break;
    case 313:
      return wind_direction_meteorological_313deg_32x32;
      break;
    case 314:
      return wind_direction_meteorological_314deg_32x32;
      break;
    case 315:
      return wind_direction_meteorological_315deg_32x32;
      break;
    case 316:
      return wind_direction_meteorological_316deg_32x32;
      break;
    case 317:
      return wind_direction_meteorological_317deg_32x32;
      break;
    case 318:
      return wind_direction_meteorological_318deg_32x32;
      break;
    case 319:
      return wind_direction_meteorological_319deg_32x32;
      break;
    case 320:
      return wind_direction_meteorological_320deg_32x32;
      break;
    case 321:
      return wind_direction_meteorological_321deg_32x32;
      break;
    case 322:
      return wind_direction_meteorological_322deg_32x32;
      break;
    case 323:
      return wind_direction_meteorological_323deg_32x32;
      break;
    case 324:
      return wind_direction_meteorological_324deg_32x32;
      break;
    case 325:
      return wind_direction_meteorological_325deg_32x32;
      break;
    case 326:
      return wind_direction_meteorological_326deg_32x32;
      break;
    case 327:
      return wind_direction_meteorological_327deg_32x32;
      break;
    case 328:
      return wind_direction_meteorological_328deg_32x32;
      break;
    case 329:
      return wind_direction_meteorological_329deg_32x32;
      break;
    case 330:
      return wind_direction_meteorological_330deg_32x32;
      break;
    case 331:
      return wind_direction_meteorological_331deg_32x32;
      break;
    case 332:
      return wind_direction_meteorological_332deg_32x32;
      break;
    case 333:
      return wind_direction_meteorological_333deg_32x32;
      break;
    case 334:
      return wind_direction_meteorological_334deg_32x32;
      break;
    case 335:
      return wind_direction_meteorological_335deg_32x32;
      break;
    case 336:
      return wind_direction_meteorological_336deg_32x32;
      break;
    case 337:
      return wind_direction_meteorological_337deg_32x32;
      break;
    case 338:
      return wind_direction_meteorological_338deg_32x32;
      break;
    case 339:
      return wind_direction_meteorological_339deg_32x32;
      break;
    case 340:
      return wind_direction_meteorological_340deg_32x32;
      break;
    case 341:
      return wind_direction_meteorological_341deg_32x32;
      break;
    case 342:
      return wind_direction_meteorological_342deg_32x32;
      break;
    case 343:
      return wind_direction_meteorological_343deg_32x32;
      break;
    case 344:
      return wind_direction_meteorological_344deg_32x32;
      break;
    case 345:
      return wind_direction_meteorological_345deg_32x32;
      break;
    case 346:
      return wind_direction_meteorological_346deg_32x32;
      break;
    case 347:
      return wind_direction_meteorological_347deg_32x32;
      break;
    case 348:
      return wind_direction_meteorological_348deg_32x32;
      break;
    case 349:
      return wind_direction_meteorological_349deg_32x32;
      break;
    case 350:
      return wind_direction_meteorological_350deg_32x32;
      break;
    case 351:
      return wind_direction_meteorological_351deg_32x32;
      break;
    case 352:
      return wind_direction_meteorological_352deg_32x32;
      break;
    case 353:
      return wind_direction_meteorological_353deg_32x32;
      break;
    case 354:
      return wind_direction_meteorological_354deg_32x32;
      break;
    case 355:
      return wind_direction_meteorological_355deg_32x32;
      break;
    case 356:
      return wind_direction_meteorological_356deg_32x32;
      break;
    case 357:
      return wind_direction_meteorological_357deg_32x32;
      break;
    case 358:
      return wind_direction_meteorological_358deg_32x32;
      break;
    case 359:
      return wind_direction_meteorological_359deg_32x32;
      break;
  }
}

const uint8_t *getBatBitmap16(uint32_t batPercent) {
  if (batPercent >= 93) {
    return battery_full_90deg_16x16;
  } else if (batPercent >= 79) {
    return battery_6_bar_90deg_16x16;
  } else if (batPercent >= 65) {
    return battery_5_bar_90deg_16x16;
  } else if (batPercent >= 50) {
    return battery_4_bar_90deg_16x16;
  } else if (batPercent >= 36) {
    return battery_3_bar_90deg_16x16;
  } else if (batPercent >= 22) {
    return battery_2_bar_90deg_16x16;
  } else if (batPercent >= 8) {
    return battery_1_bar_90deg_16x16;
  } else {  // batPercent < 8
    return battery_0_bar_90deg_16x16;
  }
}

const uint8_t *getForecastBitmap196(int id, float clouds, float wind_speed, float wind_gust) {
  // always using the day icon for weather forecast
  // bool day = current.weather.icon.endsWith("d");
  bool cloudy = clouds > 60.25;    // partly cloudy / partly sunny
  bool windy = (wind_speed >= 32.2 /*m/s*/
                || wind_gust >= 40.2 /*m/s*/);

  switch (id) {
    // Group 2xx: Thunderstorm
    case 200:  // Thunderstorm  thunderstorm with light rain     11d
    case 201:  // Thunderstorm  thunderstorm with rain           11d
    case 202:  // Thunderstorm  thunderstorm with heavy rain     11d
    case 210:  // Thunderstorm  light thunderstorm               11d
    case 211:  // Thunderstorm  thunderstorm                     11d
    case 212:  // Thunderstorm  heavy thunderstorm               11d
    case 221:  // Thunderstorm  ragged thunderstorm              11d
      if (!cloudy) { return wi_day_thunderstorm_196x196; }
      return wi_thunderstorm_196x196;
    case 230:  // Thunderstorm  thunderstorm with light drizzle  11d
    case 231:  // Thunderstorm  thunderstorm with drizzle        11d
    case 232:  // Thunderstorm  thunderstorm with heavy drizzle  11d
      if (!cloudy) { return wi_day_storm_showers_196x196; }
      return wi_storm_showers_196x196;
    // Group 3xx: Drizzle
    case 300:  // Drizzle       light intensity drizzle          09d
    case 301:  // Drizzle       drizzle                          09d
    case 302:  // Drizzle       heavy intensity drizzle          09d
    case 310:  // Drizzle       light intensity drizzle rain     09d
    case 311:  // Drizzle       drizzle rain                     09d
    case 312:  // Drizzle       heavy intensity drizzle rain     09d
    case 313:  // Drizzle       shower rain and drizzle          09d
    case 314:  // Drizzle       heavy shower rain and drizzle    09d
    case 321:  // Drizzle       shower drizzle                   09d
      if (!cloudy) { return wi_day_showers_196x196; }
      return wi_showers_196x196;
    // Group 5xx: Rain
    case 500:  // Rain          light rain                       10d
    case 501:  // Rain          moderate rain                    10d
    case 502:  // Rain          heavy intensity rain             10d
    case 503:  // Rain          very heavy rain                  10d
    case 504:  // Rain          extreme rain                     10d
      if (!cloudy && windy) { return wi_day_rain_wind_196x196; }
      if (!cloudy) { return wi_day_rain_196x196; }
      if (windy) { return wi_rain_wind_196x196; }
      return wi_rain_196x196;
    case 511:  // Rain          freezing rain                    13d
      if (!cloudy) { return wi_day_rain_mix_196x196; }
      return wi_rain_mix_196x196;
    case 520:  // Rain          light intensity shower rain      09d
    case 521:  // Rain          shower rain                      09d
    case 522:  // Rain          heavy intensity shower rain      09d
    case 531:  // Rain          ragged shower rain               09d
      if (!cloudy) { return wi_day_showers_196x196; }
      return wi_showers_196x196;
    // Group 6xx: Snow
    case 600:  // Snow          light snow                       13d
    case 601:  // Snow          Snow                             13d
    case 602:  // Snow          Heavy snow                       13d
      if (!cloudy && windy) { return wi_day_snow_wind_196x196; }
      if (!cloudy) { return wi_day_snow_196x196; }
      if (windy) { return wi_snow_wind_196x196; }
      return wi_snow_196x196;
    case 611:  // Snow          Sleet                            13d
    case 612:  // Snow          Light shower sleet               13d
    case 613:  // Snow          Shower sleet                     13d
      if (!cloudy) { return wi_day_sleet_196x196; }
      return wi_sleet_196x196;
    case 615:  // Snow          Light rain and snow              13d
    case 616:  // Snow          Rain and snow                    13d
    case 620:  // Snow          Light shower snow                13d
    case 621:  // Snow          Shower snow                      13d
    case 622:  // Snow          Heavy shower snow                13d
      if (!cloudy) { return wi_day_rain_mix_196x196; }
      return wi_rain_mix_196x196;
    // Group 7xx: Atmosphere
    case 701:  // Mist          mist                             50d
      if (!cloudy) { return wi_day_fog_196x196; }
      return wi_fog_196x196;
    case 711:  // Smoke         Smoke                            50d
      return wi_smoke_196x196;
    case 721:  // Haze          Haze                             50d
      return wi_day_haze_196x196;
      if (!cloudy) { return wi_day_haze_196x196; }
      return wi_dust_196x196;
    case 731:  // Dust          sand/dust whirls                 50d
      return wi_sandstorm_196x196;
    case 741:  // Fog           fog                              50d
      if (!cloudy) { return wi_day_fog_196x196; }
      return wi_fog_196x196;
    case 751:  // Sand          sand                             50d
      return wi_sandstorm_196x196;
    case 761:  // Dust          dust                             50d
      return wi_dust_196x196;
    case 762:  // Ash           volcanic ash                     50d
      return wi_volcano_196x196;
    case 771:  // Squall        squalls                          50d
      return wi_cloudy_gusts_196x196;
    case 781:  // Tornado       tornado                          50d
      return wi_tornado_196x196;
    // Group 800: Clear
    case 800:  // Clear         clear sky                        01d 01n
      if (windy) { return wi_strong_wind_196x196; }
      return wi_day_sunny_196x196;
    // Group 80x: Clouds
    case 801:  // Clouds        few clouds: 11-25%               02d 02n
      if (windy) { return wi_day_cloudy_gusts_196x196; }
      return wi_day_sunny_overcast_196x196;
    case 802:  // Clouds        scattered clouds: 25-50%         03d 03n
    case 803:  // Clouds        broken clouds: 51-84%            04d 04n
      if (windy) { return wi_day_cloudy_gusts_196x196; }
      return wi_day_cloudy_196x196;
    case 804:  // Clouds        overcast clouds: 85-100%         04d 04n
      if (windy) { return wi_cloudy_gusts_196x196; }
      return wi_cloudy_196x196;
    default:
      // OpenWeatherMap maybe this is a new icon in one of the existing groups
      if (id >= 200 && id < 300) { return wi_thunderstorm_196x196; }
      if (id >= 300 && id < 400) { return wi_showers_196x196; }
      if (id >= 500 && id < 600) { return wi_rain_196x196; }
      if (id >= 600 && id < 700) { return wi_snow_196x196; }
      if (id >= 700 && id < 800) { return wi_fog_196x196; }
      if (id >= 800 && id < 900) { return wi_cloudy_196x196; }
      return wi_na_196x196;
  }
}

const uint8_t *getForecastBitmap96(int id, float clouds, float wind_speed, float wind_gust) {
  // always using the day icon for weather forecast
  // bool day = current.weather.icon.endsWith("d");
  bool cloudy = clouds > 60.25;    // partly cloudy / partly sunny
  bool windy = (wind_speed >= 32.2 /*m/s*/
                || wind_gust >= 40.2 /*m/s*/);

  switch (id) {
    // Group 2xx: Thunderstorm
    case 200:  // Thunderstorm  thunderstorm with light rain     11d
    case 201:  // Thunderstorm  thunderstorm with rain           11d
    case 202:  // Thunderstorm  thunderstorm with heavy rain     11d
    case 210:  // Thunderstorm  light thunderstorm               11d
    case 211:  // Thunderstorm  thunderstorm                     11d
    case 212:  // Thunderstorm  heavy thunderstorm               11d
    case 221:  // Thunderstorm  ragged thunderstorm              11d
      if (!cloudy) { return wi_day_thunderstorm_96x96; }
      return wi_thunderstorm_96x96;
    case 230:  // Thunderstorm  thunderstorm with light drizzle  11d
    case 231:  // Thunderstorm  thunderstorm with drizzle        11d
    case 232:  // Thunderstorm  thunderstorm with heavy drizzle  11d
      if (!cloudy) { return wi_day_storm_showers_96x96; }
      return wi_storm_showers_96x96;
    // Group 3xx: Drizzle
    case 300:  // Drizzle       light intensity drizzle          09d
    case 301:  // Drizzle       drizzle                          09d
    case 302:  // Drizzle       heavy intensity drizzle          09d
    case 310:  // Drizzle       light intensity drizzle rain     09d
    case 311:  // Drizzle       drizzle rain                     09d
    case 312:  // Drizzle       heavy intensity drizzle rain     09d
    case 313:  // Drizzle       shower rain and drizzle          09d
    case 314:  // Drizzle       heavy shower rain and drizzle    09d
    case 321:  // Drizzle       shower drizzle                   09d
      if (!cloudy) { return wi_day_showers_96x96; }
      return wi_showers_96x96;
    // Group 5xx: Rain
    case 500:  // Rain          light rain                       10d
    case 501:  // Rain          moderate rain                    10d
    case 502:  // Rain          heavy intensity rain             10d
    case 503:  // Rain          very heavy rain                  10d
    case 504:  // Rain          extreme rain                     10d
      if (!cloudy && windy) { return wi_day_rain_wind_96x96; }
      if (!cloudy) { return wi_day_rain_96x96; }
      if (windy) { return wi_rain_wind_96x96; }
      return wi_rain_96x96;
    case 511:  // Rain          freezing rain                    13d
      if (!cloudy) { return wi_day_rain_mix_96x96; }
      return wi_rain_mix_96x96;
    case 520:  // Rain          light intensity shower rain      09d
    case 521:  // Rain          shower rain                      09d
    case 522:  // Rain          heavy intensity shower rain      09d
    case 531:  // Rain          ragged shower rain               09d
      if (!cloudy) { return wi_day_showers_96x96; }
      return wi_showers_96x96;
    // Group 6xx: Snow
    case 600:  // Snow          light snow                       13d
    case 601:  // Snow          Snow                             13d
    case 602:  // Snow          Heavy snow                       13d
      if (!cloudy && windy) { return wi_day_snow_wind_96x96; }
      if (!cloudy) { return wi_day_snow_96x96; }
      if (windy) { return wi_snow_wind_96x96; }
      return wi_snow_96x96;
    case 611:  // Snow          Sleet                            13d
    case 612:  // Snow          Light shower sleet               13d
    case 613:  // Snow          Shower sleet                     13d
      if (!cloudy) { return wi_day_sleet_96x96; }
      return wi_sleet_96x96;
    case 615:  // Snow          Light rain and snow              13d
    case 616:  // Snow          Rain and snow                    13d
    case 620:  // Snow          Light shower snow                13d
    case 621:  // Snow          Shower snow                      13d
    case 622:  // Snow          Heavy shower snow                13d
      if (!cloudy) { return wi_day_rain_mix_96x96; }
      return wi_rain_mix_96x96;
    // Group 7xx: Atmosphere
    case 701:  // Mist          mist                             50d
      if (!cloudy) { return wi_day_fog_96x96; }
      return wi_fog_96x96;
    case 711:  // Smoke         Smoke                            50d
      return wi_smoke_96x96;
    case 721:  // Haze          Haze                             50d
      return wi_day_haze_96x96;
      if (!cloudy) { return wi_day_haze_96x96; }
      return wi_dust_96x96;
    case 731:  // Dust          sand/dust whirls                 50d
      return wi_sandstorm_96x96;
    case 741:  // Fog           fog                              50d
      if (!cloudy) { return wi_day_fog_96x96; }
      return wi_fog_96x96;
    case 751:  // Sand          sand                             50d
      return wi_sandstorm_96x96;
    case 761:  // Dust          dust                             50d
      return wi_dust_96x96;
    case 762:  // Ash           volcanic ash                     50d
      return wi_volcano_96x96;
    case 771:  // Squall        squalls                          50d
      return wi_cloudy_gusts_96x96;
    case 781:  // Tornado       tornado                          50d
      return wi_tornado_96x96;
    // Group 800: Clear
    case 800:  // Clear         clear sky                        01d 01n
      if (windy) { return wi_strong_wind_96x96; }
      return wi_day_sunny_96x96;
    // Group 80x: Clouds
    case 801:  // Clouds        few clouds: 11-25%               02d 02n
      if (windy) { return wi_day_cloudy_gusts_96x96; }
      return wi_day_sunny_overcast_96x96;
    case 802:  // Clouds        scattered clouds: 25-50%         03d 03n
    case 803:  // Clouds        broken clouds: 51-84%            04d 04n
      if (windy) { return wi_day_cloudy_gusts_96x96; }
      return wi_day_cloudy_96x96;
    case 804:  // Clouds        overcast clouds: 85-100%         04d 04n
      if (windy) { return wi_cloudy_gusts_96x96; }
      return wi_cloudy_96x96;
    default:
      // OpenWeatherMap maybe this is a new icon in one of the existing groups
      if (id >= 200 && id < 300) { return wi_thunderstorm_96x96; }
      if (id >= 300 && id < 400) { return wi_showers_96x96; }
      if (id >= 500 && id < 600) { return wi_rain_96x96; }
      if (id >= 600 && id < 700) { return wi_snow_96x96; }
      if (id >= 700 && id < 800) { return wi_fog_96x96; }
      if (id >= 800 && id < 900) { return wi_cloudy_96x96; }
      return wi_na_96x96;
  }
}

void drawDarkGreyLine(int x1, int y1, int x2, int y2) {
  if (y1 == y2) {
    for (int x = 0; x <= x2 - x1; x++) {
      display.drawPixel(x1 + x, y1, (x % 2 == 0 ? (x % 3 == 0 ? GxEPD_BLACK : GxEPD_WHITE) : GxEPD_BLACK));
    }
  } else if (x1 == x2) {
    for (int y = 0; y <= y2 - y1; y++) {
      display.drawPixel(x1, y1 + y, (y % 2 == 0 ? (y % 3 == 0 ? GxEPD_BLACK : GxEPD_WHITE) : GxEPD_BLACK));
    }
  }
}

void fillGreyRect(int x, int y, int w, int h) {
  for (int ay = y; ay <= y + h; ay++) {
    for (int ax = x; ax <= x + w; ax++) {
      display.drawPixel(ax, ay, ay % 2 == 0 ? (ax % 2 == 0 ? GxEPD_BLACK : GxEPD_WHITE) : (ax % 2 == 0 ? GxEPD_WHITE : GxEPD_BLACK));
    }
  }
<<<<<<< HEAD
}
=======
}
>>>>>>> fef32653985dacd281fdca886c157043a07fefaa
