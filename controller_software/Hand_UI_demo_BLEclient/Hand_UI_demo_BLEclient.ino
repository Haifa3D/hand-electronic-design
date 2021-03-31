/*
 * This code is a demo code for any wireless system which translates the user's desires 
 * into hand movements. This code allows you to control the Oded Hand - a low cost, open 
 * source prosthetic hand that anyone can build (https://github.com/Haifa3D) via BLE 
 * protocl we developed for this pupose (For more information about the BLE protocol see 
 * https://github.com/Haifa3D/haifa3d-hand-app/blob/master/Protocol.md) 
 * 
 * In general, the Hand itself is the server and you UI system is the client.
 * Here we show how to communicate with the hand using esp32 controller, and
 * go to sleep if no connection was established
 * 
 * Getting started (for ESP32-based system): 
 * add your device address (under "BlE Parameters ")
 * add your system parameters - HW and SW (under "Your System Setting")
 * add in the "setup loop" your system setup code.
 * add your system functions (under "Your System Functions")
 * 
 * now you need to decide how to communicate with the hand - you can use
 * the 'Execute Characteristic' (i.e. pRemoteCharExecute) and/or the 'Trigger 
 * Characteristic' (i.e. pRemoteCharTrigger).
 * For pRemoteCharExecute: 
 *      unsigned char msg =  {5, 0b11111000, 20, 0b01111000, 0b00000000}; //movement length byte,torque,time,active motor, motor direction 
        pRemoteCharExecute->writeValue(msg,msg[0]);
 *      (for more detail see our BLE protocol at the link above)
 * For pRemoteCharTrigger: 
 *      uint8_t preset_id = 0;  //can be 0-11 since we have 12 defined presets in the Oded Hand
        pRemoteCharTrigger->writeValue(&preset_id,1);
        (to define your presets check out our mobile app: https://play.google.com/store/apps/details?id=com.gjung.haifa3d)
 *
 * Here you can see our system demo for a LegSwitch - the switch is conncted to GPIO 4.
 * If the switch has pressed two short and successive times the system triggers 'preset 0' (which we defined in the app as close all fingers)
 * If the switch has pressed three times the system sends an open task via the 'Execute Characteristic'.
 * 
 */

 /***********************************************************/
/*                Libraries                                */
/***********************************************************/
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>

/*************************************************************************************/
/*                                  BlE Parameters                                   */
/*************************************************************************************/
#define MY_DEVICE_ADDRESS  "24:62:ab:f2:af:46" // add here you devicec address

#define HAND_DIRECT_EXECUTE_SERVICE_UUID     "e0198000-7544-42c1-0000-b24344b6aa70"
#define EXECUTE_ON_WRITE_CHARACTERISTIC_UUID "e0198000-7544-42c1-0001-b24344b6aa70"

#define HAND_TRIGGER_SERVICE_UUID            "e0198002-7544-42c1-0000-b24344b6aa70"
#define TRIGGER_ON_WRITE_CHARACTERISTIC_UUID "e0198002-7544-42c1-0001-b24344b6aa70"

static BLEUUID serviceExeUUID(HAND_DIRECT_EXECUTE_SERVICE_UUID);// The remote service we wish to connect to.
static BLEUUID    charExeUUID(EXECUTE_ON_WRITE_CHARACTERISTIC_UUID);// The characteristic of the remote service we are interested in - execute.

static BLEUUID serviceTrigUUID(HAND_TRIGGER_SERVICE_UUID);// The remote service we wish to connect to.
static BLEUUID    charTrigUUID(TRIGGER_ON_WRITE_CHARACTERISTIC_UUID);// The characteristic of the remote service we are interested in - trigger

// Connection parameters:
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = true;
static BLERemoteCharacteristic* pRemoteCharExecute;
static BLERemoteCharacteristic* pRemoteCharTrigger;
static BLEAdvertisedDevice* myDevice;

