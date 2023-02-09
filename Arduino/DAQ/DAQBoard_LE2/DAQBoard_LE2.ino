/*
This code runs on the DAQ ESP32 and has a couple of main tasks.
1. Read sensor data
2. Send sensor data to COM ESP32
3. Actuate hotfire sequence
*/

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Arduino.h>
#include "HX711.h"

// USER DEFINED PARAMETERS FOR TEST/HOTFIRE //
#define pressureFuel 450    //In units of psi. Defines set pressure for fuel
#define pressureOx 450    //In units of psi. Defines set pressure for ox
#define tolerance 0.10   //Acceptable range within set pressure
#define pressureDelay 0.5   //Sets percentage of time bang-bang is open
int measurementDelay = 50; //Sets frequency of data collection. 1/(measurementDelay*10^-3) is frequency in Hz
// END OF USER DEFINED PARAMETERS //

// Set sensor pinouts //
#define PTOUT1 32
#define CLKPT1 5
#define PTOUT2 15
#define CLKPT2 2
#define PTOUT3 22
#define CLKPT3 23
#define PTOUT4 19
#define CLKPT4 21
#define PTOUT5 35
#define CLKPT5 25
#define LCOUT1 34
#define CLKLC1 26
#define LCOUT2 39
#define CLKLC2 33
#define LCOUT3 38
#define CLKLC3 41

#define TC1 10
#define TC2 15

#define capSens1 40
#define capSens2 13
// End of sensor pinouts //

// Relays pinouts. Use for solenoidPinFuel, solenoidPinOx, fuelSolVent, oxSolVent, oxQD, and fuelQD // 
#define RELAYPIN1 14
#define RELAYPIN2 27
#define RELAYPIN3 28
#define RELAYPIN4 35
#define RELAYPIN5 55
#define RELAYPIN6 43
#define RELAYPIN7 95
#define RELAYPIN8 24
// End of relay pinouts //

String serialMessage;

// Initialize the PT and LC sensor objects which use the HX711 breakout board
HX711 scale1;
HX711 scale2;
HX711 scale3;
HX711 scale4;
HX711 scale5;
HX711 scale6;
HX711 scale7;
HX711 scale8;
// End of HX711 initialization

///////////////
//IMPORTANT
//////////////
// REPLACE WITH THE MAC Address of your receiver

//OLD COM BOARD {0xC4, 0xDD, 0x57, 0x9E, 0x91, 0x6C}
// COM BOARD {0x7C, 0x9E, 0xBD, 0xD7, 0x2B, 0xE8}
//HEADERLESS BOARD {0x7C, 0x87, 0xCE, 0xF0 0x69, 0xAC}
//NEWEST COM BOARD IN EVA {0x24, 0x62, 0xAB, 0xD2, 0x85, 0xDC}
// uint8_t broadcastAddress[] = {0x24, 0x62, 0xAB, 0xD2, 0x85, 0xDC};
uint8_t broadcastAddress[] ={0x7C, 0x9E, 0xBD, 0xD7, 0x2B, 0xE8};
// {0x7C, 0x87, 0xCE, 0xF0, 0x69, 0xAC};
//{0x3C, 0x61, 0x05, 0x4A, 0xD5, 0xE0};
// {0xC4, 0xDD, 0x57, 0x9E, 0x96, 0x34}

//STATEFLOW VARIABLES
enum STATES {IDLE, ARMED, FILL, PRESS_ETH, PRESS_LOX, QD, IGNITION, HOTFIRE, ABORT, DEBUG=99};
int state;
int loopStartTime=0;
int lastPrintTime=0;

int lastMeasurementTime=-1;
short int queueLength=0;
int commandedState;

int igniterTime=750;
int hotfireTimer=0;
int igniterTimer=0;

float startTime;
float endTime;
float timeDiff;

// Variable to store if sending data was successful
String success;

// Define variables to store readings to be sent
int messageTime=10;
int readingPT1=1;
int readingPT2=1;
int readingPT3=1;
int readingPT4=1;
int readingPT5=1;
int readingLC1=1;
int readingLC2=1;
int readingLC3=1;
int readingTC1=1;
int readingTC2=1;
float readingCap1=0;
float readingCap2=0;
short int queueSize=0;
bool fillComplete=false;
bool pressEthComplete=false;
bool pressLOXComplete=false;

