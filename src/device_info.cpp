#include "device_info.h"
#include <WifiPortal.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <esp_mac.h>
#include <esp_bt.h>
#include <esp_sntp.h>
#include <atomic>
#include <math.h>

namespace {
constexpr int BATTERY_PIN = 8;
constexpr float DIVIDER = 4.0f; // (300 + 100) / 100
constexpr int MAX_BLE_RESULTS = 12;

WifiPortal wifiPortal({"Pip-Boy", "#20b845", "pipboy-net"});

String configState = "NO CONFIG";
float batteryScale = 1.0f, batteryVolts = 0;
uint32_t adcMV = 0, lastSample = 0, lastPoll = 0;
bool batteryValid = false, ntpStarted = false, wasConnected = false;
bool wifiBusy = false, wifiResults = false, bleResults = false;
int wifiCount = 0, wifiIndex = 0, bleCount = 0, bleIndex = 0;
String wifiScanState = "A: SCAN NEARBY";
std::atomic<bool> bleBusy{false}, bleReady{false}, timeSynced{false};
String bleState = "RADIO OFF";
struct BleEntry { String name, address; int rssi; };
BleEntry bleEntries[MAX_BLE_RESULTS];
bool bleTruncated = false;
bool restorePortal = false;

String mac(esp_mac_type_t type) {
    uint8_t bytes[6];
    if (esp_read_mac(bytes, type) != ESP_OK) return "UNAVAILABLE";
    char text[18];
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return text;
}

bool readConfig(bool cardMounted) {
    if (!cardMounted) { configState = "NO TF CARD"; return false; }
    File file = SD_MMC.open("/pipboy.ini", FILE_READ);
    if (!file) { configState = "NO /pipboy.ini"; return false; }
    if (file.size() > 1024) { file.close(); configState = "CONFIG TOO LARGE"; return false; }
    float nextScale = 1.0f;
    bool valid = true;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.endsWith("\r")) line.remove(line.length()-1);
        if (line.startsWith("\xEF\xBB\xBF")) line.remove(0, 3);
        if (line.isEmpty() || line.startsWith("#")) continue;
        int separator = line.indexOf('=');
        if (separator < 0) { valid = false; break; }
        String key = line.substring(0, separator), value = line.substring(separator+1);
        key.trim();
        if (key == "battery_scale") {
            value.trim();
            char *end = nullptr;
            nextScale = strtof(value.c_str(), &end);
            if (end == value.c_str() || *end || !isfinite(nextScale) || nextScale < 0.8f || nextScale > 1.2f) valid = false;
        } else valid = false;
    }
    file.close();
    if (!valid) { configState = "CONFIG INVALID"; return false; }
    batteryScale = nextScale;
    configState = "CONFIG LOADED";
    batteryValid = false;
    return true;
}

void sampleBattery() {
    uint32_t sum = 0;
    for (int i=0; i<16; ++i) sum += analogReadMilliVolts(BATTERY_PIN);
    adcMV = (sum + 8) / 16;
    float measured = adcMV * DIVIDER * batteryScale / 1000.0f;
    bool plausible = measured >= 2.5f && measured <= 4.5f;
    batteryVolts = plausible && batteryValid ? batteryVolts * 0.75f + measured * 0.25f : measured;
    batteryValid = plausible;
}

int batteryPercent() {
    const float volts[] = {3.4f, 3.6f, 3.7f, 3.8f, 3.95f, 4.2f};
    const int percent[] = {0, 10, 25, 50, 75, 100};
    if (batteryVolts <= volts[0]) return 0;
    for (int i=1; i<6; ++i) {
        if (batteryVolts <= volts[i])
            return lroundf(percent[i-1] + (batteryVolts-volts[i-1]) * (percent[i]-percent[i-1]) / (volts[i]-volts[i-1]));
    }
    return 100;
}

