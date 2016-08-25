/*
This Programm gets the local time by NTP (over ethernet, internet)
Sends Temperature read by a sensor in fixed intervall to a Server

TODO: test ethernet sockets
implement webclient, which sends JSON Object with various data to server
implement webserver for Temperature request on demand and change settings of the arduino over ethernet
read temperatur with sensor

socket flow: 
1. udp socket, for getting time in fixed intervall, allways open
2. tcp webserver, listening form incomming connections/request, allways open, opens again after every connection
3. tcp webclient for sending temperature data to server. (post JSON data)

Parse JSON Post:
http://edwin.baculsoft.com/2011/12/how-to-handle-json-post-request-using-php/


socket debuging: socket-state: 0x22 UDP, 0x17 outgoing tcp request, 0x14 listening tcp server - not connected to a client

a client connection(which connects to a webserver) opens only one socket
a open webserver connection(which listens for browsers/servers to connect) opens, if a connection is etablished, a second socket.

WEBSERVER ACTUALLY DISABLED, because no Port-Forwarding for incomming connections installed.

 */
 
 /*
 RTC Connection
 RTC Side      Arduino Side
 Pin 3 CS (SS): pin 49  (white)
 Pin 4 MOSI: pin 51     (violette)
 Pin 1 MISO: pin 50     (red)
 Pin 2 SCK: pin 52      (orange)
		
 Hardware-SS	pin 53 not used, but has to be defined as output, that SPI works properly

 Ethernet Chip W 1500 Connection
 SPI, see above
 Chip Select Pin 10
 
 ethernet pins:
 spi pins 50, 51, and 52 (icsp), pin 10 for select
 
 SD-Card Connection
 SPI, see above
 Chip Select Pin 4
 
 Dallas Temperature Sensor DS18B20
 // Data wire is plugged into pin 2 on the Arduino
 //Data pulled up to 5V with 1k resistor
 //power wire on 5V on the arduino
 //GND GND

 
 */
#include "MemoryFree.h"
#include <SPI.h>
#include "Time.h"
#include "Timezone.h"
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <utility/socket.h>
#include "TSIC.h"	// TSIC temperature sensor library
//#include <OneWire.h>
//#include <DallasTemperature.h>

//#define ONE_WIRE_BUS 2	//Pin Nr for One Wire

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
//OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
//DallasTemperature sensors(&oneWire);

//#include <Wire.h>
//#include "DS1307new.h"
//#include "RV3049_RealTimeClock_v2.h"

//Sensor's memory register addresses:
//const byte READ =  0x80; // 0b10000000;     // read command
//const byte WRITE = 0x7F; // 0b01111111;   //  write command
//const byte CONTROL_STATUS = 0b00000011;

//TSIC
TSIC Sensor1(22, 23);    // Signalpin, VCCpin
TSIC Sensor2(24, 25);    // Signalpin, VCCpin

uint16_t temperature = 0;
float Temperatur_C = 0;

uint16_t temperature2 = 0;
float Temperatur_C2 = 0;

//debug function
void ShowSockStatus()
{
	for (int i = 0; i < MAX_SOCK_NUM; i++) {
		Serial.print("Socket#");
		Serial.print(i);
		uint8_t s = W5100.readSnSR(i); //0x22 = Etablished
		Serial.print(":0x");
		Serial.print(s,16);
		Serial.print(" ");
		Serial.print(W5100.readSnPORT(i));
		Serial.print(" D:");
		uint8_t dip[4];
		W5100.readSnDIPR(i, dip);
		for (int j=0; j<4; j++) {
			Serial.print(dip[j],10);
			if (j<3) Serial.print(".");
		}
		Serial.print("(");
		Serial.print(W5100.readSnDPORT(i));
		Serial.println(")");
	}
}

//ethernet constants
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; 
//byte mac[] = { 0x6A, 0xB5, 0x99, 0xEE, 0xDF, 0x52 }; 
//byte ip[] = { 192, 168, 1, 56 }; //home 
byte ip[] = { 192, 168, 1, 55 };	//badi brugg
// NTP Servers:
IPAddress timeServer(162, 23, 41, 34); // 162.23.41.34
char server[] = "temp.simonwyss.me";

