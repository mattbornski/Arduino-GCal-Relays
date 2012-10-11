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
boolean lastConnected = false;                 // state of the connection last time through the main loop
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
  if (client.available()) {
    byte relayStates[4] = { LOW, LOW, LOW, LOW };
    String now = NULL;
    String summary = NULL;
    String dtstart = NULL;
    String dtend = NULL;
    String line = "";
    while (char c = client.read()) {
      line += c;
      //Serial.print(c);
      if (c == '\n') {
        if (line.startsWith("END:VCALENDAR")) {
          client.stop();
          break;
        } else if (now == NULL && line.startsWith("Date:")) {
          now = parseHttpDate(line.substring(6, line.length() - 1));
        } else if (line.startsWith("BEGIN:VEVENT")) {
          summary = NULL;
          dtstart = NULL;
          dtend = NULL;
        } else if (line.startsWith("END:VEVENT")) {
          // Parse event
          Serial.println("Parse event:");
          Serial.println(summary);
          Serial.println(dtstart);
          Serial.println(dtend);
          Serial.println("Time is now:");
          Serial.println(now);
          Serial.println("date >= dtstart?");
          Serial.println((dtstart <= now ? "yes": "no"));
          Serial.println("date < dtend?");
          Serial.println((now < dtend ? "yes": "no"));
          if (dtstart <= now && dtend > now) {
            relayStates[0] = HIGH;
            relayStates[1] = HIGH;
            relayStates[2] = HIGH;
            relayStates[3] = HIGH;
          }
        } else if (line.startsWith("DTSTART:")) {
          dtstart = parseVCalDate(line.substring(8, line.length() - 1));
        } else if (line.startsWith("DTEND:")) {
          dtend = parseVCalDate(line.substring(6, line.length() - 1));
        } else if (line.startsWith("SUMMARY:")) {
          summary = line.substring(8, line.length() - 1);
        }
        line = "";
      }
    }
    
    for (int i = 0; i < 4; i++) {
      digitalWrite(relayPin[i], relayStates[i]);
    }
  }

  // if there's no net connection, but there was one last time
  // through the loop, then stop the client:
  if (!client.connected() && lastConnected) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
  }

  // if you're not connected, and ten seconds have passed since
  // your last connection, then connect again and send data:
//  Serial.println(millis() - lastConnectionTime);
//  Serial.println(client.connected());
  if(!client.connected() && (((millis() - lastConnectionTime) > postingInterval) || (lastConnectionTime == 0))) {
    httpRequest();
  }
  // store the state of the connection for next time through
  // the loop:
  lastConnected = client.connected();
}

String parseVCalDate(String dt) {
  return dt;
}

String parseHttpDate(String dt) {
  // RFC 822 date format is
  // DOM, [D]D MMM YYYY hh:mm:ss GMT
  int prevSplit = dt.indexOf(' ') + 1;
  int nextSplit = dt.indexOf(' ', prevSplit);
  String DD = "0" + dt.substring(prevSplit, nextSplit);
  DD = DD.substring(DD.length() - 2, DD.length());
  prevSplit = nextSplit + 1;
  nextSplit = dt.indexOf(' ', prevSplit);
  String MM = dt.substring(prevSplit, nextSplit);
  if (MM.equalsIgnoreCase("JAN")) {
    MM = "01";
  } else if (MM.equalsIgnoreCase("FEB")) {
    MM = "02";
  } else if (MM.equalsIgnoreCase("MAR")) {
    MM = "03";
  } else if (MM.equalsIgnoreCase("APR")) {
    MM = "04";
  } else if (MM.equalsIgnoreCase("MAY")) {
    MM = "05";
  } else if (MM.equalsIgnoreCase("JUN")) {
    MM = "06";
  } else if (MM.equalsIgnoreCase("JUL")) {
    MM = "07";
  } else if (MM.equalsIgnoreCase("AUG")) {
    MM = "08";
  } else if (MM.equalsIgnoreCase("SEP")) {
    MM = "09";
  } else if (MM.equalsIgnoreCase("OCT")) {
    MM = "10";
  } else if (MM.equalsIgnoreCase("NOV")) {
    MM = "11";
  } else if (MM.equalsIgnoreCase("DEC")) {
    MM = "12";
  }
  prevSplit = nextSplit + 1;
  nextSplit = dt.indexOf(' ', prevSplit);
  String YY = dt.substring(prevSplit, nextSplit);
  prevSplit = nextSplit + 1;
  nextSplit = dt.indexOf(':', prevSplit);
  String hh = dt.substring(prevSplit, nextSplit);
  prevSplit = nextSplit + 1;
  nextSplit = dt.indexOf(':', prevSplit);
  String mm = dt.substring(prevSplit, nextSplit);
  prevSplit = nextSplit + 1;
  nextSplit = dt.indexOf(' ', prevSplit);
  String ss = dt.substring(prevSplit, nextSplit);
  String ret = YY + MM + DD + "T" + hh + mm + ss + "Z";
  Serial.println("parsed " + ret);
  return ret;
}

// this method makes a HTTP connection to the server:
void httpRequest() {
  // if there's a successful connection:
  if (client.connect(server, 80)) {
    Serial.println("connecting...");
    // send the HTTP GET request:
    client.println("GET /" + CALENDAR_FEED_URL + " HTTP/1.1");
    client.println("Host: proxy.bornski.com");
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println();

    // note the time that the connection was made:
    lastConnectionTime = millis();
  } 
  else {
    // if you couldn't make a connection:
    Serial.println("connection failed");
    Serial.println("disconnecting.");
    client.stop();
  }
}
