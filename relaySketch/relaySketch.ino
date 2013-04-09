#include <SPI.h>

#include <Ethernet.h>
#include <EthernetClient.h>
#include <Dhcp.h>

// Determine your private Google calendar feed URL and fill in here:
#define CALENDAR_FEED_URL "https://google.com/your calendars private vcal/ics format feed here"
#define CALENDAR_FEED_URL_REQUEST_HEADERS "GET /" CALENDAR_FEED_URL " HTTP/1.1\n" "Host: proxy.bornski.com\n" "User-Agent: arduino-ethernet\n" "Connection: close\n"

byte myMac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x66 };
IPAddress myIp(192, 168, 1, 66);
IPAddress myDns(192, 168, 1, 1);

EthernetClient client;
char server[] = "proxy.bornski.com";

unsigned long lastConnectionTime = 0;                  // last time you connected to the server, in milliseconds
static const unsigned long connectionInterval = 60000; // delay between updates, in milliseconds
unsigned long lastHeartbeatTime = 0;                   // last time you sent a heartbeat pulse, in milliseconds
static const unsigned long heartbeatInterval = 10000;  // delay between heartbeat pulses, in milliseconds

// D2 -> RELAY1
// D3 -> RELAY2
// D4 -> RELAY3
// D5 -> RELAY4
byte relayPin[4] = { 2, 3, 4, 5};
// D8 -> Heartbeat for watchdog timer
byte pulsePin = 8;

typedef struct zoneControl {
  byte relay;
  const char *keyword;
} zoneControl;

// garden -> relay 3
// Don't have too many keywords, they consume RAM
static const zoneControl zoneControlKeywords[] = {
  { relay: 0, keyword: "left lawn" },
  
  { relay: 1, keyword: "right lawn" },
  
  { relay: 2, keyword: "roses" },
  
  { relay: 3, keyword: "garden" },
};



void heartbeat() {
  pinMode(pulsePin, OUTPUT);
  Serial.println("heartbeat");
  delay(300);
  pinMode(pulsePin, INPUT);
  lastHeartbeatTime = millis();
}

// State which we do not wish to continuously allocate/deallocate.
char now[32] = {'\0'};
char dtstart[32] = {'\0'};
char dtend[32] = {'\0'};
char line[128] = {'\0'};
int lineIndex = 0;
boolean zones[4];

void setup() {
  heartbeat();
  
  Serial.begin(9600);
    // Some information indicates the ethernnet module likes to take time to boot.
  delay(1000);
  Serial.println("Begin setup");
  
  // Attempt to get a router assigned IP.
  Serial.print("Attempting to get an IP address using DHCP: ");
  if (!Ethernet.begin(myMac)) {
    // if that fails, start with a hard-coded address:
    Serial.println("failed, falling back to hardcoded IP.");
    Ethernet.begin(myMac, myIp, myDns);
  } else {
    Serial.println("OK");
  }
  Serial.print("My address: ");
  Serial.println(Ethernet.localIP());

  
  for (int i = 0; i < 4; i++) {
    pinMode(relayPin[i], OUTPUT);
  }
}

void loop() {
  boolean needHeartbeat = (millis() - lastHeartbeatTime) > heartbeatInterval;
  boolean needConnection = ((millis() - lastConnectionTime) > connectionInterval) || (lastConnectionTime == 0);
  if (needHeartbeat || needConnection) {
    Serial.print("Last heartbeat: ");
    Serial.print(millis() - lastHeartbeatTime);
    Serial.print(" / ");
    Serial.print(heartbeatInterval);
    Serial.print(" -> ");
    Serial.println(needHeartbeat ? "heartbeat" : "nothing");
    Serial.print("Last connection: ");
    Serial.print(millis() - lastConnectionTime);
    Serial.print(" / ");
    Serial.print(connectionInterval);
    Serial.print(" -> ");
    Serial.println(needConnection ? "request" : "nothing");
  }
  if (needHeartbeat && !needConnection) {
    // If the designated interval has passed since you sent a heartbeat,
    // send one again.  This reassures the hardware watchdog that all is
    // well.  Note that we do not send a heartbeat if you are in a "need
    // connection" state; this ensures that if we encounter network
    // trouble that would be solved by a reset, we will let the watchdog
    // handle that for us.
    heartbeat();
  }
  if (needConnection) {
    if (httpRequest()) {
      byte relayStates[4] = {LOW, LOW, LOW, LOW};
      parseResponse(relayStates);
      
      for (int i = 0; i < 4; i++) {
        Serial.print("Zone #");
        Serial.print(i);
        Serial.print(" set ");
        Serial.println((relayStates[i] ? "high": "low"));
        digitalWrite(relayPin[i], relayStates[i]);
      }
      
      // Flush all remaining data
      while (client.connected()) {
        while (client.available()) {
          client.read();
        }
      }
      
      // Terminate our half of the connection.
      client.stop();
    }
  }
}

