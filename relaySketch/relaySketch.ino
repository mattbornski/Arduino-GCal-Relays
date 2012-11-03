#include <SPI.h>

#include <Ethernet.h>
#include <EthernetClient.h>
#include <Dhcp.h>

String CALENDAR_FEED_URL = "https://google.com/your calendars private vcal/ics format feed here";

byte myMac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x66 };
IPAddress myIp(192, 168, 1, 66);
IPAddress myDns(192, 168, 1, 1);

EthernetClient client;
char server[] = "proxy.bornski.com";

unsigned long lastConnectionTime = 0;          // last time you connected to the server, in milliseconds
const unsigned long postingInterval = 10*1000;  // delay between updates, in milliseconds

byte relayPin[4] = { 2, 3, 4, 5};
//D2 -> RELAY1
//D3 -> RELAY2
//D4 -> RELAY3
//D5 -> RELAY4

void setup() {
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
  // If the designated interval has passed since you last connected, then connect again.
  if (((millis() - lastConnectionTime) > postingInterval) || (lastConnectionTime == 0)) {
    httpRequest();
    
    byte relayStates[4] = {LOW, LOW, LOW, LOW};
    parseResponse(relayStates);
    
    for (int i = 0; i < 4; i++) {
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

void parseResponse(byte relayStates[]) {
  char now[64] = {'\0'};
  char dtstart[64] = {'\0'};
  char dtend[64] = {'\0'};
  char summary[1024] = {'\0'};
  char line[1024] = {'\0'};
  int lineIndex = 0;
  
  // The server will close it's side of the connection when it is finished transferring data.
  while (client.connected()) {
    // It's possible that there is still data to come, but it is not yet ready to read.
    while (client.available()) {
      while (char c = client.read()) {
        if (c == '\n' || c == '\r') {
          line[lineIndex] = '\0';
          if (strstr(line, "END:VCALENDAR")) {
            return;
          } else if (now == NULL && strstr(line, "Date:") == line) {
            parseHttpDate(now, line + 6);
          } else if (strstr(line, "BEGIN:VEVENT") == line) {
            summary[0] = '\0';
            dtstart[0] = '\0';
            dtend[0] = '\0';
          } else if (strstr(line, "END:VEVENT") == line) {
            // Parse event
            int zone = -1;
            if (strstr(summary, "zone0") != NULL) {
              zone = 0;
            } else if (strstr(summary, "zone1") != NULL) {
              zone = 1;
            } else if (strstr(summary, "zone2") != NULL) {
              zone = 2;
            } else if (strstr(summary, "zone3") != NULL) {
              zone = 3;
            }
            
            if (zone != -1 && strcmp(dtstart, now) <= 0 && strcmp(dtend, now) >= 0) {
              relayStates[zone] = HIGH;
            }
          } else if (strstr(line, "DTSTART:") == line) {
            strcpy(dtstart, line + 8);
          } else if (strstr(line, "DTEND:") == line) {
            strcpy(dtend, line + 6);
          } else if (strstr(line, "SUMMARY:") == line) {
            strcpy(summary, line + 8);
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
  tok =  strtok(NULL, splitOn);
  now[13] = tok[0];
  now[14] = tok[1];
}

// this method makes a HTTP connection to the server:
void httpRequest() {
  Serial.println("connecting...");
  // if there's a successful connection:
  if (client.connect(server, 80)) {
    // send the HTTP GET request:
    client.println("GET /" + CALENDAR_FEED_URL + " HTTP/1.1");
    client.println("Host: proxy.bornski.com");
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println();

    // note the time that the connection was made:
    lastConnectionTime = millis();
    Serial.println("connected");
  } else {
    // if you couldn't make a connection:
    Serial.println("Connection failed");
    client.stop();
  }
}
