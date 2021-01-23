/*
 * This code allows you to control the Oded Hand - a low cost, open source prosthetic hand that anyone can build
 * 
 * The event holder here assigns tasks for each event. The structures is as follow:
 * task_type 0   :   do nothing
 * task_type 1   :   do action according to preset data
 * task_type 2   :   save new preset according to preset data
 * task_type 3   :   save new config according to config id
 * 
 * For more information about the BLE protocol see https://github.com/Haifa3D/haifa3d-hand-app/blob/master/Protocol.md
 * For more information about the Hardware, circuit and wiring see https://github.com/Haifa3D/hand-electronic-design
 * 
 * If you see the battery status is not correct go to the buttery function parameters and change the min and max value.
 * If you want to check the battery status faster, go to the Software setting, battery function
 * If you want to add/reduce number of buttons add at the Hardware button sections.
 * If you want to change the Button long click time, change the DT_LONG_CLICK
 * If you see the motors are not moving correctly (moving in the reversed directions or wrong motor is moving, go to Hardware motor connection
 * 
 */

/***********************************************************/
/*                Libraries                                */
/***********************************************************/
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h> //Library to use BLE as server
#include <BLE2902.h> 
#include <bitset>
#include <EEPROM.h> // for ESP32 flash memory
#include <analogWrite.h> //PWM options

/*************************************************************************************/
/*                BlE Parameters and General Setting:                                */
/*************************************************************************************/
#define PRESETS_NUM 12  // number of presets, save on the ocntroller
#define CONFIG_NUM 13  //number of config parameters, save on the controller
#define CONFIG_TRIG_NUM 2  // number of config trigger, excute action without saving data on the controller
#define MAX_MOV 10  //maximal movements per presents, decided due to the limited amount of flsuh memory
#define ACTION_BYTES 4  // number of byte per action
#define MIN_PRESET_BYTES (1+ACTION_BYTES)  //one movements per preset
#define MAX_PRESET_BYTES (1+(MAX_MOV*ACTION_BYTES))  //MAX_MOV movements per preset
#define CONFIG_BYTES 20  // maximal number of config bytes. currently we utilized 13 config paramerters each param is 1 byte.
#define EEPROM_SIZE 512  // number of bytes we'll need to access in the flash memory. each char is 1 byte
#define TIME_UNITS 50  //one time units equals to TIME_UNITS[msec]
#define MOTORS_NUM 5  // number of motors connected to the hand

/*************************************************************************************/
/*                          HardWare Setting:                                        */
/*************************************************************************************/
// motors Connection - change between IN1 and IN2 if the motor moves at the reverse direction 
// and change between groups (each group consists of 3 values: IN1, IN2, ISENSE) if the wrong motor is moving.
#define W_IN1     26 
#define W_IN2     27 
#define W_ISENSE  36 

#define F1_IN1    19 
#define F1_IN2    21 
#define F1_ISENSE 34 

#define F2_IN1    23 
#define F2_IN2    22 
#define F2_ISENSE 35 

#define F3_IN1    4  
#define F3_IN2    16 
#define F3_ISENSE 32 

#define F4_IN1    18 
#define F4_IN2    17 
#define F4_ISENSE 33 

const int all_motors[MOTORS_NUM*2]={W_IN1,W_IN2,F1_IN1,F1_IN2,F2_IN1,F2_IN2,F3_IN1,F3_IN2,F4_IN1,F4_IN2};
const int all_controls[MOTORS_NUM*2]={W_ISENSE,F1_ISENSE,F2_ISENSE,F3_ISENSE,F4_ISENSE};

#define MODES_NUM 4 // 0 = long click modes, 1 = mode1, ..., currently we have 4 mode
#define MOV_PER_MODE 3  //currnely we have 3 movements per mode, total of 12 presets
#define BUTTONS_NUM 1  // depends on the circuit version, here we have only one button connected

#define BUTTON_MODE 13 //mode button
//#define BUTTON1     2  // button of movement 1
//#define BUTTON2     25  // button of movement 1
//#define BUTTON3     12 // button of movement 1
const int all_buttons[BUTTONS_NUM] = {BUTTON_MODE}; //{BUTTON_MODE, BUTTON1,BUTTON2,BUTTON3};

#define LEDS_NUM 3  //RGB led

#define LED1 5 //Red
#define LED2 14 //Green
#define LED3 15 //Blue
const int all_leds[LEDS_NUM] = {LED1,LED2,LED3};

const int battery_pin = 39; //GPIO 39 is connected to the 18650 Li-Ion battery with a voltage devider 1/3*Vin
 
/*************************************************************************************/
/*                          SoftWare Setting:                                        */
/*************************************************************************************/
// moving average
#define MAX_WINDOW 20  //the maximsl length of the smoothing window

// setting PWM properties for the leds
const int freq = 5000;  //[Hz]
const int resolution = 8;  //[bits] value is from 0 to 255

// buttons function - parameters and declaration:
#define DT_LONG_CLICK 1000  //[msec] the threshold time between short click and a long click
int button_state = 0; //0= none, 1= a click was stated, 2=still short click, 3= starting long click, 4=still long click, 5= end of a click.
int button_id = 0; //0=mode button, 1=button1, ...
int hand_mode = 0 ; // 0 = long click modes, 1 = mode1, 2 = mode2, ...
unsigned long t_press; // the time when a button press was strated

// battery function - parameters and declaration:
#define DT_BATTERY_STATUS  (10 * 60 * 1000) //check battery every 10 min
#define BATTERY_MIN 1512  //=~6v , 385 esp unit =~1v for ADC_11db
#define BATTERY_MAX 1985  //=~8.4v
unsigned long t_check_battery; //the elapsed time between battery status checks
uint8_t battery_level = 100; // battery status in [%]

