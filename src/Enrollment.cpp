#include "Enrollment.h"

#include <NimBLEAdvertisedDevice.h>
#include <NimBLECharacteristic.h>
#include <NimBLEDescriptor.h>
#include <NimBLEDevice.h>
#include <NimBLEService.h>

#include "HttpWebServer.h"
#include "globals.h"
#include "mqtt.h"
#include "util.h"

namespace Enrollment {
static const char hex_digits[] = "0123456789abcdef";
static String newName, newId;
static unsigned long lastLoop = 0;
static int connectionToEnroll = -1;
static uint16_t major, minor;
static NimBLEServer *pServer;
static NimBLEAdvertisementData *oAdvertisementData;
static NimBLEService *heartRate;
static NimBLEService *deviceInfo;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer) {
        if (enrolling) {
            NimBLEDevice::startAdvertising();
        }
    };

    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) {
        Serial.print("Connected to: ");
        Serial.println(NimBLEAddress(desc->peer_ota_addr).toString().c_str());
        if (enrolling) {
            connectionToEnroll = desc->conn_handle;
        }
    };

    void onDisconnect(NimBLEServer *pServer) {
        if (enrolling) {
            Serial.println("Client disconnected");
            NimBLEDevice::startAdvertising();
        }
    };

    void onMTUChange(uint16_t MTU, ble_gap_conn_desc *desc) {
        Serial.printf("MTU updated: %u for connection ID: %u\r\n", MTU, desc->conn_handle);
    };

    void onAuthenticationComplete(ble_gap_conn_desc *desc) {
        Serial.printf("Encrypt connection %s conn: %d!\r\n", desc->sec_state.encrypted ? "success" : "failed", desc->conn_handle);
    }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *pCharacteristic) {
        Serial.print(pCharacteristic->getUUID().toString().c_str());
        Serial.print(": onRead(), value: ");
        Serial.println(pCharacteristic->getValue().c_str());
    };

    void onWrite(NimBLECharacteristic *pCharacteristic) {
        Serial.print(pCharacteristic->getUUID().toString().c_str());
        Serial.print(": onWrite(), value: ");
        Serial.println(pCharacteristic->getValue().c_str());
    };

    void onNotify(NimBLECharacteristic *pCharacteristic) {
        Serial.println("Sending notification to clients");
    };

    void onStatus(NimBLECharacteristic *pCharacteristic, Status status, int code) {
        String str = ("Notification/Indication status code: ");
        str += status;
        str += ", return code: ";
        str += code;
        str += ", ";
        str += NimBLEUtils::returnCodeToString(code);
        Serial.println(str);
    };

    void onSubscribe(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc, uint16_t subValue) {
        String str = "Client ID: ";
        str += desc->conn_handle;
        str += " Address: ";
        str += std::string(NimBLEAddress(desc->peer_ota_addr)).c_str();
        if (subValue == 0) {
            str += " Unsubscribed to ";
        } else if (subValue == 1) {
            str += " Subscribed to notfications for ";
        } else if (subValue == 2) {
            str += " Subscribed to indications for ";
        } else if (subValue == 3) {
            str += " Subscribed to notifications and indications for ";
        }
        str += std::string(pCharacteristic->getUUID()).c_str();
        Serial.println(str);
    };
};

class DescriptorCallbacks : public NimBLEDescriptorCallbacks {
    void onWrite(NimBLEDescriptor *pDescriptor) {
        std::string dscVal = pDescriptor->getValue();
        Serial.print("Descriptor witten value:");
        Serial.println(dscVal.c_str());
    };

    void onRead(NimBLEDescriptor *pDescriptor) {
        Serial.print(pDescriptor->getUUID().toString().c_str());
        Serial.println(" Descriptor read");
    };
};

static DescriptorCallbacks dscCallbacks;
static CharacteristicCallbacks chrCallbacks;

static int ble_sm_read_bond(uint16_t conn_handle, struct ble_store_value_sec *out_bond) {
    struct ble_store_key_sec key_sec;
    struct ble_gap_conn_desc desc;
    int rc;

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        return rc;
    }

    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr = desc.peer_id_addr;

    rc = ble_store_read_peer_sec(&key_sec, out_bond);
    return rc;
}

bool tryGetIrkFromConnection(uint16_t conn_handle, std::string &irk) {
    struct ble_store_value_sec bond;
    int rc;

    rc = ble_sm_read_bond(conn_handle, &bond);
    if (rc != 0)
        return false;

    if (!bond.irk_present)
        return false;

    std::string output;
    output.reserve(32);
    for (int i = 0; i < 16; i++) {
        auto c = bond.irk[15 - i];
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
    }
    irk = output;
    return true;
}

