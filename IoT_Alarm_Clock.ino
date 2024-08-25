#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

WiFiClient client;
ESP8266WebServer server;

String date;
String ampm = "AM";  // AM/PM indicator

int h24 = 0;
int alarmHour = -1; // Default value indicating no alarm is set
int alarmMinute = -1; // Default value indicating no alarm is set

#define NUM_MAX 4
#define LINE_WIDTH 16
#define ROTATE  90

// for NodeMCU 1.0
#define DIN_PIN 15  // D8
#define CS_PIN  13  // D7
#define CLK_PIN 12  // D6
#define BUZZER_PIN 4 // D2

#include "max7219.h"
#include "fonts.h"

// =======================================================================
// CHANGE YOUR CONFIG HERE:
// =======================================================================
const char* ssid     = "YOUR_SSID";     // SSID of local network
const char* password = "YOUR_PASSWORD";   // Password on network
float utcOffset = 6; // Time Zone setting
// =======================================================================

void setup() 
{
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  initMAX7219();
  sendCmdAll(CMD_SHUTDOWN,1);
  // sendCmdAll(CMD_INTENSITY,10); // Adjust the brightness between 0 and 15

  Serial.print("Connecting WiFi ");
  WiFi.begin(ssid, password);
  printStringWithShift("Connecting ",16);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected: "); Serial.println(WiFi.localIP());

  // Set up web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setAlarm", HTTP_GET, handleSetAlarm);
  server.begin();
}

#define MAX_DIGITS 16
byte dig[MAX_DIGITS]={0};
byte digold[MAX_DIGITS]={0};
byte digtrans[MAX_DIGITS]={0};
int updCnt = 0;
int dots = 0;
long dotTime = 0;
long clkTime = 0;
int dx=0;
int dy=0;
byte del=0;
int h,m,s;
long localEpoc = 0;
long localMillisAtUpdate = 0;

// =======================================================================
void loop()
{
  server.handleClient(); // Handle web server requests

  if(updCnt<=0) { // every 10 scrolls, ~450s=7.5m
    updCnt = 10;
    Serial.println("Getting data ...");
    printStringWithShift("  Getting data",15);
   
    getTime();
    Serial.println("Data loaded");
    clkTime = millis();
  }
 
  if(millis()-clkTime > 20000 && !del && dots) { // clock for 15s, then scrolls for about 30s
    printStringWithShift(date.c_str(),40);
    delay(7000);
    updCnt--;
    clkTime = millis();
  }
  if(millis()-dotTime > 500) {
    dotTime = millis();
    dots = !dots;
  }
  updateTime();
  showAnimClock();
    
  // Adjusting LED intensity.
  // 12am to 6am, lowest intensity 0
  if ( (h24 == 0) || ((h24 >= 1) && (h24 <= 6)) ) sendCmdAll(CMD_INTENSITY, 0);
  // 6pm to 11pm, intensity = 2
  else if ( (h24 >=18) && (h24 <= 23) ) sendCmdAll(CMD_INTENSITY, 2);
  // max brightness during bright daylight
  else sendCmdAll(CMD_INTENSITY, 10);

  // Check if it's time to sound the alarm
  if (h == alarmHour && m == alarmMinute) {
    tone(BUZZER_PIN, 1000); // Play a 1kHz tone
    delay(1000); // Sound the buzzer for 1 second
    noTone(BUZZER_PIN); // Stop the buzzer
  }
}

// =======================================================================

void showSimpleClock()
{
  dx=dy=0;
  clr();
  showDigit(h/10,  4, dig7x16);
  showDigit(h%10,  12, dig7x16);
  showDigit(m/10, 21, dig7x16);
  showDigit(m%10, 29, dig7x16);
  showDigit(s/10, 38, dig7x16);
  showDigit(s%10, 46, dig7x16);

  // Add a dot in the top right corner if it's PM
  if (ampm == "PM") {
    setCol(31, B00000001);
  }

  setCol(19,dots ? B00100100 : 0);
  setCol(36,dots ? B00100100 : 0);
  refreshAll();
}

// =======================================================================

void showAnimClock()
{
  
  byte digPos[4]={1,8,17,24};
  int digHt = 12;
  int num = 4; 
  int i;
  if(del==0) {
    del = digHt;
    for(i=0; i<num; i++) digold[i] = dig[i];
    dig[0] = h/10 ? h/10 : 10;
    dig[1] = h%10;
    dig[2] = m/10;
    dig[3] = m%10;
    for(i=0; i<num; i++)  digtrans[i] = (dig[i]==digold[i]) ? 0 : digHt;
  } else
    del--;
  
  clr();
  for(i=0; i<num; i++) {
    if(digtrans[i]==0) {
      dy=0;
      showDigit(dig[i], digPos[i], dig6x8);
    } else {
      dy = digHt-digtrans[i];
      showDigit(digold[i], digPos[i], dig6x8);
      dy = -digtrans[i];
      showDigit(dig[i], digPos[i], dig6x8);
      digtrans[i]--;
    }
  }

  // Add a dot in the top right corner if it's PM
  if (ampm == "PM") {
    setCol(31, B00000001);
  }

  dy=0;
  setCol(15,dots ? B00100100 : 0);
  refreshAll();
  delay(30);
}

// =======================================================================