// declaring config parameters:
int thr_value_L;
int thr_value_H;
int t_start;
int win_width; //prefered 2^x
int thr_slope_L;
int thr_slope_H;
int thr_factor[MOTORS_NUM+1];
unsigned char dv_config;
bool is_debug = false;

unsigned char all_config[CONFIG_NUM+1]; 
int config_id = 0;

// default values for config parameters:
// These parameters are the default values saved on the controller memeory
#define THR_VALUE_L 20  // [CURRENT_UNIT] current threshold value for LOW torque level
#define THR_VALUE_H 80  // [CURRENT_UNIT] current threshold value for HIGH torque level
#define T_START     150  // [msec] The time it takes to the initial high starting current to get back to normal
#define WIN_WIDTH   20  // [units] window width of the moving average filter
#define THR_SLOPE_L 10  //  [CURRENT_UNIT] the time [msec] threshold for increasing current for LOW torque level
#define THR_SLOPE_H 20  //  [CURRENT_UNIT] the time [msec] threshold for increasing current for LOW torque level
#define THR_FACTOR0 100  // [units] The multiplaction factor for the Wrist motor Thrshold values
#define THR_FACTOR1 150  // [units] The multiplaction factor for Finger1 motor Thrshold values
#define THR_FACTOR2 150  // [units] The multiplaction factor for Finger2 motor Thrshold values
#define THR_FACTOR3 100  // [units] The multiplaction factor for Finger3 motor Thrshold values
#define THR_FACTOR4 100  // [units] The multiplaction factor for Finger4 motor Thrshold values
#define DV_CONFIG   2  // [units] CURRENT_UNIT * DV_CONFIG = esp unit which is measured by the controller 
#define IS_DEBUG    0  // [bool] 0 - no debug mode, reduced comments, 1 - debug mode

const unsigned char all_config_default[CONFIG_NUM+1] = {THR_VALUE_L,THR_VALUE_H,T_START,WIN_WIDTH,THR_SLOPE_L,THR_SLOPE_H,
                                                         THR_FACTOR0,THR_FACTOR1,THR_FACTOR2,THR_FACTOR3,THR_FACTOR4,
                                                        DV_CONFIG,IS_DEBUG};
                                                        
// declaring BLE variables: all presentes
unsigned char current_preset[MAX_PRESET_BYTES];
unsigned char preset1[MAX_PRESET_BYTES];
unsigned char preset2[MAX_PRESET_BYTES];
unsigned char preset3[MAX_PRESET_BYTES];
unsigned char preset4[MAX_PRESET_BYTES];
unsigned char preset5[MAX_PRESET_BYTES];
unsigned char preset6[MAX_PRESET_BYTES];
unsigned char preset7[MAX_PRESET_BYTES];
unsigned char preset8[MAX_PRESET_BYTES];
unsigned char preset9[MAX_PRESET_BYTES];
unsigned char preset10[MAX_PRESET_BYTES];
unsigned char preset11[MAX_PRESET_BYTES];
unsigned char preset12[MAX_PRESET_BYTES];

unsigned char* all_presets[PRESETS_NUM+1]= {preset1,preset2,preset3,preset4,preset5,
                                            preset6,preset7,preset8,preset9,preset10,
                                            preset11,preset12,current_preset};

// default values:
// each preset is defined as following:
// movement length byte, torque,time[TIME_UNIT=50msec],active motor, motor direction
#define CLOSE_ALL_HIGH_TORQUE {5, 0b11111000, 20, 0b01111000, 0b11111000} 
#define OPEN_ALL              {5, 0b11111000, 20, 0b01111000, 0b00000000}
#define CLOSE_ALL_LOW_TORQUE  {5, 0b00000000, 20, 0b01111000, 0b11111000}
#define TRIPOD                {5, 0b00000000, 20, 0b01111000, 0b11100000}
#define TRIPOD_HIGH           {5, 0b11111000, 20, 0b01111000, 0b11100000}
#define PINCH                 {5, 0b00000000, 20, 0b01111000, 0b10100000}
#define PINCH_HIGH            {5, 0b11111000, 20, 0b01111000, 0b10100000}
#define POINTER               {5, 0b00000000, 20, 0b01111000, 0b10111000}
#define COOL                  {5, 0b00000000, 20, 0b01111000, 0b10110000}
#define THE_FINGER            {5, 0b00000000, 20, 0b01111000, 0b11011000}
#define TURN_RIGHT            {5, 0b00000000, 25, 0b10000000, 0b10110000}
#define TURN_LEFT             {5, 0b00000000, 25, 0b10000000, 0b00110000}

const unsigned char all_presets_default[PRESETS_NUM+1][ACTION_BYTES+1+1] = {CLOSE_ALL_HIGH_TORQUE,OPEN_ALL,CLOSE_ALL_LOW_TORQUE,
                                                           TRIPOD,TRIPOD_HIGH,PINCH,PINCH_HIGH,POINTER,COOL,
                                                           THE_FINGER,TURN_RIGHT,TURN_LEFT};
// initialize communication parameters:
int current_task = 0;
int current_preset_id = 0;
bool was_connected = false;

/*************************************************************************************/
/*                  BLE Class Definition and Set Callbacks:                          */
/*************************************************************************************/
// All BLE characteristic UUIDs are of the form:
// 0000XXXX-0000-1000-8000-00805f9b34fb

#define HAND_DIRECT_EXECUTE_SERVICE_UUID     "e0198000-7544-42c1-0000-b24344b6aa70"
#define EXECUTE_ON_WRITE_CHARACTERISTIC_UUID "e0198000-7544-42c1-0001-b24344b6aa70"

#define HAND_PRESET_SERVICE_UUID             "e0198001-7544-42c1-0000-b24344b6aa70"