void parseResponse(byte relayStates[]) {
  // The server will close it's side of the connection when it is finished transferring data.
  while (client.connected()) {
    // It's possible that there is still data to come, but it is not yet ready to read.
    while (client.available()) {
      while (char c = client.read()) {
//        Serial.print(c);
        if (c == '\n' || c == '\r' || lineIndex == 255) {
          line[lineIndex] = '\0';
          if (strstr(line, "END:VCALENDAR")) {
            // Reset state for next time
            now[0] = '\0';
            dtstart[0] = '\0';
            dtend[0] = '\0';
            line[0] = '\0';
            lineIndex = 0;
            
            return;
          } else if (strstr(line, "Date:") == line) {
            parseHttpDate(now, line + 6);
          } else if (strstr(line, "BEGIN:VEVENT") == line) {
            // New event.  Reset the per-event parsing state.
            dtstart[0] = '\0';
            dtend[0] = '\0';
            for (int i = 0; i < 4; i++) {
              zones[i] = false;
            }
          } else if (strstr(line, "END:VEVENT") == line) {
            // End of the event.  If the timestamps are right,
            // then OR any of the found zones into the desired
            // zone state.
            if (strcmp(dtstart, now) <= 0 && strcmp(dtend, now) >= 0) {
              for (int i = 0; i < 4; i++) {
                if (zones[i]) {
                  relayStates[i] = HIGH;
                }
              }
            }
            
            // If we get a long calendar it can take us a while to parse it out.
            if ((millis() - lastHeartbeatTime) > heartbeatInterval) {
              heartbeat();
            }
          } else if (strstr(line, "DTSTART:") == line) {
            strcpy(dtstart, line + 8);
          } else if (strstr(line, "DTEND:") == line) {
            strcpy(dtend, line + 6);
          } else if (strstr(line, "SUMMARY:") == line) {
            Serial.println("Found summary, parsing for zones...");
            Serial.println(line);
            // Parse summary line for any zones mentioned.
            for (int zcIndex = 0; zcIndex < sizeof(zoneControlKeywords) / sizeof(zoneControl); zcIndex++) {
              if (strstr(line, zoneControlKeywords[zcIndex].keyword) != NULL) {
                Serial.print("Setting zone ");
                Serial.println(zoneControlKeywords[zcIndex].relay);
                zones[zoneControlKeywords[zcIndex].relay] = true;
              }
            }
          }
          lineIndex = 0;
          line[lineIndex] = '\0';
        } else {
          line[lineIndex++] = c;
        }
      }
    }
  }
}

void parseHttpDate(char *now, char *line) {
  // Input line is in RFC 822 date format:
  // DOM, [D]D MMM YYYY hh:mm:ss GMT
  // Final result will look like:
  // YYYYMMDDThhmmssZ
  now[8] = 'T';
  now[14] = 'Z';
  now[15] = '\0';
  const char *splitOn = " :";
  // Retrieves DOM, which we discard.
  char *tok = strtok(line, splitOn);
  // Retrieves [D]D, which we might have to pad.
  tok = strtok(NULL, splitOn);
  if (strlen(tok) == 1) {
    now[6] = '0';
    now[7] = tok[0];
  } else {
    now[6] = tok[0];
    now[7] = tok[1];
  }
  // Retrieves MMM, which we have to convert to numeric.
  tok = strtok(NULL, splitOn);
  const char months[][4] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
  for (int i = 0; i < 12; i++) {
    for (int j = 0; j <= strlen(tok); j++) {
      if (j == strlen(tok)) {
        // We have a match.
        char MM[3] = {'\0'};
        itoa(i + 1, MM, 10);
        if (strlen(MM) == 1) {
          now[4] = '0';
          now[5] = MM[0];
        } else {
          now[4] = MM[0];
          now[5] = MM[1];
        }
      } else if (tolower(tok[j]) != months[i][j]) {
        break;
      }
    }
  }
  // Retrieves YYYY
  tok = strtok(NULL, splitOn);
  now[0] = tok[0];
  now[1] = tok[1];
  now[2] = tok[2];
  now[3] = tok[3];
  // Retrieves hh
  tok = strtok(NULL, splitOn);
  now[9] = tok[0];
  now[10] = tok[1];
  // Retrieves mm
  tok = strtok(NULL, splitOn);
  now[11] = tok[0];
  now[12] = tok[1];
  // Retrieves ss
  tok = strtok(NULL, splitOn);
  now[13] = tok[0];
  now[14] = tok[1];
}

// this method makes a HTTP connection to the server:
boolean httpRequest() {
  Serial.println("Connecting to host");
  // if there's a successful connection:
  if (client.connect(server, 80)) {
    Serial.println("Requesting URL");
    // send the HTTP GET request:
    client.println(CALENDAR_FEED_URL_REQUEST_HEADERS);
    
    // note the time that the connection was made:
    lastConnectionTime = millis();
    
    return true;
  } else {
    // if you couldn't make a connection:
    Serial.println("Connection failed");
    client.stop();
    
    return false;
  }
}