void showDigit(char ch, int col, const uint8_t *data)
{
  if(dy<-8 | dy>8) return;
  int len = pgm_read_byte(data);
  int w = pgm_read_byte(data + 1 + ch * len);
  col += dx;
  for (int i = 0; i < w; i++)
    if(col+i>=0 && col+i<8*NUM_MAX) {
      byte v = pgm_read_byte(data + 1 + ch * len + 1 + i);
      if(!dy) scr[col + i] = v; else scr[col + i] |= dy>0 ? v>>dy : v<<-dy;
    }
}

// =======================================================================

void setCol(int col, byte v)
{
  if(dy<-8 | dy>8) return;
  col += dx;
  if(col>=0 && col<8*NUM_MAX)
    if(!dy) scr[col] = v; else scr[col] |= dy>0 ? v>>dy : v<<-dy;
}

// =======================================================================

int showChar(char ch, const uint8_t *data)
{
  int len = pgm_read_byte(data);
  int i,w = pgm_read_byte(data + 1 + ch * len);
  for (i = 0; i < w; i++)
    scr[NUM_MAX*8 + i] = pgm_read_byte(data + 1 + ch * len + 1 + i);
  scr[NUM_MAX*8 + i] = 0;
  return w;
}

// =======================================================================

void printCharWithShift(unsigned char c, int shiftDelay) {
  
  if (c < ' ' || c > '~'+25) return;
  c -= 32;
  int w = showChar(c, font);
  for (int i=0; i<w+1; i++) {
    delay(shiftDelay);
    scrollLeft();
    refreshAll();
  }
}

// =======================================================================

void printStringWithShift(const char* s, int shiftDelay){
  while (*s) {
    printCharWithShift(*s, shiftDelay);
    s++;
  }
}

// =======================================================================

void getTime()
{
  WiFiClient client;
  if (!client.connect("www.google.com", 80)) {
    Serial.println("connection to google failed");
    return;
  }

  client.print(String("GET / HTTP/1.1\r\n") +
               String("Host: www.google.com\r\n") +
               String("Connection: close\r\n\r\n"));
  int repeatCounter = 0;
  while (!client.available() && repeatCounter < 10) {
    delay(500);
    //Serial.println(".");
    repeatCounter++;
    
  }

  String line;
  client.setNoDelay(false);
  while(client.connected() && client.available()) {
    line = client.readStringUntil('\n');
    line.toUpperCase();
    if (line.startsWith("DATE: ")) {
      date = "     "+line.substring(6, 17);
      h = line.substring(23, 25).toInt();
      m = line.substring(26, 28).toInt();
      s = line.substring(29, 31).toInt();
      localMillisAtUpdate = millis();
      localEpoc = (h * 60 * 60 + m * 60 + s);
      
    }
  }
  client.stop();
}

// =======================================================================

void updateTime()
{
  long curEpoch = localEpoc + ((millis() - localMillisAtUpdate) / 1000);
  long epoch = fmod(round(curEpoch + 3600 * utcOffset + 86400L), 86400L);
  h24 = ((epoch  % 86400L) / 3600) % 24; // 24-hour format
  m = (epoch % 3600) / 60;
  s = epoch % 60;
  
  // Convert 24-hour to 12-hour format
  if (h24 == 0) {
    h = 12;
    ampm = "AM";
  } else if (h24 < 12) {
    h = h24;
    ampm = "AM";
  } else if (h24 == 12) {
    h = 12;
    ampm = "PM";
  } else {
    h = h24 - 12;
    ampm = "PM";
  }
}

// =======================================================================

void handleRoot() {
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Alarm Clock</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; margin: 0; padding: 0; box-sizing: border-box; }";
  html += "h1 { color: #333; margin-top: 20px; }";
  html += ".container { display: flex; justify-content: center; align-items: center; flex-direction: column; height: 100vh; }";
  html += "form { display: inline-block; background: #fff; padding: 20px 40px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); margin-top: 10px; }";
  html += "div.form-content { display: flex; justify-content: space-between; align-items: center; gap: 10px; }";
  html += "label { font-size: 20px; }";
  html += "input[type='number'] { width: 60px; padding: 5px; margin: 5px; box-sizing: border-box; font-size: 20px; }";
  html += "input[type='submit'] { background-color: #4CAF50; color: white; border: none; padding: 10px 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 20px; margin-top: 20px; cursor: pointer; border-radius: 4px; }";
  html += "input[type='submit']:hover { background-color: #45a049; }";
  html += "@media (max-width: 600px) { label { width: 120%; } input[type='number'] { width: 50%; } form { width: 70%; } }";
  html += "</style></head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<h1>Alarm Clock</h1>";
  html += "<div class='content'>";
  html += "<form action='/setAlarm' method='get'>";
  html += "<div class='form-content'>";
  html += "<label class='hour' for='hour'>Hour (0-12): </label>";
  html += "<input type='number' name='hour' min='0' max='12'>";
  html += "</div>";
  html += "<div class='form-content'>";
  html += "<label class='minute' for='minute'>Minute (0-59): </label>";
  html += "<input type='number' name='minute' min='0' max='59'>";
  html += "</div>";
  html += "<input type='submit' value='Set Alarm'>";
  html += "</form>";
  html += "</div>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSetAlarm() {
  if (server.hasArg("hour") && server.hasArg("minute")) {
    alarmHour = server.arg("hour").toInt();
    alarmMinute = server.arg("minute").toInt();
    String message = "Alarm set for " + String(alarmHour) + ":" + String(alarmMinute);
    server.send(200, "text/html", "<html><body><h1>" + message + "</h1><a href='/'>Back</a></body></html>");
  } else {
    server.send(400, "text/html", "<html><body><h1>Invalid parameters</h1><a href='/'>Back</a></body></html>");
  }
}