//time constants
const int timeZone = 0;     // Central European Time
//init Daylight Saving Time Rules
TimeChangeRule chEDT = {"chDT", Last, Sun, Mar, 2, +120};  //UTC + 2 hours, changed at local time 0200 to 0300
TimeChangeRule chEST = {"chST", Last, Sun, Oct, 3, +60};   //UTC + 1 hour, changed at local time 0300 to 0200
Timezone chZone(chEDT, chEST);

EthernetUDP Udp;
EthernetClient tempClient;
EthernetServer webServer(80);
unsigned int tempClientConnected = 0;
unsigned int localPort = 8888;  // local port to listen for UDP packets

//global Variables
time_t prevDisplay = 0; // when the digital clock was displayed
time_t utc = 295;
time_t chTime = 0;
time_t timer1 = 0;

unsigned long currentmillis = 0;

unsigned int measureOn = 100;

EthernetClient webClient;
unsigned int webClientConnected = 0;
boolean currentLineIsBlank = 0;

//debug code:
int NrOfErrors = 0;
int MaxNrOfErrors = 10;
int Errors[12] = {9,9,9,9,9,9,9,9,9,9,9,9}; //initialize all elements to 0
int ErrorBuffer = 0;
time_t timer2 = 0;
unsigned long connectTime[MAX_SOCK_NUM];
byte socketStat[MAX_SOCK_NUM];

void setup() 
{
	Serial.begin(115200);
	while (!Serial) ; // Needed for Leonardo only
	delay(250);
	Serial.println("TimeNTP Example");
	Ethernet.begin(mac, ip);
	Serial.print("IP number assigned  is ");
	Serial.println(Ethernet.localIP());
	
	ShowSockStatus();
	
	//init SPI
	pinMode(53, OUTPUT);  	//Hardware-SS, not Used, need to be set as OUTPUT
  
	pinMode(10, OUTPUT);      // Chipselect Pin of the Ethernet Chip W5100
	digitalWrite(10, HIGH);   // Turn chip off -> set pin high
  
	pinMode(4, OUTPUT);		// Chipselect Pin of SD Card
	digitalWrite(4, HIGH);	// Turn SD-Card off -> set pin high
	
	Serial.println("webserver.begin");
	
	ShowSockStatus();
	//Serial.print("freeMemory()= ");
	//Serial.println(freeMemory());
	
	Serial.println("waiting for sync");
	//setSyncProvider(getNtpTime);
	//utc = now();
	//timer1 = utc - 295;
	timer1 = 0;
	
	
	currentmillis = millis();
}

