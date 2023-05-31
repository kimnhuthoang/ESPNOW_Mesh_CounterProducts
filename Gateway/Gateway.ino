#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

#define CHANNEL   1
#define GATEWAY_ID 255  //ID của Gateway

#define DELETE    0
#define READ_EEP  1
#define requestTime 1000 //Thời gian request data từ các Node
#define timeOut 5 //Số lần request mà Node không reponse tối đa (kiểm tra trạng thái Node)

uint8_t broadcastAddress[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; //Địa chỉ MAC gửi đến tất cả các Node

uint16_t NodeData_Buff[20];   //Dữ liệu counter của các Node
uint8_t Node_Status[20];      //Trạng thái hoạt động của các Node
uint8_t Reponse_Status[20];   //Trạng thái phản hồi dữ liệu của các Node
uint32_t Node_TimeOut[20];    //Time Out của các Node từ lúc request

uint32_t waitRequest = millis();
uint32_t led_time = millis();
uint32_t waitCheckTO = millis();

bool Check_Reponse_Flag = false;

struct requestType
{
  uint8_t type = '#';         //request
  uint8_t fromNode = GATEWAY_ID;  //ID mặc định của Gateway là 255
  uint8_t byNode = GATEWAY_ID;
  uint32_t orderNum;
  uint8_t requestDataType;
};

struct responseData
{
  uint8_t type = '%'; //reponse
  uint8_t fromNode = GATEWAY_ID;
  uint8_t byNode;
  uint16_t productQuantity;
};

//User struct variable
requestType requestNode;

//User define function
void ESPNOW_SentCallback(uint8_t *mac_addr, uint8_t sendStatus); //Hàm callback kiểm tra gửi được hay chưa
void ESPNOW_ReceivedCallback(uint8_t * mac, uint8_t *incomingData, uint8_t len); //Hàm callback nhận dữ liệu gửi đến
void CheckNodeResponse_TO(uint16_t timeCheck); //Hàm kiểm tra trạng thái cái Node
bool long_press(uint16_t timepress); //Hàm nhấn nút theo thời gian
//---------------------------------------------------------------------------------//

void setup() {
  Serial.begin(9600);
  pinMode(0, INPUT_PULLUP);
  pinMode(2, OUTPUT); //Led builtin
  digitalWrite(2, HIGH);

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
}

void loop() {
  //Kiểm tra và nhận dữ liệu UART
  if (Serial.available() > 0)
  {
    char Data = Serial.read();
    if (Data == 'A') //Đọc data của các Node qua UART
    {
      for (uint8_t i = 0; i < 20; i++)
      {
        Serial.print("Node ["); Serial.print(i + 1); Serial.print("]: ");
        Serial.println(NodeData_Buff[i]);
      }
    } else if (Data == 'B') //Đọc trạng thái của các Node qua UART
    {
      for (uint8_t i = 0; i < 20; i++)
      {
        Serial.print("Node Status ["); Serial.print(i + 1); Serial.print("]: ");
        Serial.println(Node_Status[i]);
      }
    } else if (Data == 'C') //Yêu cầu Gateway gửi gói tin request các Node xóa counter
    {
      requestNode.orderNum ++;
      requestNode.requestDataType = DELETE;
      esp_now_send(broadcastAddress, (uint8_t*) &requestNode, sizeof(requestNode));
    }
       
  }
  //---------------------------------------------------------------------------------//

  //Gửi gói tin request đến các Node
  if (millis() - waitRequest >= requestTime)
  {
    requestNode.orderNum ++;
    requestNode.requestDataType = READ_EEP;
    for (uint8_t i = 0; i < 20; i++)
    {
      Reponse_Status[i] = 0;
    }
    esp_now_send(broadcastAddress, (uint8_t*) &requestNode, sizeof(requestNode));
    waitRequest = millis();
    //Serial.println("OK");
  }
  //---------------------------------------------------------------------------------//

  //Kiểm tra trạng thái reponse của các Node mỗi 1000ms
  CheckNodeResponse_TO(1000);
  //---------------------------------------------------------------------------------//

  //Kiểm tra nhấn nút 3s thì gửi gói tin request các Node xóa Counter về 0
  if (long_press(3000))
  {
    requestNode.orderNum ++;
    requestNode.requestDataType = DELETE;
    for (uint8_t i = 0; i < 20; i++)
    {
      Reponse_Status[i] = 0;
    }
    esp_now_send(broadcastAddress, (uint8_t*) &requestNode, sizeof(requestNode));
  }
  //---------------------------------------------------------------------------------//
  
  //Led nhận dữ liệu
  if (millis() - led_time >= 20)
  {
    digitalWrite(2, HIGH);
  }
  //---------------------------------------------------------------------------------//
  
}

//----------------------------------------------ESP-NOW-----------------------------------------------------//
// Callback when data is sent
void ESPNOW_SentCallback(uint8_t *mac_addr, uint8_t sendStatus) {
  //Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0) {
    Check_Reponse_Flag = true; waitCheckTO = millis();
    Serial.println("Delivery success");
    Serial.println("----------------------------------------------------");
  }
  else {
    Serial.println("Delivery fail");
    Serial.println("----------------------------------------------------");
  }
}

void ESPNOW_ReceivedCallback(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  digitalWrite(2, LOW); led_time = millis();
  responseData nodeData;
  memcpy(&nodeData, incomingData, sizeof(nodeData));
  if((char)incomingData[0] == '%') {
    if (Reponse_Status[nodeData.fromNode - 1] == 0) {
      Reponse_Status[nodeData.fromNode - 1] = 1;
      Node_Status[nodeData.fromNode - 1] = 1;
      Node_TimeOut[nodeData.fromNode - 1] = 0;
      NodeData_Buff[nodeData.fromNode - 1] = nodeData.productQuantity;
      Serial.print("Data from node "); Serial.print(nodeData.fromNode);
      Serial.print(": "); Serial.print(nodeData.productQuantity);
      Serial.print(" | By node: "); Serial.println(nodeData.byNode);
    }
  }
}

//---------------------------------------------------------------------------------//

void CheckNodeResponse_TO(uint16_t timeCheck) {
  if (Check_Reponse_Flag == true)
  {
    if (millis() - waitCheckTO >= timeCheck)
    {
      waitCheckTO = millis();
      for (uint8_t i = 0; i < 20; i++)
      {
        Node_TimeOut[i] += 1;
        if (Node_TimeOut[i] >= timeOut)
        {
          Node_Status[i] = 0;
        } 
      }
      Check_Reponse_Flag = false;
    }
  }
}

//---------------------------------------------------------------------------------//

bool long_press(uint16_t timepress)
{
  static uint32_t lastpress = 0;
  if(millis() - lastpress >= timepress && digitalRead(0) == 0)
  {
    lastpress = 0;
    return true;
  }
  else if(digitalRead(0) == 1) lastpress = millis();
  return false;
}