void scanBLE(void *) {
    if (!bleReady.load()) bleReady.store(BLEDevice::init("PipBoy"));
    bleCount = 0; bleTruncated = false;
    if (!bleReady.load()) bleState = "BLE INIT FAILED";
    else {
        BLEScan *scan = BLEDevice::getScan();
        scan->setActiveScan(false);
        scan->setInterval(160); scan->setWindow(80);
        BLEScanResults *results = scan->start(5, false);
        if (!results) bleState = "BLE SCAN FAILED";
        else {
            int found = results->getCount();
            bleCount = min(found, MAX_BLE_RESULTS);
            bleTruncated = found > MAX_BLE_RESULTS;
            for (int i=0; i<bleCount; ++i) {
                BLEAdvertisedDevice device = results->getDevice(i);
                bleEntries[i] = {String(device.getName().c_str()), String(device.getAddress().toString().c_str()), device.getRSSI()};
            }
            bleState = found ? "SCAN COMPLETE" : "NO ADVERTISERS";
        }
        scan->clearResults();
    }
    bleBusy.store(false);
    vTaskDelete(nullptr);
}
}

bool clockLocalTime(tm &local) {
    time_t now = time(nullptr);
    if (now < 1704067200) return false;
    return localtime_r(&now, &local) != nullptr;
}

bool deviceInfoProvisioning() { return wifiPortal.isProvisioning(); }

String batterySummary() {
    return batteryValid ? String(batteryVolts, 2) + "V ~" + String(batteryPercent()) + "%" : "BAT: CHECK ADC";
}

int batteryLevel() { return batteryValid ? batteryPercent() : -1; }

int wifiSignalBars() {
    if (!wifiPortal.isConnected()) return 0;
    int rssi = WiFi.RSSI();
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -80 ? 2 : 1;
}

void radioStatus(String &wifi, String &ble) {
    wifi = wifiPortal.isVerifying() ? "TESTING" :
        wifiPortal.isProvisioning() ? "SETUP" : wifiBusy ? "SCANNING" :
        wifiPortal.isConnected() ? "CONNECTED" : "DISCONNECTED";
    ble = bleBusy.load() ? "SCANNING" : bleReady.load() ? "IDLE" : "OFF";
}

void beginDeviceInfo(bool cardMounted) {
    setenv("TZ", "CST-8", 1); tzset();
    analogReadResolution(12);
    analogSetPinAttenuation(BATTERY_PIN, ADC_6db);
    sampleBattery();
    readConfig(cardMounted);
    sampleBattery();
    wifiPortal.begin();
}

void toggleProvisioning() {
    if (wifiPortal.isProvisioning()) wifiPortal.stopProvisioning();
    else if (wifiBusy || bleBusy.load()) Serial.println("WI-FI SETUP: wait for the active radio scan");
    else {
        wifiResults = false; wifiCount = 0;
        wifiPortal.startProvisioning();
    }
}

void updateDeviceInfo() {
    uint32_t now = millis();
    wifiPortal.update();
    if (now-lastSample >= 2000) { lastSample = now; sampleBattery(); }
    if (now-lastPoll < 250) return;
    lastPoll = now;
    bool connected = wifiPortal.isConnected();
    if (connected && (!ntpStarted || !wasConnected)) {
        sntp_set_time_sync_notification_cb([](timeval *) { timeSynced.store(true); });
        configTzTime("CST-8", "ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org");
        ntpStarted = true;
    }
    wasConnected = connected;
    if (wifiBusy) {
        int result = WiFi.scanComplete();
        if (result != WIFI_SCAN_RUNNING) {
            wifiBusy = false;
            wifiCount = max(0, min(result, 255));
            wifiScanState = result < 0 ? "SCAN FAILED" : result == 0 ? "NO NETWORKS" : "SCAN COMPLETE";
        }
    }
}

bool deviceInfoBack(InfoSection section) {
    if (section == INFO_WIFI && wifiPortal.isProvisioning()) {
        wifiPortal.stopProvisioning();
        wifiResults = false;
        return true;
    }
    if (section == INFO_WIFI && wifiResults) { wifiResults = false; return true; }
    if (section == INFO_BLE && bleResults) { bleResults = false; return true; }
    return false;
}