#define HAND_TRIGGER_SERVICE_UUID            "e0198002-7544-42c1-0000-b24344b6aa70"
#define TRIGGER_ON_WRITE_CHARACTERISTIC_UUID "e0198002-7544-42c1-0001-b24344b6aa70"

#define HAND_CONFIG_SERVICE_UUID             "e0198003-7544-42c1-0000-b24344b6aa70"
#define CONFIG_LTV_UUID                      "e0198003-7544-42c1-1000-b24344b6aa70"
#define CONFIG_HTV_UUID                      "e0198003-7544-42c1-1001-b24344b6aa70"
#define CONFIG_TS_UUID                       "e0198003-7544-42c1-1002-b24344b6aa70"
#define CONFIG_WW_UUID                       "e0198003-7544-42c1-1003-b24344b6aa70"
#define CONFIG_LTS_UUID                      "e0198003-7544-42c1-1004-b24344b6aa70"
#define CONFIG_HTS_UUID                      "e0198003-7544-42c1-1005-b24344b6aa70"
#define CONFIG_TF0_UUID                      "e0198003-7544-42c1-1006-b24344b6aa70"
#define CONFIG_TF1_UUID                      "e0198003-7544-42c1-1007-b24344b6aa70"
#define CONFIG_TF2_UUID                      "e0198003-7544-42c1-1008-b24344b6aa70"
#define CONFIG_TF3_UUID                      "e0198003-7544-42c1-1009-b24344b6aa70"
#define CONFIG_TF4_UUID                      "e0198003-7544-42c1-100a-b24344b6aa70"
#define CONFIG_DV_UUID                       "e0198003-7544-42c1-1012-b24344b6aa70"
#define CONFIG_DEB_UUID                      "e0198003-7544-42c1-1013-b24344b6aa70"
#define CONFIG_RST_PRESETS_UUID              "e0198003-7544-42c1-0100-b24344b6aa70"
#define CONFIG_RST_CONFIG_UUID               "e0198003-7544-42c1-0101-b24344b6aa70"

bool _BLEClientConnected = false;

#define BatteryService BLEUUID((uint16_t)0x180F) 
BLECharacteristic BatteryLevelCharacteristic(BLEUUID((uint16_t)0x2A19), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor BatteryLevelDescriptor(BLEUUID((uint16_t)0x2901));

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      _BLEClientConnected = true;
      Serial.println("Connected");
    };

    void onDisconnect(BLEServer* pServer) {
      _BLEClientConnected = false;
      Serial.println("Disconnected");
    }
};

const char * presetCharacteristicUuid(int presetNumber)
{
  const char *uuids[12];
  uuids[0] = "e0198001-7544-42c1-1000-b24344b6aa70";
  uuids[1] = "e0198001-7544-42c1-1001-b24344b6aa70";
  uuids[2] = "e0198001-7544-42c1-1002-b24344b6aa70";
  uuids[3] = "e0198001-7544-42c1-1003-b24344b6aa70";
  uuids[4] = "e0198001-7544-42c1-1004-b24344b6aa70";
  uuids[5] = "e0198001-7544-42c1-1005-b24344b6aa70";
  uuids[6] = "e0198001-7544-42c1-1006-b24344b6aa70";
  uuids[7] = "e0198001-7544-42c1-1007-b24344b6aa70";
  uuids[8] = "e0198001-7544-42c1-1008-b24344b6aa70";
  uuids[9] = "e0198001-7544-42c1-1009-b24344b6aa70";
  uuids[10] = "e0198001-7544-42c1-100a-b24344b6aa70";
  uuids[11] = "e0198001-7544-42c1-100b-b24344b6aa70";
  return uuids[presetNumber];
}

const char * configCharacteristicUuid(int idx)
{
  const char *uuids[20];

  uuids[0] = CONFIG_LTV_UUID;   // Low Torque Value
  uuids[1] = CONFIG_HTV_UUID;   // High Torque Value
  uuids[2] = CONFIG_TS_UUID;    // Torque Measure Start
  uuids[3] = CONFIG_WW_UUID;    // Window Width Filter
  uuids[4] = CONFIG_LTS_UUID;   //  Low Torque Slope Value
  uuids[5] = CONFIG_HTS_UUID;   // High Torque Slope Value
  uuids[6] = CONFIG_TF0_UUID;   // Threshold Factor Motor0 (Wrist)
  uuids[7] = CONFIG_TF1_UUID;   // Threshold Factor Motor1
  uuids[8] = CONFIG_TF2_UUID;   // Threshold Factor Motor2
  uuids[9] = CONFIG_TF3_UUID;   // Threshold Factor Motor3
  uuids[10] = CONFIG_TF4_UUID;  // Threshold Factor Motor4
  // config [11-17] not defined by protocol but we'll
  // fill the array consequtively though; this might seem
  // unintuitive but is needed and makes no difference
  uuids[11] = CONFIG_DV_UUID;   //  Threshold Value Unit
  uuids[12] = CONFIG_DEB_UUID;  // Debugg mode (1/0)

  uuids[13] = CONFIG_RST_PRESETS_UUID; // reset preset to default
  uuids[14] = CONFIG_RST_CONFIG_UUID;  // reset config to default
  return uuids[idx];
}


class DirectExecuteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) { //execute one movement from the 'live control' or from the 'movement editing'
      unsigned char* dataPtr;
      dataPtr = pCharacteristic->getData();
      short len = dataPtr[0];
      if (!(len<MIN_PRESET_BYTES || len>MAX_PRESET_BYTES)){   // valid length:
        for (int i = 0; i < len; i++) *(all_presets[PRESETS_NUM]+i)= *(dataPtr + i);
        current_task = 1;  //do action according to preset data
        current_preset_id = PRESETS_NUM;  // a temporary present, nothing is saved upon the EEPROM
        if (is_debug) { //debugging
          Serial.printf("DirectExecuteCallbacks : current_task=%i, current_preset_id=%i\n",current_task,current_preset_id);
          //for (int i = 0; i < 5; i++) Serial.println(*(all_presets[PRESETS_NUM]+i));
        }
      }
      else Serial.println("DirectExecuteCallbacks : invalid  command (too short or too long)");
    };
};

