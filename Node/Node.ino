#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

#define CHANNEL     1
#define GATEWAY_ID  255  //ID của Gateway
#define NODE_ID     1    //ID của Node

//DEFINE GPIO PIN
#define CONFIG 0
#define LED_STATUS 2
#define COUNT_TRIGGER 12 //Chân đọc cảm biến

//DEFINE TYPE DATA
#define DELETE    0
#define READ_EEP  1

uint8_t GatewayAddress[] = {0xD8, 0xBF, 0xC0, 0xFD, 0x29, 0x9B}; //Địa chỉ MAC của Gateway
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool Save_Data = false;
uint8_t nodeRepeater[20];

uint16_t Counter = 0;
uint8_t NodeSendData;
uint8_t repeaterNode;
uint8_t requestByNode;
uint32_t last_orderNum = 0;
uint32_t led_time = millis();
uint32_t currtime = millis();

struct requestData
{
  uint8_t type = '#'; //request
  uint8_t fromNode;
  uint8_t byNode;
  uint32_t orderNum;
  uint8_t requestDataType;
};

struct responseData
{
  uint8_t type = '%'; //reponse
  uint8_t fromNode = NODE_ID;
  uint8_t byNode = NODE_ID;
  uint16_t productQuantity;
};

responseData responseGateway;
responseData repeaterData;

//User define function
void ESPNOW_SentCallback(uint8_t *mac_addr, uint8_t sendStatus);
void ESPNOW_ReceivedCallback(uint8_t * mac, uint8_t *incomingData, uint8_t len);
bool long_press(uint16_t timepress);
IRAM_ATTR void ISR_Counter();
void EEPROM_writeInt(uint16_t address, uint16_t number);
uint16_t EEPROM_readInt(uint16_t address);

void setup() {
  Serial.begin(9600);
  EEPROM.begin(512);
  Counter = EEPROM_readInt(0);
  
  pinMode(CONFIG, INPUT_PULLUP);
  pinMode(COUNT_TRIGGER, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT); digitalWrite(LED_STATUS, HIGH);
  attachInterrupt(COUNT_TRIGGER, ISR_Counter, FALLING);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Init ESP-NOW
  if(esp_now_init() != 0){
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(ESPNOW_SentCallback);
  esp_now_register_recv_cb(ESPNOW_ReceivedCallback);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  esp_now_add_peer(GatewayAddress, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
}

void loop() {
  
  if (long_press(3000)) {
    Counter = 0;
    EEPROM_writeInt(0, 0);
  }

  if (Save_Data == true) {
    Save_Data = false;
    EEPROM_writeInt(0, Counter);
  }
  
  if (millis() - led_time >= 50) {
    digitalWrite(LED_STATUS, HIGH);
  }
}

//----------------------------------------------ESP-NOW-----------------------------------------------------------//
// Callback when data is sent
void ESPNOW_SentCallback(uint8_t *mac_addr, uint8_t sendStatus) {
  //Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0) {    
    //Serial.println("Delivery success");
    //Serial.println("----------------------------------------------------");
  }
  else {
    if (NodeSendData == NODE_ID)
    {
      Serial.println("broadcastAddress");
      NodeSendData = NODE_ID; 
      esp_now_send(broadcastAddress, (uint8_t*) &responseGateway, sizeof(responseGateway));
    } else {
      Serial.println("repeaterData");
      NodeSendData = repeaterData.fromNode; 
      esp_now_send(broadcastAddress, (uint8_t*) &repeaterData, sizeof(repeaterData));
    }
    //Serial.println("Delivery fail");
    //Serial.println("----------------------------------------------------");
  }
}

void ESPNOW_ReceivedCallback(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  digitalWrite(LED_STATUS, LOW); led_time = millis();
  if ((char)incomingData[0] == '#')
  {
    requestData newRequest;
    memcpy(&newRequest, incomingData, sizeof(newRequest));
    if (newRequest.fromNode == GATEWAY_ID && last_orderNum != newRequest.orderNum) {
      last_orderNum = newRequest.orderNum;
      requestByNode = newRequest.byNode;
      for (uint8_t i = 0; i < 20; i++) {
        nodeRepeater[i] = 0;
      }
      newRequest.byNode = NODE_ID;
      NodeSendData = NODE_ID; esp_now_send(broadcastAddress, (uint8_t*) &newRequest, sizeof(newRequest));
      Serial.println("Request from Gateway by:" + String(requestByNode));
      if (newRequest.requestDataType == READ_EEP)
      {
        //Serial.println("Type: READ");
        responseGateway.productQuantity = Counter;
        responseGateway.byNode = NODE_ID;
        if (requestByNode == 255) {
          Serial.println("Response to GatewayAddress");
          NodeSendData = NODE_ID; esp_now_send(GatewayAddress, (uint8_t*) &responseGateway, sizeof(responseGateway));
        } else {
          Serial.println("Response to broadcastAddress");
          NodeSendData = NODE_ID; esp_now_send(broadcastAddress, (uint8_t*) &responseGateway, sizeof(responseGateway));
        }
      } else if (newRequest.requestDataType == DELETE) {
        //Serial.println("Type: DELETE");
        Counter = 0;
        EEPROM_writeInt(0, 0);
      }   
    }
  }
  else if ((char)incomingData[0] == '%')
  {
    //Serial.println("DATA FROM OTHER NODE");
    responseData newResponse;
    memcpy(&newResponse, incomingData, sizeof(newResponse));
    if (nodeRepeater[newResponse.fromNode - 1] == 0) {
      nodeRepeater[newResponse.fromNode - 1] = 1;
      memcpy(&repeaterData, incomingData, sizeof(repeaterData));
      repeaterData.byNode = NODE_ID;
      if (requestByNode == 255) {
        Serial.println("Repeat to GatewayAddress");
        NodeSendData = repeaterData.fromNode; esp_now_send(GatewayAddress, (uint8_t*) &repeaterData, sizeof(repeaterData));
      } else {
        Serial.println("Repeat to broadcastAddress");
        NodeSendData = repeaterData.fromNode; esp_now_send(broadcastAddress, (uint8_t*) &repeaterData, sizeof(repeaterData));
      }
    }
  }
}

void IRAM_ATTR ISR_Counter() {
  Counter++;
  Save_Data = true;
}

bool long_press(uint16_t timepress)
{
  static uint32_t lastpress = 0;
  if(millis() - lastpress >= timepress && digitalRead(CONFIG) == 0)
  {
    lastpress = 0;
    return true;
  }
  else if(digitalRead(CONFIG) == 1) lastpress = millis();
  return false;
}

void EEPROM_writeInt(uint16_t address, uint16_t number) {
  EEPROM.write(address, number >> 8);
  EEPROM.write(address + 1, number & 0xFF);
  EEPROM.commit();
}

uint16_t EEPROM_readInt(uint16_t address) {
  return (EEPROM.read(address) << 8) + EEPROM.read(address + 1);
}