void deviceInfoAction(InfoSection section, int key, bool cardMounted) {
    if (key == KEY_B) { deviceInfoBack(section); return; }
    if (section == INFO_WIFI && key == KEY_X && !wifiResults && !wifiBusy && !bleBusy.load()) {
        toggleProvisioning();
        return;
    }
    if (section == INFO_WIFI && wifiResults && !wifiBusy && wifiCount && (key == KEY_X || key == KEY_Y))
        wifiIndex = (wifiIndex + (key == KEY_X ? 1 : wifiCount-1)) % wifiCount;
    if (section == INFO_BLE && bleResults && !bleBusy.load() && bleCount && (key == KEY_X || key == KEY_Y))
        bleIndex = (bleIndex + (key == KEY_X ? 1 : bleCount-1)) % bleCount;
    if (key != KEY_A || wifiBusy || bleBusy.load()) return;
    if (section == INFO_BATTERY) {
        readConfig(cardMounted);
        sampleBattery();
    } else if (section == INFO_CLOCK) {
        ntpStarted = false; timeSynced.store(false);
        if (!wifiPortal.hasCredentials() && !wifiPortal.isProvisioning()) toggleProvisioning();
    } else if (section == INFO_WIFI) {
        WiFi.persistent(false);
        if (wifiPortal.isProvisioning()) return;
        WiFi.mode(WIFI_STA);
        WiFi.scanDelete(); wifiCount = 0; wifiIndex = 0; wifiResults = true;
        int result = WiFi.scanNetworks(true);
        wifiBusy = result == WIFI_SCAN_RUNNING;
        wifiScanState = wifiBusy ? "SCANNING..." : result < 0 ? "SCAN FAILED" : "SCAN COMPLETE";
        if (result >= 0) wifiCount = min(result, 255);
    } else if (section == INFO_BLE) {
        if (wifiPortal.isProvisioning()) return;
        bleIndex = 0; bleResults = true;
        bleBusy.store(true);
        if (xTaskCreate(scanBLE, "pipboy-ble", 6144, nullptr, 1, nullptr) != pdPASS) {
            bleState = "NO SCAN MEMORY"; bleCount = 0; bleBusy.store(false);
        }
    }
}

