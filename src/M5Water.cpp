#ifdef SENSORS
#include "M5Water.h"
#include "globals.h"
#include "mqtt.h"
#include "defaults.h"
#include <HeadlessWiFiSettings.h>
#include <AsyncMqttClient.h>
#include "string_utils.h"

namespace M5Water
{
    unsigned long M5WaterPreviousSensorMillis = 0;
    unsigned long M5WaterPreviousPumpMillis = 0;

    int inputPin = 32;
    int outputPin = 26;

    int sensorInterval = 100000;
    int pumpInterval = 30000;

    int minReading = 1000;
    int maxReading = 2000;

    bool flag = true;
    int rawADC;

    char info[30];


    void Setup()
    {
        pinMode(inputPin, INPUT);
        pinMode(outputPin, OUTPUT);
    }

    void Loop()
    {
        if (M5WaterPreviousPumpMillis == 0 || millis() - M5WaterPreviousPumpMillis >= pumpInterval) {
            M5WaterPreviousPumpMillis = millis();
            digitalWrite(outputPin, false);
        }

        if (M5WaterPreviousSensorMillis == 0 || millis() - M5WaterPreviousSensorMillis >= sensorInterval) {
            M5WaterPreviousSensorMillis = millis();
            rawADC = analogRead(inputPin);
            int prettyValue = map(rawADC, maxReading, minReading, 0, 100);
            if(prettyValue > 100) prettyValue = 100;
            if(prettyValue < 0) prettyValue = 0;

            pub((roomsTopic + "/moisture").c_str(), 0, 1, String(prettyValue).c_str());

            if( prettyValue < 25 ) digitalWrite(outputPin, true);
        }
        delay(100);
    }


//          Serial.print("Watering ADC value: ");
//        Serial.println(rawADC);

//        digitalWrite(pumpPin, flag);
//        flag = !flag;

//        delay(100);
//    }


    void ConnectToWifi()
    {
     //   inputPin = HeadlessWiFiSettings.integer("m5moisture_pin", -1, "Moisture sensor pin (-1 for disable)");
      //  pumpPin = HeadlessWiFiSettings.integer("pumpPin", -1, "Pump switch pin (-1 for disable)");
    }

    void SerialReport()
    {
        if (inputPin<0) return;
        Serial.print("Moist Sensor: ");
        Serial.println((inputPin>=0 ? "pin " + String(inputPin) : "disabled").c_str());
    }


    bool SendDiscovery()
    {
        return sendSensorDiscovery("Moisture", EC_NONE, "moisture", "%");
    }

    bool Command(String& command, String& pay)
    {
        return false;
    }

    bool SendOnline()
    {
        return true;
    }
}
#endif
