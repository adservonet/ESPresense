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

    int inputPin = -1; // 32;
    int outputPin = -1; // 26;

    int sensorInterval = 30000;
    int pumpInterval = 10000;

    int minReading = 3000;
    int maxReading = 1000;
    int readingCount = 0;

    int rawADC;
    char info[30];

    void Setup()
    {
        if (inputPin >= 0 && outputPin >= 0) {
            pinMode(inputPin, INPUT);
            pinMode(outputPin, OUTPUT);
        }
    }

    void Loop() {
        if (inputPin<0 && outputPin<0) return;

        if (M5WaterPreviousPumpMillis == 0 || millis() - M5WaterPreviousPumpMillis >= pumpInterval) {
            M5WaterPreviousPumpMillis = millis();
            digitalWrite(outputPin, false);
            pub((roomsTopic + "/pump").c_str(), 0, 1, "OFF");
        }

        if (M5WaterPreviousSensorMillis == 0 || millis() - M5WaterPreviousSensorMillis >= sensorInterval) {
            M5WaterPreviousSensorMillis = millis();
            rawADC = analogRead(inputPin);

            if( rawADC < minReading ) {
                minReading = rawADC;
                if( readingCount >= 5 ) pub((roomsTopic + "/adc_min").c_str(), 0, 1, String(rawADC).c_str());
            }

            if( rawADC > maxReading ) {
                maxReading = rawADC;
                if( readingCount >= 5 ) pub((roomsTopic + "/adc_max").c_str(), 0, 1, String(rawADC).c_str());
            }

            readingCount++;
            if( readingCount < 5 ) return;


            int prettyValue = map(rawADC, maxReading, minReading, 0, 100);
            if(prettyValue > 100) prettyValue = 100;
            if(prettyValue < 0) prettyValue = 0;

            pub((roomsTopic + "/moisture").c_str(), 0, 1, String(prettyValue).c_str());
            pub((roomsTopic + "/adc").c_str(), 0, 1, String(rawADC).c_str());




            if( prettyValue < 25 ) {
                digitalWrite(outputPin, true);
                pub((roomsTopic + "/pump").c_str(), 0, 1, "ON");
            }
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
        inputPin = HeadlessWiFiSettings.integer("m5moisture_pin", -1, "Moisture sensor pin (-1 for disable)");
        outputPin = HeadlessWiFiSettings.integer("m5pump_pin", -1, "Pump switch pin (-1 for disable)");
    }

    void SerialReport()
    {
        if (inputPin<0) return;
        Serial.print("Moist Sensor: ");
        Serial.println((inputPin>=0 ? "pin " + String(inputPin) : "disabled").c_str());
    }


    bool SendDiscovery()
    {
        return sendSensorDiscovery("Moisture", EC_NONE, "moisture", "%") && sendSensorDiscovery("adc", EC_NONE, "number") && sendSensorDiscovery("pump", EC_NONE, "switch") && sendSensorDiscovery("adc_min", EC_NONE, "number") && sendSensorDiscovery("adc_max", EC_NONE, "number");
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
