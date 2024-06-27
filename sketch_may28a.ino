#include <NimBLEDevice.h>
#include <ESP32Servo.h>

void scanEndedCB(NimBLEScanResults results);

static NimBLEAdvertisedDevice* advDevice;

static bool doConnect = false;
static uint32_t scanTime = 0; // 0 = scan forever

// UUID HID
static NimBLEUUID HIDserviceUUID("1812");
// UUID Report Characteristic
static NimBLEUUID HIDcharUUID("2A4D");

static int servo1Pin = 13;

// for EMAX Servo
static int minUs = 544;
static int maxUs = 2450;
static Servo myServo;
static bool isServing = false;

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("Connected");
        pClient->setConnectionParams( 16,32,2,300 );
    };

    void onDisconnect(NimBLEClient* pClient) {
        Serial.print(pClient->getPeerAddress().toString().c_str());
        Serial.println(" Disconnected - Starting scan");
        NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
    };

    bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) {
        if(params->itvl_min < 24 || params->itvl_max > 40 || params->latency > 2 || params->supervision_timeout > 100) {
            return false;
        }
        return true;
    };

    uint32_t onPassKeyRequest() {
        Serial.println("Client Passkey Request");
        return 123456;
    };

    bool onConfirmPIN(uint32_t pass_key) {
        Serial.print("The passkey YES/NO number: ");
        Serial.println(pass_key);
        return true;
    };

    void onAuthenticationComplete(ble_gap_conn_desc* desc) {
        if(!desc->sec_state.encrypted) {
            Serial.println("Encrypt connection failed - disconnecting");
            NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
            return;
        }
    };
};

class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        Serial.print("Advertised Device found: ");
        Serial.println(advertisedDevice->toString().c_str());
        if(advertisedDevice->isAdvertisingService(HIDserviceUUID)) {
            Serial.println("Found Our Service");
            NimBLEDevice::getScan()->stop();
            advDevice = advertisedDevice;
            doConnect = true;
        }
    };
};

void openKey(){
  if(!isServing){
  isServing = true;
  myServo.write(0);
  delay(1000);
  myServo.write(90);
  isServing = false;
  }
}

void closeKey(){
  if(!isServing){
     isServing = true;
  myServo.write(100);
  delay(1000);
  // myServo.write(90);
   isServing = false;
  }
}

String getKeyString(uint8_t keyCode) {
  Serial.println("keycode="+String(keyCode));
  switch(keyCode) {
    case 0x02:
      openKey();
      return "close the door.";
    // case 0x00:
    //  openKey();
    //   return "open the door.";
    default:
      return "UNKNOWN";
  }
}


void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  Serial.print("Value = ");
    for (size_t i = 0; i < length; i++) {
        Serial.print(pData[i]); // データを16進数として表示

        // 1番目のバイトがキーコードであると仮定
        if (i == 0) {
            Serial.print(" (Key: ");
            Serial.print(getKeyString(pData[i]));
            Serial.print(")");
        }
        Serial.print(" ");
    }
    Serial.println();
}

void scanEndedCB(NimBLEScanResults results) {
    Serial.println("Scan Ended");
}

static ClientCallbacks clientCB;

bool connectToServer() {
    NimBLEClient* pClient = nullptr;

    if(NimBLEDevice::getClientListSize()) {
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
        if(pClient) {
            if(!pClient->connect(advDevice, false)) {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
        } else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    if(!pClient) {
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&clientCB, false);
        pClient->setConnectionParams(80, 80, 0, 100);
        pClient->setConnectTimeout(5);

        if (!pClient->connect(advDevice)) {
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if(!pClient->isConnected()) {
        if (!pClient->connect(advDevice)) {
            Serial.println("Failed to connect");
            return false;
        }
    }

    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    Serial.print("RSSI: ");
    Serial.println(pClient->getRssi());

    NimBLERemoteService* pSvc = pClient->getService(HIDserviceUUID);
    if(pSvc) {
      Serial.println("aaa");
        NimBLERemoteCharacteristic* pChr = pSvc->getCharacteristic(HIDcharUUID);

        
        if(pChr) {
          std::vector<NimBLERemoteCharacteristic*> *pCharacteristicMap;
        pCharacteristicMap = pSvc->getCharacteristics();
        for (auto itr = pCharacteristicMap->begin();
            itr != pCharacteristicMap->end(); ++itr) {
              Serial.println("bbb");
              if((*itr)->getUUID().equals(HIDcharUUID) && pChr->canNotify()) {
              Serial.println("Characteristic can notify");
              bool subscribe = (*itr)->subscribe(true, notifyCB);
              
                if(!subscribe) {
                  Serial.println("Not Found subscribe");
                    pClient->disconnect();
                    return false;
                }
              }
            }
        } else {
            Serial.println("Characteristic not found.");
        }
    } else {
        Serial.println("Service not found.");
    }

    return true;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting NimBLE Client");
    myServo.attach(servo1Pin,minUs,maxUs);

    NimBLEDevice::init("");

    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_SC);

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setInterval(45);
    pScan->setWindow(15);
    pScan->setActiveScan(true);
    pScan->start(scanTime, scanEndedCB);

    myServo.write(90);
}

void loop() {
    while(!doConnect) {
        delay(1);
    }
    doConnect = false;

    bool cTS = connectToServer();

    if(cTS) {
        Serial.println("Success! we should now be getting notifications, scanning for more!");
    } else {
        Serial.println("Failed to connect, starting scan");
        NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
    }

   
}