void deviceInfoRows(InfoSection section, String (&rows)[6], String &hint) {
    hint = "A LOAD  B BACK";
    if (section == INFO_BATTERY) {
        rows[0] = "BATTERY";
        rows[1] = batterySummary();
        rows[2] = "ADC " + String(adcMV) + " mV";
        rows[3] = "SCALE " + String(batteryScale, 4);
        rows[4] = "CHARGE: UNKNOWN";
        rows[5] = configState;
    } else if (section == INFO_CLOCK) {
        tm local = {};
        bool valid = clockLocalTime(local);
        char date[20] = "WAITING FOR TIME", clock[20] = "--:--:--";
        if (valid) { strftime(date, sizeof(date), "%Y-%m-%d", &local); strftime(clock, sizeof(clock), "%H:%M:%S UTC+8", &local); }
        rows[0] = "CLOCK"; rows[1] = date; rows[2] = clock;
        rows[3] = timeSynced.load() ? "NTP SYNCED" : valid ? "RTC / NTP PENDING" : "NTP WAIT NETWORK";
        if (!valid && wifiPortal.isConnected()) rows[3] = "NTP WAIT SERVER";
        rows[4] = "UP " + String(millis()/1000) + " SEC";
        rows[5] = wifiPortal.isConnected() ? "WIFI CONNECTED" : "START: WIFI SETUP";
        hint = "A SYNC  B BACK";
    } else if (section == INFO_WIFI) {
        hint = "A SCAN  B BACK";
        rows[0] = "WI-FI";
        if (wifiPortal.isVerifying()) {
            // The hotspot is down while the new credentials are tried, so the
            // screen is the only place the result shows up.
            rows[0] = "WI-FI TEST";
            rows[1] = "TRYING NEW WIFI";
            rows[2] = "HOTSPOT OFF NOW";
            rows[3] = "RETURNS IF FAILS";
            rows[4] = wifiPortal.state();
            hint = "WAIT WI-FI";
        } else if (wifiPortal.isProvisioning()) {
            rows[0] = "PHONE SETUP";
            rows[1] = wifiPortal.apName(); rows[2] = "PASS " + wifiPortal.apPassword();
            rows[3] = "192.168.4.1";
            rows[4] = wifiPortal.state();
            rows[5] = "B / START: CLOSE";
            hint = "B CLOSE HOTSPOT";
        } else if (wifiResults) {
            rows[1] = wifiScanState;
            if (!wifiBusy && wifiCount) {
                rows[0] = "WI-FI " + String(wifiIndex+1) + "/" + String(wifiCount);
                rows[1] = WiFi.SSID(wifiIndex);
                if (rows[1].isEmpty()) rows[1] = "<HIDDEN SSID>";
                rows[2] = WiFi.BSSIDstr(wifiIndex);
                rows[3] = String(WiFi.RSSI(wifiIndex)) + "dBm CH " + String(WiFi.channel(wifiIndex));
                rows[4] = WiFi.encryptionType(wifiIndex) == WIFI_AUTH_OPEN ? "OPEN" : "SECURED";
            }
            hint = wifiBusy ? "SCANNING B STATUS" : wifiCount ? "X/Y ITEM B STATUS" : "A SCAN B STATUS";
        } else {
            bool connected = wifiPortal.isConnected();
            rows[1] = connected ? "CONNECTED" : WiFi.getMode() == WIFI_OFF ? "RADIO OFF" : "DISCONNECTED";
            rows[2] = connected ? wifiPortal.ssid() : "X: PHONE SETUP";
            rows[3] = connected ? WiFi.localIP().toString() : "A: SCAN NEARBY";
            rows[4] = connected ? String(WiFi.RSSI()) + "dBm CH " + String(WiFi.channel()) : "STA MAC";
            rows[5] = mac(ESP_MAC_WIFI_STA);
            hint = "A SCAN  X SETUP";
            if (wifiBusy || bleBusy.load()) {
                rows[1] = wifiBusy ? "WI-FI SCANNING" : "BLE SCANNING";
                hint = "WAIT SCAN  B BACK";
            }
        }
    } else if (section == INFO_BLE) {
        rows[0] = "BLUETOOTH LE";
        hint = bleResults ? "A SCAN B STATUS" : "A SCAN  B BACK";
        if (bleBusy.load()) { rows[1] = "SCANNING (5 SEC)"; hint = bleResults ? "SCANNING B STATUS" : "SCANNING B BACK"; return; }
        rows[1] = bleState;
        if (bleResults && bleCount) {
            rows[0] = "BLE " + String(bleIndex+1) + "/" + String(bleCount) + (bleTruncated ? "+" : "");
            rows[1] = bleEntries[bleIndex].name.isEmpty() ? "<UNNAMED>" : bleEntries[bleIndex].name;
            rows[2] = bleEntries[bleIndex].address;
            rows[3] = String(bleEntries[bleIndex].rssi) + " dBm";
            rows[4] = "ADVERTISEMENT ONLY";
            hint = "X/Y ITEM B STATUS";
        } else {
            rows[2] = "NO CONNECTIONS";
            rows[3] = "CLASSIC: UNSUPPORTED";
            rows[4] = bleReady.load() ? "IDLE / LOCAL MAC" : "LOCAL MAC";
            rows[5] = mac(ESP_MAC_BT);
        }
        if (wifiPortal.isProvisioning() || wifiBusy) {
            rows[1] = wifiPortal.isVerifying() ? "WI-FI TEST ACTIVE" :
                wifiPortal.isProvisioning() ? "WI-FI SETUP ACTIVE" : "WI-FI SCANNING";
            hint = "WAIT WI-FI  B BACK";
        }
    }
}

bool deviceInfoCanSleep() { return !wifiBusy && !bleBusy.load() && !wifiPortal.isProvisioning(); }

bool suspendDeviceInfo() {
    restorePortal = wifiPortal.isProvisioning();
    if (restorePortal) wifiPortal.stopProvisioning();
    WiFi.setAutoReconnect(false);
    bool wifiStopped = WiFi.mode(WIFI_OFF);
    if (bleReady.load()) {
        BLEDevice::deinit(false);
        bleReady.store(false);
    }
    bleState = "RADIO OFF";
    return wifiStopped && esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED;
}

void resumeDeviceInfo() {
    wasConnected = false;
    lastPoll = millis() - 250;
    lastSample = millis() - 2000;
    if (restorePortal) wifiPortal.startProvisioning();
    // WifiPortal handles reconnection internally via update().
}