class PresetCallbacks : public BLECharacteristicCallbacks {
    int presetId = 0;

    void onWrite(BLECharacteristic *pCharacteristic) { //save present on the controllerer 
      current_preset_id = presetId;
      unsigned char* dataPtr;
      dataPtr = pCharacteristic->getData();
      short len = dataPtr[0];
      if (!(len<MIN_PRESET_BYTES || len>MAX_PRESET_BYTES)){   // valid length:
        for (int i = 0; i < len; i++) *(all_presets[current_preset_id]+i)= *(dataPtr + i);
        current_task = 2;  //save new preset according to preset data
        if (is_debug) { //debugging
          Serial.printf("PresetCallbacks-onWrite : current_task=%i, current_preset_id=%i\n",current_task,current_preset_id);
          //for (int i = 0; i < 5; i++) Serial.println(*(all_presets[current_preset_id]+i));
        }
      }
      else Serial.println("DirectExecuteCallbacks : invalid  command (too short or too long)");
    };

    void onRead(BLECharacteristic *pCharacteristic) { //read present from the controllerer 
      current_preset_id = presetId;
      short len = *(all_presets[current_preset_id]+0);
      pCharacteristic->setValue(all_presets[current_preset_id], len); 
      if (is_debug) { //debugging
        Serial.printf("PresetCallbacks-onRead : current_task=%i, current_preset_id=%i\n",current_task,current_preset_id);
        //for (int i = 0; i < 5; i++) Serial.println(*(all_presets[current_preset_id]+i));
      }
    }

  public:
    PresetCallbacks(int id) {
      presetId = id;
    }
};

class TriggerCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) { //execute one of the presents
      unsigned char* dataPtr;
      dataPtr = pCharacteristic->getData();
      current_preset_id = dataPtr[0];
      if (current_preset_id<=PRESETS_NUM ){
        current_task = 1; //do action according to preset id
        if (is_debug) { //debugging
          Serial.printf("TriggerCallbacks : current_task=%i, current_preset_id=%i\n",current_task,current_preset_id);
          //for (int i = 0; i < 5; i++) Serial.println(*(all_presets[current_preset_id]+i));
        }
      }
      else Serial.println("TriggerCallbacks : invalid  preset id (too large)");
    };
};

class ConfigCallbacks : public BLECharacteristicCallbacks {
    int idx = 0;
    //unsigned char value = 0;

    void onWrite(BLECharacteristic *pCharacteristic) {
      config_id = idx;
      unsigned char* dataPtr;
      dataPtr = pCharacteristic->getData();
      if (config_id>=CONFIG_BYTES) {
        Serial.println();
        Serial.printf("--- Config number  %i has no space on the EEPROM memory ---\n", config_id);
        config_id = 0;
      }
      else {
        *(all_config + config_id) = *(dataPtr);
        if (is_debug) { //debugging
          Serial.println();
          Serial.printf("--- Config %i written. Set to: %i ---\n", config_id, all_config[config_id]);
        }
        current_task = 3; //save new config according to config id
      }
      
    };

    void onRead(BLECharacteristic *pCharacteristic) {
      config_id = idx;
      unsigned char config_val[1];
      //currently there are only 20 availble adresses on the EEPROM:
      if (config_id>=CONFIG_BYTES){
        config_val[0] = 0;
        config_id = 0;
      }
      else config_val[0] = *(all_config + config_id); 
      pCharacteristic->setValue(config_val, 1);
      if (is_debug) { //debugging
        Serial.printf("ConfigCallbacks-onRead : config_id=%i, config_value=%i\n",config_id,*config_val);
      }
    }

  public:
    ConfigCallbacks(int id) {
      idx = id;
    }
};

