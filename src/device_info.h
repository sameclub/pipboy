#pragma once
#include <Arduino.h>
#include <time.h>

// Index into the firmware key table; shared by main.cpp and deviceInfoAction().
enum KeyIndex { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B,
                KEY_X, KEY_Y, KEY_SELECT, KEY_START, KEY_APP, KEY_COUNT };
enum InfoSection { INFO_DEVICE, INFO_BATTERY, INFO_CLOCK, INFO_WIFI, INFO_BLE, INFO_KEYS };

void beginDeviceInfo(bool cardMounted);
void updateDeviceInfo();
bool clockLocalTime(tm &local);
bool deviceInfoProvisioning();
void toggleProvisioning();
String batterySummary();
int batteryLevel(); // -1 when the ADC reading is invalid.
int wifiSignalBars(); // 0 when disconnected, otherwise 1..4.
void radioStatus(String &wifi, String &ble);
void deviceInfoRows(InfoSection section, String (&rows)[6], String &hint);
void deviceInfoAction(InfoSection section, int key, bool cardMounted);
bool deviceInfoBack(InfoSection section);

// Sleep preparation waits for scans/credential saving to finish.
bool deviceInfoCanSleep();
bool suspendDeviceInfo();
void resumeDeviceInfo();