void loop()
{  
	if(millis() - currentmillis > 1000){
		utc++;
		currentmillis = millis();
		//Serial.print("Time");
	}
	//if 20 seconds passed, print all safed erros
	if((utc - timer2) > 10){
		timer2 = utc;
		Serial.println("");
		Serial.println("Safed Errors:");
		for(int i = 0; i<10; i++){
			Serial.print(Errors[i],DEC);
			Serial.println("");
		}
		Serial.println("End off safed Errors");
	}
	checkSockStatus();
	
	//Ethernet Client Loop (send Temp as JSON Object to a webserver), connect only to server, if no webclient is connected to the arduino server
	//only 3 sockets are available
	if(((utc - timer1) > 300) && (webClientConnected == 0) ){
		timer1 = utc;
		ErrorBuffer = tempClient.connect(server, 80);
		if (ErrorBuffer) {
			Serial.println("tempClient.connect");
			ShowSockStatus();
			tempClientConnected = 1;
			Serial.println("connected");
			//construckt post data:
			//String PostData = "param1=44";
			//sensors.requestTemperatures(); // Send the command to get temperatures
			//delay(10);
			//float fTemp = sensors.getTempCByIndex(0);
			
			if (Sensor1.getTemperture(&temperature)) {
				Temperatur_C = Sensor1.calc_Celsius(&temperature);
	
			}
			
			if (Sensor2.getTemperture(&temperature2)) {
				Temperatur_C2 = Sensor2.calc_Celsius(&temperature2);

			}
			
			
			Serial.print("Temperature1:");
			Serial.println(Temperatur_C);
			Serial.print("Temperature2:");
			Serial.println(Temperatur_C2);
			//Serial.print("freeMemory()= ");
			//Serial.println(freeMemory());
			
			//int fTemp = 22;
			char sbuffer[150];
			//sprintf(sbuffer, "{\"badid\": \"44\", \"pincode\": \"12345\", \"timestamp\": \"%02d:%02d:%02d %d %d %d\", \"temp\": {\"129\": \"%d\"}}", hour(chTime), minute(chTime), second(chTime), day(chTime), month(chTime), year(chTime), fTemp); 
			sprintf(sbuffer, "{\"badid\": \"44\", \"pincode\": \"12345\", \"temp\": {\"129\": \"22.19\", \"130\": \"22.19\" }}"); 
			
			// Make a HTTP request:
			//tempClient.println("GET /phptest/index.php?param1=44&param2=wow HTTP/1.1");
			tempClient.println("POST /temp1.php HTTP/1.1");
			tempClient.println("Host: temp.simonwyss.me");
			//tempClient.println("Connection: close");
			tempClient.println("Content-Type: application/json");
			tempClient.print("Content-Length: ");
			tempClient.println(strlen(sbuffer));
			tempClient.println();
			//tempClient.println(sbuffer);
			tempClient.print("{\"badid\": \"44\", \"pincode\": \"12345\", \"temp\": {\"129\": \"");
			tempClient.print(Temperatur_C);
			tempClient.print("\", \"130\": \"");
			tempClient.print(Temperatur_C2);
			tempClient.print("\"}}");
			tempClient.println();
			tempClient.println();
		}
		else {
			Serial.println("connection failed");	//error occured: 19.03.2015 09:38
			if(NrOfErrors < MaxNrOfErrors){	//safe error to Erros[]
				Errors[NrOfErrors] = ErrorBuffer;
				NrOfErrors++;
				ErrorBuffer = 0;
			}
			//close connnection:
			tempClient.flush();
			tempClient.stop();
			tempClientConnected = 0;
			ShowSockStatus();
		}
	}
	if(tempClientConnected == 1){
		if (tempClient.available()) {
			char c = tempClient.read();
			Serial.print(c);
		}
		//toDo: timeout einbauen, das socket schliesst, oder globale socket überprüfung, die sockets
		//die älter als 30 sekunden sind, schliesst.
		// if the server's disconnected, stop the client:
		if (!tempClient.connected()) {
			tempClientConnected = 0;
			Serial.println();
			Serial.println("disconnecting.");
			tempClient.flush();
			tempClient.stop();
			Serial.println("tempClient disconnect");
			ShowSockStatus();
		}
	}
	
  
}//end of Loop

void digitalClockDisplay(){
  // digital clock display of the time
  Serial.print(hour(chTime));
  printDigits(minute(chTime));
  printDigits(second(chTime));
  Serial.print(" ");
  Serial.print(day(chTime));
  Serial.print(" ");
  Serial.print(month(chTime));
  Serial.print(" ");
  Serial.print(year(chTime)); 
  Serial.println(); 
}

void printDigits(int digits){
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
	//Serial.println("\nparse Udp packet");
    int size = Udp.parsePacket();
	//Serial.println("parse Packet");
	//ShowSockStatus();
	//Serial.println("\nUdp packet parsed!");
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{

  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
	//Serial.println("\nBegin Udp packet send");  
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  //Serial.println("write Packet");
  //ShowSockStatus();
  Udp.endPacket();
  Serial.println("end Packet");
  ShowSockStatus();
  //Serial.println("\nNTP-Packet sent!");
}

void checkSockStatus()
{
  unsigned long thisTime = millis();

  for (int i = 0; i < MAX_SOCK_NUM; i++) {
    uint8_t s = W5100.readSnSR(i);

    if((s == 0x17) || (s == 0x1C)) {
        if(thisTime - connectTime[i] > 16000UL) { //16 seconds
          Serial.print(F("\r\nSocket frozen: "));
          Serial.println(i);
          close(i);
					tempClientConnected = 0;
					if(NrOfErrors < MaxNrOfErrors){	//safe error to Erros[]
						Errors[NrOfErrors] = 7;
						NrOfErrors++;
						ErrorBuffer = 0;
					}
				}
    }
    else connectTime[i] = thisTime;

    socketStat[i] = W5100.readSnSR(i);
  }
}