void Setup() {
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, true, true);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    heartRate = pServer->createService("180D");
    NimBLECharacteristic *bpm = heartRate->createCharacteristic("2A37", NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC, 2);
    bpm->setCallbacks(&chrCallbacks);

    deviceInfo = pServer->createService("180A");
    NimBLECharacteristic *manufName = deviceInfo->createCharacteristic("2A29", NIMBLE_PROPERTY::READ_ENC);
    manufName->setValue("ESPresense");
    manufName->setCallbacks(&chrCallbacks);
    NimBLECharacteristic *appearance = deviceInfo->createCharacteristic("2A01", NIMBLE_PROPERTY::READ_ENC, 2);
    appearance->setValue((int16_t)0x4142);
    appearance->setCallbacks(&chrCallbacks);
    NimBLECharacteristic *modelNum = deviceInfo->createCharacteristic("2A24", NIMBLE_PROPERTY::READ_ENC);
    modelNum->setValue(std::string(room.c_str()));
    modelNum->setCallbacks(&chrCallbacks);

    heartRate->start();
    deviceInfo->start();

    uint32_t nodeId = (uint32_t)(ESP.getEfuseMac() >> 24);
    major = (nodeId & 0xFFFF0000) >> 16;
    minor = nodeId & 0xFFFF;

    BLEBeacon oBeacon = BLEBeacon();
    oBeacon.setManufacturerId(0x4C00);
    oBeacon.setProximityUUID(espresenseUUID);
    oBeacon.setMajor(major);
    oBeacon.setMinor(minor);
    oBeacon.setSignalPower(BleFingerprintCollection::txRefRssi);
    oAdvertisementData = new NimBLEAdvertisementData();
    oAdvertisementData->setFlags(BLE_HS_ADV_F_BREDR_UNSUP);
    oAdvertisementData->setManufacturerData(oBeacon.getData());

    pServer->start();
}

bool Loop() {
    static bool lastEnrolling = true;
    if (enrolling != lastEnrolling) {
        auto pAdvertising = NimBLEDevice::getAdvertising();
        if (enrolling) {
            pAdvertising->reset();
            pAdvertising->setScanResponse(true);
            pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
            pAdvertising->setMinPreferred(0x12);
            pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
            pAdvertising->addServiceUUID(heartRate->getUUID());
            pAdvertising->start();
            Serial.printf("%u Advert | HRM\r\n", xPortGetCoreID());
        } else {
            pAdvertising->reset();
            pAdvertising->setScanResponse(false);
            pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
            pAdvertising->setAdvertisementData(*oAdvertisementData);
            pAdvertising->start();
            Serial.printf("%u Advert | iBeacon\r\n", xPortGetCoreID());
        }
        lastEnrolling = enrolling;
        HttpWebServer::SendState();
    }

    if (enrolling && enrollingEndMillis < millis()) {
        enrolling = false;
    }

    if (millis() - lastLoop > 500) {
        lastLoop = millis();

        if (enrolling) HttpWebServer::SendState();
        if (pServer->getConnectedCount()) {
            NimBLEService *pSvc = pServer->getServiceByUUID("180D");
            if (pSvc) {
                NimBLECharacteristic *pChr = pSvc->getCharacteristic("2A37");
                if (pChr) {
                    static unsigned long lastNotify = 0;
                    if (millis() - lastNotify > 250) {  // throttle to 4Hz
                        lastNotify = millis();
                        uint8_t hr = micros() & 0xFF;
                        uint8_t buf[2] = {0b00000110, hr};
                        pChr->setValue(buf, 2);
                        pChr->notify();
                    }
                }
            }

            if (enrolling && connectionToEnroll > -1) {
                std::string irk;
                if (tryGetIrkFromConnection(connectionToEnroll, irk)) {
                    if (newId.isEmpty()) newId = newName.isEmpty() ? String("irk:") + irk.c_str() : slugify(newName);
                    sendConfig(String("irk:") + irk.c_str(), newId, newName);
                    enrolledId = newId;
                    newId = newName = "";
                    enrolling = false;
                    pServer->disconnect(connectionToEnroll);
                    connectionToEnroll = -1;
                }
            }
        }
    }

    return true;
}

bool Command(String &command, String &pay) {
    if (command == "enroll") {
        const int separatorIndex = pay.indexOf('|');
        if (separatorIndex != -1) {
            newId = pay.substring(0, separatorIndex);
            newName = pay.substring(separatorIndex + 1);
        } else {
            newId = "";
            newName = pay.equals("PRESS") ? "" : pay;
        }
        enrolling = true;
        enrollingEndMillis = millis() + 120000;
        HttpWebServer::SendState();
        return true;
    }
    if (command == "cancelEnroll") {
        enrolledId = newId = newName = "";
        enrolling = false;
        HttpWebServer::SendState();
        return true;
    }
    return false;
}

bool SendDiscovery() {
    return sendConfig(Sprintf("iBeacon:e5ca1ade-f007-ba11-0000-000000000000-%hu-%hu", major, minor), "node:" + id, room) &&
           sendButtonDiscovery("Enroll", EC_CONFIG);
}
}  // namespace Enrollment