//Structure example to send data
//Must match the receiver structure
typedef struct struct_message {
    int messageTime;
    int pt1;  
    int pt2; 
    int pt3;  
    int pt4;  
    int pt5;  
    int lc1; 
    int lc2;
    int lc3;
    int tc1;
    int tc2;
    float cap1;
    float cap2;
    int commandedState;
    short int queueSize;
    bool fillComplete;
    bool pressEthComplete;
    bool pressLOXComplete;
} struct_message;

// Create a struct_message called Readings to hold sensor readings
struct_message Readings;
//create a queue for readings in case
struct_message ReadingsQueue[120];

// Create a struct_message to hold incoming commands
struct_message Commands;

esp_now_peer_info_t peerInfo;

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  sendTime = millis();
}

// Callback when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&Commands, incomingData, sizeof(Commands));
  commandedState = Commands.commandedState;
}

// Initialize all sensors and parameters
void setup() {
  // pinMode(ONBOARD_LED,OUTPUT);
  Serial.begin(115200);

  pinMode(RELAYPIN1, OUTPUT);
  pinMode(RELAYPIN2, OUTPUT);
  pinMode(RELAYPIN3, OUTPUT);
  pinMode(RELAYPIN4, OUTPUT);
  pinMode(RELAYPIN5, OUTPUT);
  pinMode(RELAYPIN6, OUTPUT);
  pinMode(RELAYPIN7, OUTPUT);
  pinMode(RELAYPIN8, OUTPUT);

  //EVERYTHING SHOULD BE WRITTEN HIGH EXCEPT QDs, WHICH SHOULD BE LOW
  digitalWrite(RELAYPIN1, HIGH);
  digitalWrite(RELAYPIN2, HIGH);
  digitalWrite(RELAYPIN3, HIGH);
  digitalWrite(RELAYPIN4, HIGH);
  digitalWrite(RELAYPIN5, HIGH);
  digitalWrite(RELAYPIN6, HIGH);
  digitalWrite(RELAYPIN7, HIGH);
  digitalWrite(RELAYPIN8, HIGH);

  //set gains for pt pins
  scale1.begin(PTOUT1, CLKPT1); scale1.set_gain(64);
  scale2.begin(PTOUT2, CLKPT2); scale2.set_gain(64);
  scale3.begin(PTOUT3, CLKPT3); scale3.set_gain(64);
  scale4.begin(PTOUT4, CLKPT4); scale4.set_gain(64);
  scale5.begin(PTOUT5, CLKPT5); scale5.set_gain(64);
  scale6.begin(LCOUT1, CLKLC1); scale6.set_gain(64);
  scale7.begin(LCOUT2, CLKLC2); scale7.set_gain(64);
  scale8.begin(LCOUT3, CLKLC3); scale8.set_gain(64);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  //Print MAC Accress on startup for easier connections
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(OnDataSent);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  // Register for a callback function that will be called when data is received
  esp_now_register_recv_cb(OnDataRecv);

  sendTime = millis();
  state = IDLE;
}