unsigned char* msg;
uint8_t preset_id;
/*************************************************************************************/
/*                 Go to Sleep and Keep Scanning Setting:                            */
/*************************************************************************************/
#define DT_SCAN    5000 //if not connected to BLE device scan every 5 seconds.
#define DT_SLEEP    (10 * 60 * 1000) //if not connected to BLE device go to sleep after 10 minute.
unsigned long t_scan; //the elapsed time between BLE scanning
unsigned long t_disconnected; //the elapsed time from a disconecting event

/*************************************************************************************/
/*                              Your System Setting:                                 */
/*************************************************************************************/
#define SwitchLeg_PRESET_TRIGGER 0
#define CLICK_TIME        1000   // the max time for a single click
#define NOISE_TIME        50     // the min time for a single click
#define BTWN_CLICKS_TIME  450  //the max time between two successive clicks
#define CLICKS_NUM        2     // the clicks counter variable
//switch parameters:
const int buttonPin = 4; // the pin which is connected to the button
bool button_state = LOW; // the current state of the button
bool button_last = LOW; // the last state of the button
unsigned long StartTime = millis(); // the time when a click was started
unsigned long EndTime = millis();  //the time when a single click was ended

int NumClick = 0; // the clicks counter variable
int pre_NumClick = 0; // the previous clicks counter variable

bool is_open_action = true;
unsigned char close_task[] =  {5, 0b11111000, 20, 0b01111000, 0b11111000}; //movement length byte, torque,time,active motor, motor direction
unsigned char open_task[] =  {5, 0b11111000, 20, 0b01111000, 0b00000000}; //movement length byte, torque,time,active motor, motor direction

/*************************************************************************************/
/*                  BLE Class Definition and Set Callbacks:                          */
/*************************************************************************************/
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
    doScan = true;
    t_disconnected = millis();
  }
};