void InitBLE() {
  BLEDevice::init("Haifa3D");
  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the Battery Service
  BLEService *pBattery = pServer->createService(BatteryService);

  pBattery->addCharacteristic(&BatteryLevelCharacteristic);
  BatteryLevelDescriptor.setValue("Percentage 0 - 100");
  BatteryLevelCharacteristic.addDescriptor(&BatteryLevelDescriptor);
  BatteryLevelCharacteristic.addDescriptor(new BLE2902());

  BLEService *pDirectExecService = pServer->createService(HAND_DIRECT_EXECUTE_SERVICE_UUID);
  BLECharacteristic *pExecOnWriteCharacteristic = pDirectExecService->createCharacteristic(
                                         EXECUTE_ON_WRITE_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  pExecOnWriteCharacteristic->setCallbacks(new DirectExecuteCallbacks());

  BLEService *pTriggerService = pServer->createService(HAND_TRIGGER_SERVICE_UUID);
  BLECharacteristic *pTriggerOnWriteCharacteristic = pTriggerService->createCharacteristic(
                                         TRIGGER_ON_WRITE_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  pTriggerOnWriteCharacteristic->setCallbacks(new TriggerCallbacks());

  // the 32 is important because otherwise we dont have enough handles and just the first 6 characteristics will be visible
  BLEService *pPresetService = pServer->createService(BLEUUID(HAND_PRESET_SERVICE_UUID), 32);
  for (int i = 0; i < PRESETS_NUM ; i++) {
    BLECharacteristic *pPresetCharacteristic = pPresetService->createCharacteristic(
                                         presetCharacteristicUuid(i),
                                         BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
                                       );
    pPresetCharacteristic->setCallbacks(new PresetCallbacks(i));
    Serial.printf("Added Preset Characteristic %i\n", i);
  }

  // the 32 is important because otherwise we dont have enough handles and just the first 6 characteristics will be visible
  BLEService *pConfigService = pServer->createService(BLEUUID(HAND_CONFIG_SERVICE_UUID), 96);
  for (int i = 0; i < (CONFIG_NUM + CONFIG_TRIG_NUM); i++) {
    BLECharacteristic *pConfigCharacteristic = pConfigService->createCharacteristic(
                                         configCharacteristicUuid(i),
                                         BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
                                       );
    pConfigCharacteristic->setCallbacks(new ConfigCallbacks(i));
  }

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BatteryService);
  pAdvertising->addServiceUUID(HAND_DIRECT_EXECUTE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  pBattery->start();
  pDirectExecService->start();
  pTriggerService->start();
  pPresetService->start();
  pConfigService->start();
  // Start advertising
  pAdvertising->start();
}

/******************************************************************************/
/*                          Functions:                                        */
/******************************************************************************/
/******************** load and init parameters ***************************/
void load_update_parameters(bool is_config, bool is_preset){
  // load/update configuration:
  if (is_config){
    for (int c=0; c<CONFIG_NUM; c++) all_config[c] =  EEPROM.read(c); //reading the config bytes
      //update configuration:
      dv_config = all_config[11];
      thr_value_L = all_config[0] * dv_config;
      thr_value_H = all_config[1] * dv_config;
      t_start = all_config[2];
      win_width = all_config[3];
      thr_slope_L = all_config[4];
      thr_slope_H = all_config[5];
      for (int k=0; k<MOTORS_NUM; k++) thr_factor[k] = all_config[6+k];
      is_debug = all_config[12];
      if (is_debug) { //debugging
        Serial.printf("dv_config: %i, thr_value_L: %i, thr_value_H: %i, t_start: %i, win_width: %i, thr_slope_L: %i, thr_slope_H: %i",
        dv_config,thr_value_L,thr_value_H,t_start,win_width,thr_slope_L,thr_slope_H);
        for (int k=0; k<MOTORS_NUM; k++)  Serial.printf(", thr_factor%i: %i",k,all_config[6+k]);
        Serial.println();
      }
  }
  // load/updates presets:
  if(is_preset){
    for (int p=0; p<PRESETS_NUM; p++){
      short len = EEPROM.read(CONFIG_BYTES + (p*MAX_PRESET_BYTES)); //read the length of the preset
      if (is_debug) Serial.printf(" preset %i: from address: %i, length %i, ",p,(int)CONFIG_BYTES + (p*MAX_PRESET_BYTES),(int) len);
      if (len > MAX_PRESET_BYTES || len < MIN_PRESET_BYTES){//garbage values
        len = MIN_PRESET_BYTES;
      }
      *(all_presets[p]+0) = len;
      if (is_debug) Serial.printf("fixed length %i\n",p,(int) len);
      // update presets:
      for (int i=1; i<len; i++){
        *(all_presets[p]+i) =  EEPROM.read(CONFIG_BYTES + (p*(MAX_PRESET_BYTES)) + i ); //reading the preset bytes w\o the length
        if (is_debug) Serial.printf("read preset %i: from address: %i, byte %i\n",p,(int)CONFIG_BYTES + (p*(MAX_PRESET_BYTES)) + i,(int) *(all_presets[p]+i));
      }
    }
  }
}

/******************** save parameters to EEPROM ***************************/
void save_preset(int p_ind){
  short len = *(all_presets[p_ind]+0);
  for (int i=0; i<len; i++){
    EEPROM.write(CONFIG_BYTES + (p_ind*(MAX_PRESET_BYTES)) + i,  *(all_presets[p_ind]+i));//writing the preset bytes w the length
    if (is_debug) Serial.printf("EEPROM Write - adderess: %i, value: %i\n",CONFIG_BYTES + (p_ind*(MAX_PRESET_BYTES)) + i, (int) *(all_presets[p_ind]+i));
  }
  EEPROM.commit();
}

void save_config(int c_ind){
  if (c_ind==3 && *(all_config + c_ind)>MAX_WINDOW ){ //window-width config
    Serial.printf("window width exceeded the allowed Width [%i]\n", MAX_WINDOW);
    *(all_config + c_ind)=MAX_WINDOW;
  }
  EEPROM.write(c_ind,  *(all_config + c_ind));
  EEPROM.commit();
  if (is_debug) Serial.printf("EEPROM Write - adderess: %i, value: %i\n",c_ind, (int)*(all_config + c_ind));
  //update configuration:
  load_update_parameters(true, false);

}

/******************** reset parameters  ***************************/
void reset_to_default(bool is_config, bool is_preset){
  // reset configs:
  if (is_config){
    for (int c=0; c<CONFIG_NUM; c++) EEPROM.write(c,  *(all_config_default + c));
    EEPROM.commit();
    
  }
  // reset presets:
  if (is_preset){
    for (int p=0; p<PRESETS_NUM; p++) {
      short len = *(all_presets[p]+0);
      for (int i=0; i<len; i++) EEPROM.write(CONFIG_BYTES + (p*(MAX_PRESET_BYTES)) + i,  *(all_presets_default[p]+i));//writing the preset bytes w the length
    }
    EEPROM.commit();
    
  }
  // load update variables:
  load_update_parameters(is_config, is_preset);
  
}

/******************** interpeting moving according to our pre-defined protocol ***************************/
void interp_movement(unsigned char* msg, int* pthreshold, int* pslope_threshold, int* pt_stop,
                     int pindx_active[MOTORS_NUM], int* pnum_of_active, bool pmotor_dir[MOTORS_NUM]){
      std::bitset<8> bs;
      // thrshold:
      bs = std::bitset<8>(msg[0]);
      *pthreshold = bs.test(7 - 0) ? thr_value_H : thr_value_L;
      *pslope_threshold = bs.test(7 - 0) ? thr_slope_H : thr_slope_L;
      // max end time:
      *pt_stop  = msg[1] * TIME_UNITS;
      // active motors:
      bs = std::bitset<8>(msg[2]);
      int num=0;
      for (int i=0; i<MOTORS_NUM; i++){
        if (bs.test(7 - i)){
          pindx_active[num]=i;
          num++;
        }
      }
      *pnum_of_active = num;
      // motors direction:
      bs = std::bitset<8>(msg[3]);
      for (int i=0; i<MOTORS_NUM; i++) pmotor_dir[i]= bs.test(7 - i);
}

/******************** activate motors ***************************/
int activate_motors(int inHIGH, int inLOW, int Isense, int motor){
  digitalWrite(inHIGH, HIGH);
  digitalWrite(inLOW, LOW);
  int new_current = analogRead(Isense); // value 0 - 4095 = 1v
  //to stabilize the signal:
  delay(1);
  new_current = (new_current+analogRead(Isense))/2;
  
  
  return new_current;
}

/******************** applying moving average - Filtering ***************************/
int moving_average(int new_current, int* psum_smooth, int all_motor_samples[MAX_WINDOW], int* pind_smooth){
  int sample_out; 
  (*psum_smooth) -= all_motor_samples[*pind_smooth];
  (*psum_smooth) += new_current;
  all_motor_samples[*pind_smooth] = new_current;
  sample_out = *psum_smooth / win_width;
  (*pind_smooth)++;
  if (*pind_smooth>=win_width) *pind_smooth=0;

  return sample_out;
}

/******************** calculating the time of increasing current (slope time) ***************************/
int slope_calculation(int motor_current, int* plast_motor_current, int* pslope_start_time){
  int dt_slope = 0;
  if (motor_current > *plast_motor_current && motor_current > 20) dt_slope = millis() - *pslope_start_time; //current/resistmace is increasing
  else *pslope_start_time = millis(); //idea: else if (millis() - *pslope_start_time>DT_MIN_SLOPE) *pslope_start_time = millis(); //initializing starting time only if enpugh time has elapsed
  *plast_motor_current = motor_current; //update the previous motor current
  
  return dt_slope;
}

/******************** finish movement and stop all motors ***************************/
void reset_action(int sum_smooth[MOTORS_NUM],int ind_smooth[MOTORS_NUM],int all_samples[MOTORS_NUM][MAX_WINDOW],int last_process_samples[MOTORS_NUM] ){
  for (int m = 0 ; m < MOTORS_NUM * 2 ; m++) digitalWrite(all_motors[m], LOW);
  for (int m = 0 ; m < MOTORS_NUM ; m++) sum_smooth[m] = 0;
  for (int m = 0 ; m < MOTORS_NUM ; m++) ind_smooth[m] = 0;
  for (int m = 0 ; m < MOTORS_NUM ; m++) for (int i = 0 ; i < MAX_WINDOW ; i++) all_samples[m][i] = 0;
  for (int m = 0 ; m < MOTORS_NUM ; m++) last_process_samples[m] = 0;

}

/******************** check if event occured during movement ***************************/
bool check_new_event(int p_id, int dt, int* pt_stop){

  bool is_stop = false;

  //buttons second click:
  // #TODO

  //BLE additional events:
  if (current_task==0) is_stop = false; //do nothing
  else if (current_task==1 && current_preset_id == p_id){ //still sending execute messages
    *pt_stop = TIME_UNITS + dt;
    current_task = 0;
    //Serial.println("t_stop was updated"); //debugging
  }
  else is_stop = true; //different tasks

  return is_stop; 
}

/******************** the control function - current control method for adaptive grip ***************************/
bool action_control(bool user_stop, int dt, int t_stop, int motor_current, int motor_slope_current, int threshold, int slope_threshold, int motor_ind){
  
  bool end_motor_action = false;
  
  // User Stop Conditions: 
  if (user_stop){//exit the while loop at ActionRun function and stops all motors
    if (is_debug) Serial.println("User stops all motors"); //debugging
    end_motor_action = true;    
  }
  // Time conditions:
  else if ( dt < t_start) end_motor_action = false;   //allow motor activation during "t_start" due to high starting current
  else if (dt >= t_stop){//exit the while loop at ActionRun function and stops all motors
    if (is_debug) Serial.printf("motor:%i, end-time: %i [ms]\n", motor_ind,dt); //debugging
    end_motor_action = true;    
  }
  // Current Control (applying current control only during t_start < dt < t_stop)
  else if (motor_current > int((thr_factor[motor_ind]*threshold)/100) && motor_slope_current > int((thr_factor[motor_ind]*slope_threshold)/100)) { //The threshold was achieved (maybe && instead of ||) 
    if (is_debug) Serial.printf("motor:%i, dt: %i, reached Threshold: %i[value], %i[slope]\n",motor_ind,dt,motor_current,motor_slope_current);//debugging
    end_motor_action = true;   
  }
  
  return end_motor_action;
}

/******************** execute action ***************************/
void exe_action(int p_id){
  short len = *(all_presets[p_id]+0);
  short movements = (len - 1) / ACTION_BYTES;
  
  for (int i = 0; i < movements; i++) {
     Serial.printf("--== Movement %i ==--\n", i);
     int threshold; 
     int slope_threshold;
     int t_stop;
     int indx_active[MOTORS_NUM];
     int num_of_active; 
     bool motor_dir[MOTORS_NUM];
     //smoothing parameters:
     int sum_smooth[MOTORS_NUM];
     int ind_smooth[MOTORS_NUM];
     int all_samples[MOTORS_NUM][MAX_WINDOW];
     // slope calculation parameters:
     int last_process_samples[MOTORS_NUM];
     int t_slope_i[MOTORS_NUM];
     // debugging variable:
     int all_measurements[MOTORS_NUM];

     //initialization:
     reset_action(sum_smooth, ind_smooth, all_samples,last_process_samples);

     interp_movement((all_presets[p_id]+1+i*ACTION_BYTES),&threshold,&slope_threshold,&t_stop,indx_active,&num_of_active,motor_dir);
     //debugging:
     if (is_debug){ //debugging:
       Serial.printf("threshold: %i, t_stop: %i , num_of_active: %i, indx_active: ",threshold,t_stop,num_of_active);
       for(int i=0; i<num_of_active; i++) Serial.printf("%i ",indx_active[i]);
       Serial.print("motor_dir: ");
       for(int i=0; i<num_of_active; i++) Serial.printf("%i ", (int)motor_dir[indx_active[i]]);
       Serial.println();
     }
     //action run:
     unsigned long t_i = millis(); //the starting time
     while(num_of_active>0){
      if (is_debug) for (int k=0; k<2*MOTORS_NUM; k++) all_measurements[k]=-1;
      for (int i = 0 ; i < num_of_active ; i++){
        //Defining Parameters:
        int m = indx_active[i];
        int in1 = all_motors[2 * m];
        int in2 = all_motors[2 * m + 1];
        int Isense = all_controls[m];
        int motor_current;
        int motor_slope_current;
        int dt = (int) (millis() - t_i); //the motors running time

        //activate motors:
        if (motor_dir[m]) motor_current = activate_motors(in1, in2, Isense, m); //close the hand
        else motor_current = activate_motors(in2, in1, Isense, m); //open the hand

        //smoothing:
        motor_current = moving_average(motor_current, &sum_smooth[m], all_samples[m], &ind_smooth[m]);
        
        // calculating the current slope 
        motor_slope_current = slope_calculation(motor_current, &last_process_samples[m], &t_slope_i[m]);
        
        //check events:
        bool user_stops = check_new_event(p_id, dt, &t_stop); //check for user inputs during motion

        // print parameters:
        if (is_debug){
          all_measurements[m] = motor_current;
          all_measurements[m + MOTORS_NUM] = motor_slope_current;
        }
        //check control:
        bool end_action = action_control(user_stops, dt, t_stop, motor_current,motor_slope_current, threshold,slope_threshold, m);
        if (end_action) {
          num_of_active--;
          for (int k=i; k<num_of_active; k++) indx_active[k] = indx_active[k+1];
          i--;
        }
      }
      if (is_debug) {
        for (int k=0; k<2*MOTORS_NUM; k++) Serial.printf("%i, ",all_measurements[k]);
        Serial.println();
      }
     }
     //reset:
     reset_action(sum_smooth, ind_smooth, all_samples,last_process_samples);
  }
}

/******************** event holder - responsiple for assigning task per event ***************************/
void event_holder(int task_type, int task_id){

  //reset:
  if (current_task == task_type) current_task = 0;
  
  switch (task_type){
    case 0:{
      break;
    }
    case 1:{ //do action
      Serial.println("exe_action");
      exe_action(task_id);
      break;
    }
    case 2:{ //save preset
      Serial.println("save_preset");
      save_preset(task_id);
      break;
    }
    case 3:{ //save config
      if (config_id<CONFIG_NUM){
        Serial.println("save_config");
        save_config(config_id);
      }
      else {//trigger config
        switch(config_id){
          case 13:{// reset preset to default
            Serial.println("reset preset");
            reset_to_default(false, true);
            break;
          }
          case 14:{// reset config to default
            Serial.println("reset config");
            reset_to_default(true, false);
            break;
          }
          default:
          Serial.printf("Config number  %i is NOT defined on the controller\n", config_id);
          break;
        }
      } 
      break;
    }
  }
}

/******************** button functions ***************************/
bool is_button(int* new_button){
  bool is_high = false;
  bool button_stat = false;
  for (int i = 0; i < BUTTONS_NUM ; i++){
    button_stat = digitalRead(all_buttons[i]);
    if (button_stat && !is_high){
      is_high = true;
      *new_button = i;
    }
    else if (button_stat && is_high) {
      Serial.printf("at least two buttons are active %i,%i\n", new_button,i);
    }
  }
  return is_high;
}

int check_button_state(int is_high, int new_button){
  
  int active_button;
  bool was_high;
  if (button_state == 0 || button_state == 5 || button_state == 6 ) {// a click was finished or not started
    if (is_high) {//check again to reduce debouncing
      delay(50);
      is_high = is_button(&active_button); 
    }
  }
  else  {//during a click process
    if (is_high && new_button != button_id) {// still in a click but two different button
      Serial.println("Check again");
      is_high = is_button(&new_button);
      if (is_high && new_button != button_id) Serial.println("make no sense..."); // we still have a problem
    }
    active_button = button_id; //still the same button
  }
  /* check the button states according to the different states:
   * 0 = nothing, 1 = a click was started, 2 = still a short click, 3 = starting a long click, 4 = still a long click, 5 = end of a short click, 6 = end of a long click.
   * if IsPressed is true, button_state updates from # to #: 0->1, 1->2or3, 2->2or3, 3->4, 4->4, 5->1, 6->1 (the decision between 2or3 is defined according to the dt_press)
   * if IsPressed is false, button_state updates to: 0,5,6->0, 1,2->5 , 3,4->6
  */
  int states_LOW[7] = {0,5,5,6,6,0,0};
  int states_HIGH_long[7]= {1,3,3,4,4,1,1};
  int states_HIGH_short[7]= {1,2,2,4,4,1,1};
  int *states_HIGH;
  if (  (millis() - t_press) > DT_LONG_CLICK ) states_HIGH = states_HIGH_long;
  else states_HIGH = states_HIGH_short;
  
  //updating button state:
  if (is_high) button_state = states_HIGH[button_state];
  else button_state = states_LOW[button_state];
  
  //updating parameters:
  if (button_state == 1) t_press = millis(); //a click was stated -> initialized the pressing time

  return active_button;
}

/******************** led functions ***************************/
void set_led_color(int color_mode, bool led_state){
    
    int all_colors[5][3] = {{0,20,0},{0,0,80},{100,0,100},  {180,75,0},{255,0,0}   }; //green, blue, purple - for three modes, orange, red - for battery status.
    for (int k = 0 ; k < LEDS_NUM ; k++) {
      if (led_state) ledcWrite(k, all_colors[color_mode][k]); //(ledChannel, dutyCycle[0-2^resolution=8])// for arduino w/o pwm pins: digitalWrite(AllLEDs[k], all_colors[color_mode][k]);
      else ledcWrite(k, 0);
   }
}

/******************** awitching mode function ***************************/
void switching_mode(){
  hand_mode++;
  if (hand_mode >= (MODES_NUM-1) ) hand_mode=0;
  //mode indication:
  set_led_color(hand_mode, HIGH);
  Serial.printf("mode: %i\n",hand_mode);
  
}

/******************** assign event for each detected pressed button ***************************/
void interp_buttons(){

  if (button_state == 3 || button_state == 5){
    if (button_id == 0) switching_mode(); //mode button was pressed
    else if (button_state == 5){ //a click was finished and it was short
      current_preset_id = (button_id-1) + MOV_PER_MODE * hand_mode;
      current_task = 1;
    }
    else{//a long click was started
      current_preset_id = (button_id-1) + MOV_PER_MODE *(MODES_NUM-1); //last hand_mode is for the long clicks
      current_task = 1;
    }
  }
   
}

void buttons_events(){

  int new_button;
  bool is_high = is_button(&new_button);
  
  button_id = check_button_state(is_high,new_button); //updates button_id, button_state

  interp_buttons();
  
}

/******************** check battery status ***************************/
void check_battery_status(){
  
  if ( millis() - t_check_battery > DT_BATTERY_STATUS || (_BLEClientConnected && !was_connected)){
    int battery_raw = analogRead(battery_pin);
    delay(20);
    battery_raw = (battery_raw + analogRead(battery_pin))/2;
    battery_level = ((battery_raw-BATTERY_MIN)*100) / (BATTERY_MAX-BATTERY_MIN);
    if (is_debug) Serial.printf("value %i, calculated value %i\n",battery_raw,int(battery_level)); //debugging
    if (battery_level > 100) battery_level = 100;
    else if (battery_level < 0) battery_level = 0;
    BatteryLevelCharacteristic.setValue(&battery_level, 1);
    BatteryLevelCharacteristic.notify();
    //controller indications:
    if ( battery_level > 50 ) ; //Serial.println("battery is full");
    else if ( battery_level > 35 ) set_led_color(3, HIGH);//Serial.println("battery is 50% discharge");
    else if ( battery_level > 16 ) set_led_color(4, HIGH); //Serial.println("battery is 75% discharge");
    else {
      Serial.println("go to sleep mode");
      for (int k = 0 ; k < MOTORS_NUM * 2 ; k++) digitalWrite(all_motors[k], LOW); // turn off all motors
      for (int k = 0 ; k < LEDS_NUM ; k++) ledcWrite(k, 0); //trun off all leds
      //esp_deep_sleep_start(); //enter sleep mode or other mode // #TODO
      //while(1); // do nothing
    }
    t_check_battery = millis();
    was_connected = true;
  }
  else if (!_BLEClientConnected) was_connected=false;
}

/******************************************************************************/
/*                          Setup Loop:                                       */
/******************************************************************************/
void setup() {
  Serial.begin(115200);//debugging
  EEPROM.begin(EEPROM_SIZE); // relevant only for ESP32
  InitBLE();
  load_update_parameters(true, true);
  // Motors Init:
  for (int k = 0 ; k < MOTORS_NUM * 2 ; k++) pinMode(all_motors[k], OUTPUT); //initialize the motors
  for (int k = 0 ; k < MOTORS_NUM ; k++) {
    pinMode(all_controls[k], INPUT); //initialize the Isense
    analogSetPinAttenuation(all_controls[k], ADC_0db); //sets the Isense attunation to 0, meaning the voltage range is 0-1v
  }
  for (int k = 0 ; k < MOTORS_NUM * 2 ; k++) digitalWrite(all_motors[k], LOW); // initialize LOW to all motors

  // LEDS Init:
  for (int k = 0 ; k < LEDS_NUM ; k++) { //initialize the LEDs
    ledcSetup(k, freq, resolution); // configure LED PWM functionalitites: (ledChannel, freq, resolution)
    ledcAttachPin(all_leds[k], k); // attach the channel to the GPIO to be controlled: (ledPin, ledChannel)
    set_led_color(0, HIGH); //turn the first mode LED on! (hand_mode = 0)
  }

  // Battery Init
  pinMode(battery_pin, INPUT); // init the battery status pin
  analogSetPinAttenuation(battery_pin, ADC_11db);
  t_check_battery = millis(); // the battery status is checked every hour
  
  // Buttons Init:
  for (int k = 0 ; k < BUTTONS_NUM ; k++) pinMode(all_buttons[k], INPUT); //initialize all buttons
    
}

/******************************************************************************/
/*                          Main Loop:                                        */
/******************************************************************************/
void loop() {
  
  event_holder(current_task, current_preset_id);

  buttons_events();

  check_battery_status();

}