// Implementation of State Machine
void loop() {
  loopStartTime=millis();

  switch (state) {

  case (IDLE):
    idle();
    if (commandedState==ARMED) {state=ARMED;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (ARMED): //NEED TO ADD TO CASE OPTIONS //ALLOWS OTHER CASES TO TRIGGER //INITIATE TANK PRESS LIVE READINGS
    armed();
    if (commandedState==FILL) {state=FILL;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (FILL):
    fillComplete = fill();
    if (commandedState==PRESS_ETH && fillComplete) {state=PRESS_ETH;}
    if (commandedState==IDLE) {state = IDLE;}
    if (commandedState==ABORT) {state = ABORT;}
    break;

  case (PRESS_ETH):
    pressEthComplete = press_eth();
    if (pressEthComplete) {state=PRESS_LOX;}
    if (commandedState==IDLE) {state=IDLE;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (PRESS_LOX):
    pressLOXComplete = press_lox();
    if (commandedState==QD && pressLOXComplete) {state=QD;}
    if (commandedState==IDLE) {state=IDLE;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (QD):
    quick_disconnect();
    if (commandedState==IGNITION) {state=IGNITION;}
    if (commandedState==IDLE) {state=IDLE;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (IGNITION): 
    ignition();
    if (commandedState==HOTFIRE) {state=HOTFIRE;}
    if (commandedState==IDLE) {state=IDLE;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (HOTFIRE): 
    hotfire();
    if (commandedState==IDLE) {state=IDLE;}
    if (commandedState==ABORT) {state=ABORT;}
    break;

  case (ABORT):
    abort_sequence();
    if (commandedState==IDLE) {state=IDLE;}
    break;

  case (DEBUG):
    debug();
    if (commandedState==IDLE) {state=IDLE;}
    break;
  }
}


/// STATE FUNCTION DEFINITIONS ///

void idle() {
  sendData();
}

void armed() {
  sendData();
}

bool fill() {
  //FILL IN
}

bool press_eth() {
  // Increase pressure
  while (Readings.pt1val < pressureFuel) {
    openSolenoidFuel();
    if (commandedState = 1) {
      closeSolenoidFuel();
      ventFuel();
      state = 1;
      return false;
    }
  }
  closeSolenoidFuel();
  // Address potential overshoot & hold pressure (within tolerance)
  sleep(pressureDelay);
  if (Readings.pt1val > (1+tolerance)*pressureFuel) {
    while (Readings.pt1val > pressure) {
      openSolenoidFuel();
      if (commandedState = 1) {
        closeSolenoidFuel();
        ventFuel();
        state = 1;
        return false;
      }
    }
    closeSolenoidFuel();
    sleep(pressureDelay);
    if (Readings.pt1val < (1-tolerance)*pressureFuel) {
      pressurizeFuel();
    }
  }
  return true;
}

bool press_lox() {
  // Increase pressure
  while (Readings.pt2val < pressureOx) {
    openSolenoidOx();
    if (commandedState = 1) {
      closeSolenoidOx();
      ventOx();
      state = 1;
      return false;
    }
  }
  closeSolenoidOx();
  // Address potential overshoot & hold pressure (within tolerance)
  sleep(pressureDelay);
  if (Readings.pt1val > (1+tolerance)*pressureOx) {
    while (Readings.pt1val > pressure) {
      openSolenoidOx();
      if (commandedState = 1) {
        closeSolenoidOx();
        ventOx();
        state = 1;
        return false;
      }
    }
    closeSolenoidOx();
    sleep(pressureDelay);
    if (Readings.pt1val < (1-tolerance)*pressureOx) {
      pressurizeOx();
    }
  }
  return true;
}

void quick_disconnect() {
  // FILL IN
}

void ignition() {
  if ((loopStartTime-igniterTimer) < igniterTime) { digitalWrite(RELAYPIN1, LOW); digitalWrite(RELAYPIN2, LOW); Serial.print("IGNITE"); }
  if ((loopStartTime-igniterTimer) > igniterTime) { digitalWrite(RELAYPIN1, HIGH); digitalWrite(RELAYPIN2, HIGH); Serial.print("NO"); }
  sendData();

  Serial.println(loopStartTime-igniterTimer);
  Serial.println("Igniter time");
  Serial.println(igniterTime);
  Serial.println(" ");
}

void hotfire() {
  //FILL IN
}

void abort_sequence() {
  //FILL IN
}

void debug() {
  //FILL IN
}


/// END OF STATE FUNCTION DEFINITIONS ///



/// HELPER FUNCTIONS ///

void openSolenoidFuel() {
  digitalWrite(solenoidPinFuel, LOW);
}

void closeSolenoidFuel() {
  digitalWrite(solenoidPinOx, HIGH);
}

void openSolenoidOx() {
  digitalWrite(solenoidPinOx, LOW);
}

void closeSolenoidOx() {
  digitalWrite(solenoidPinOx, HIGH);
}

void vent() {
  ventOx();
  ventFuel();
}

void ventFuel() {
  digitalWrite(fuelSolVent, LOW);
}

void ventOx() {
  digitalWrite(oxSolVent, LOW);
}

void disconnectOx() {
  digitalWrite(oxQD, HIGH);
}

void disconnectFuel() {
  digitalWrite(fuelQD, HIGH);
}

/// END OF HELPER FUNCTIONS ///


/// DATA LOGGING AND COMMUNICATION ///

void sendData() {
  if ((loopStartTime-lastMeasurementTime)>measurementDelay) { 
    addReadingsToQueue();
    sendQueue();
  }
}

void addReadingsToQueue() {
  getReadings();
  if (queueLength<40) {
    queueLength+=1;
    ReadingsQueue[queueLength].messageTime=loopStartTime;
    ReadingsQueue[queueLength].pt1=readingPT1;
    ReadingsQueue[queueLength].pt2=readingPT2;
    ReadingsQueue[queueLength].pt3=readingPT3;
    ReadingsQueue[queueLength].pt4=readingPT4;
    ReadingsQueue[queueLength].pt5=readingPT5;
    ReadingsQueue[queueLength].lc1=readingLC1;
    ReadingsQueue[queueLength].lc2=readingLC2;
    ReadingsQueue[queueLength].lc3=readingLC3;
    ReadingsQueue[queueLength].tc1=readingTC1;
    ReadingsQueue[queueLength].tc2=readingTC2;
    ReadingsQueue[queueLength].cap1=readingCap1;
    ReadingsQueue[queueLength].cap2=readingCap2;
    ReadingsQueue[queueLength].queueSize=queueLength;
  }
}

void getReadings(){

  readingPT1 = scale1.read(); 
  readingPT2 = scale2.read(); 
  readingPT3 = scale3.read(); 
  readingPT4 = scale4.read(); 
  readingPT5 = scale5.read(); 
  readingLC1 = scale6.read(); 
  readingLC2 = scale7.read();
  readingLC3 = scale8.read();
  readingTC1 = analogRead(TC1);
  readingTC2 = analogRead(TC2);
  readingCap1 = analogRead(capSens1);
  readingCap2 = analogRead(capSens2);

  printSensorReadings();
  lastMeasurementTime=loopStartTime;
}

void printSensorReadings() {
   serialMessage = "";
 //
 serialMessage.concat(millis());
 serialMessage.concat(" ");
 serialMessage.concat(readingPT1);
 serialMessage.concat(" ");
 serialMessage.concat(readingPT2);
 serialMessage.concat(" ");
 serialMessage.concat(readingPT3);
 serialMessage.concat(" ");
 serialMessage.concat(readingPT4);
 serialMessage.concat(" ");
 serialMessage.concat(readingPT5);
 serialMessage.concat(" ");
 serialMessage.concat(readingLC1);
 serialMessage.concat(" ");
 serialMessage.concat(readingLC2);
 serialMessage.concat(" ");
 serialMessage.concat(readingLC3);
 serialMessage.concat(" ");
 serialMessage.concat(readingTC1);
 serialMessage.concat(" ");
 serialMessage.concat(readingTC2);
 serialMessage.concat(" ");
 serialMessage.concat(readingCap1);
 serialMessage.concat(" ");
 serialMessage.concat(readingCap2);
 serialMessage.concat(" Queue Length: ");
 serialMessage.concat(queueLength);
 Serial.println(serialMessage);
}

void sendQueue() {
  if (queueLength>0){
    dataSend();
  }
}

void dataSend() {
   // Set values to send
  Readings.messageTime=ReadingsQueue[queueLength].messageTime;
  Readings.pt1 = ReadingsQueue[queueLength].pt1;
  Readings.pt2 = ReadingsQueue[queueLength].pt2;
  Readings.pt3 = ReadingsQueue[queueLength].pt3;
  Readings.pt4 = ReadingsQueue[queueLength].pt4;
  Readings.pt5 = ReadingsQueue[queueLength].pt5;
  Readings.lc1 = ReadingsQueue[queueLength].lc1;
  Readings.lc2 = ReadingsQueue[queueLength].lc2;
  Readings.lc3  = ReadingsQueue[queueLength].lc3;
  Readings.tc1 = ReadingsQueue[queueLength].tc1;
  Readings.tc2 = ReadingsQueue[queueLength].tc2;
  Readings.cap1 = ReadingsQueue[queueLength].cap1;
  Readings.cap2 = ReadingsQueue[queueLength].cap2;

  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &Readings, sizeof(Readings));

  if (result == ESP_OK) {
     Serial.println("Sent with success Data Send");
   //  ReadingsQueue[queueLength].pt1val=0;
     queueLength-=1;
  }
  else {
     Serial.println("Error sending the data");
  }
}