bool connectToServer() {
  
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Execute Charachteristics:
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteExeService = pClient->getService(serviceExeUUID);
    if (pRemoteExeService == nullptr) {
      Serial.print("Failed to find our Execute service UUID: ");
      Serial.println(serviceExeUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Execute service");
    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharExecute = pRemoteExeService->getCharacteristic(charExeUUID);
    if (pRemoteCharExecute == nullptr) {
      Serial.print("Failed to find our Execute characteristic UUID: ");
      Serial.println(charExeUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Execute characteristic");
 
    // Read the value of the characteristic.
    if(pRemoteCharExecute->canRead()) {
      std::string value = pRemoteCharExecute->readValue();
      Serial.print("Execute: The characteristic value was: ");
      Serial.println(value.c_str());
    }

    // Trigger Charachteristics:
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteTrigService = pClient->getService(serviceTrigUUID);
    if (pRemoteTrigService == nullptr) {
      Serial.print("Failed to find our Trigger service UUID: ");
      Serial.println(serviceTrigUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Trigger service");
    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharTrigger = pRemoteTrigService->getCharacteristic(charTrigUUID);
    if (pRemoteCharTrigger == nullptr) {
      Serial.print("Failed to find our Trigger characteristic UUID: ");
      Serial.println(charTrigUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our Trigger characteristic");
 
    // Read the value of the characteristic.
    if(pRemoteCharTrigger->canRead()) {
      std::string value = pRemoteCharTrigger->readValue();
      Serial.print("Trigger: The characteristic value was: ");
      Serial.println(value.c_str());
    }

    connected = true;
}

// Scan for BLE servers and find the first one that advertises the service we are looking for.
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {  // Called for each advertising BLE server.
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (((String)advertisedDevice.getAddress().toString().c_str()).equals(MY_DEVICE_ADDRESS)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

void InitBLE() {
  BLEDevice::init("SwitchLeg");
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(1, false);
}

/******************************************************************************/
/*                          Setup Loop:                                       */
/******************************************************************************/
void setup() {
  Serial.begin(115200);
 
  // Create the BLE Device
  InitBLE();
  t_scan = millis();

  // enable deep sleep mode for the esp32:
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1); //1 = High, 0 = Low , same GPIO as the button pin
  t_disconnected = millis();

  // add here your system setup:
  pinMode(buttonPin,INPUT); // Check Leg SwitchStatus
}

/******************************************************************************/
/*                      Your System Functions:                                */
/******************************************************************************/
// Check the leg switch status
bool CheckLegSwitch(){
  bool DoAction = false;
  // change action variable to 1 if an event (i.e. 2 successive clicks) is identified:
  button_state = digitalRead(buttonPin);
  //Serial.println(button_state);//debugging
  if (button_state != button_last){
    //identify button status:
    if (button_last == LOW && button_state == HIGH){//a click was started
      StartTime = millis();
      if (StartTime - EndTime > BTWN_CLICKS_TIME) NumClick = 0; //too much time has passed between successive clicks
      //Serial.print("Number of clicks ");Serial.println(NumClick);//debugging
    }
    else if ( button_last == HIGH && button_state == LOW){//a click was finished
      EndTime = millis();
      if (  (EndTime-StartTime) > NOISE_TIME && (EndTime-StartTime) < CLICK_TIME){ //a "legal" click was detected
        NumClick ++; 
        //Serial.print("Number of clicks ");Serial.println(NumClick);//debugging
      }
    }
    //updating switch status:
    button_last = button_state;
  }
  else if (millis() - EndTime > BTWN_CLICKS_TIME) NumClick = 0; //too much time has passed from last click

  // check the clicks status:
    /* the options are: 
     *  do nothing: [0,1], [1,0],[3,0]
     *  check if additional click was made: [1,2]
     *  close hand: [2,0] - two clicks
     *  open hand: [2,3] - three clicks
     */
  if (pre_NumClick!=NumClick){ //the status was changed
    Serial.printf("pre_NumClick: %i, NumClick: %i\n",pre_NumClick,NumClick);
    if (pre_NumClick==CLICKS_NUM){
      DoAction = true;
      if (NumClick>0) is_open_action = true;
      else is_open_action = false;
    }
    pre_NumClick=NumClick; // updating clicks status  
  }

  return DoAction;
}

/******************************************************************************/
/*                          Main Loop:                                        */
/******************************************************************************/
void loop() {

  if (doConnect == true) { //TRUE when we scanned and found the desired BLE server
    if (connectToServer()) Serial.println("We are now connected to the BLE Server."); // connect to the server. TRUE if connection was established
    else Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    doConnect = false; //no need to reconnect to the server
  }

  if (connected) { //TRUE if we are already connected to the server
    // Read the updated value of the characteristic.
    if (CheckLegSwitch()){ //read the data from the sensor only when notification has sent to reduce power consumption. 
      // Set the characteristic's value
      
      if (is_open_action) {
        msg = open_task;
        pRemoteCharExecute->writeValue(msg,msg[0]);
        Serial.println("OPEN Message was sent");
      }
      else {
        preset_id = SwitchLeg_PRESET_TRIGGER;
        pRemoteCharTrigger->writeValue(&preset_id,1);
        Serial.println("CLOSE Message was sent");
      }
      
      delay(3); // bluetooth stack will go into congestion, if too many packets are sent
    }         
  }
  else { //not connected
    //scanning for server:
    if((millis()-t_scan>DT_SCAN)){ //not connected
      //BLEDevice::getScan()->start(0);  // start to scan after disconnect. 0 - scan for infinity, 1-1 sec, ..
      Serial.println("Scanning...");
      BLEDevice::getScan()->start(1, false);
      t_scan = millis();
    }
    // going to sleep if long time without connection
    else if (millis()-t_disconnected > DT_SLEEP){
      //Go to sleep now
      Serial.println("Going to sleep now");
      esp_deep_sleep_start();
    }
  }
}
