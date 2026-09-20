#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SD_MMC.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "walk_frames.h"
#include "device_info.h"

// Explicit bus setup bypasses ST7789's default SPI mode 3.
Arduino_HWSPI bus(12, 3, 11, 46, -1);
Arduino_ST7789 panel(&bus, 7, 4, false, 240, 240, 0, 0, 0, 0);
Arduino_Canvas screen(240, 240, nullptr);
uint16_t *previousFrame = nullptr;
bool screenSleeping = false, fullRefresh = true;
constexpr uint32_t AUTO_SLEEP_MS = 10000;
uint32_t lastInteraction = 0;
constexpr uint16_t GREEN = 0x07ec, DIM = 0x0507, BLACK = 0x0000;
int keyBank = 0;
InfoSection infoSection = INFO_DEVICE;
int radioSelection = 0, systemSelection = 0;
bool detailOpen = false;
const InfoSection systemSections[] = {INFO_DEVICE, INFO_BATTERY, INFO_CLOCK, INFO_KEYS};
const char *systemItems[] = {"DEVICE", "BATTERY", "CLOCK", "KEY TEST"};
const char *systemHints[] = {"DEVICE & STORAGE", "VOLTAGE & CALIBRATE", "BEIJING / UTC+8", "GPIO & KEY STATES"};
// Draw off-screen, then transfer only changed rows: never visibly clear the LCD.
void present() {
    if (screenSleeping) return;
    uint16_t *pixels = screen.getFramebuffer();
    for (int y = 0; y < 240; ++y) {
        uint16_t *row = pixels + y * 240;
        if (fullRefresh || !previousFrame || memcmp(row, previousFrame + y * 240, 480)) {
            panel.draw16bitRGBBitmap(0, y, row, 240, 1);
            if (previousFrame) memcpy(previousFrame + y * 240, row, 480);
        }
    }
    fullRefresh = false;
}
struct Key { const char *name; uint8_t pin; bool raw = false, down = false; uint32_t changed = 0; };
// keys[] must stay in KeyIndex order (device_info.h); both dispatch on indices.
Key keys[] = {{"UP",42},{"DOWN",45},{"LEFT",21},{"RIGHT",14},{"A",1},
              {"B",43},{"X",44},{"Y",2},{"SELECT",48},{"START",47},{"APP",10}};
static_assert(sizeof(keys)/sizeof(keys[0]) == KEY_COUNT, "keys[] out of sync with KeyIndex");
constexpr int KEY_BANKS = (KEY_COUNT + 4) / 5;
enum Page { STAT, FILES, RADIO, SYSTEM };
Page page = STAT;
bool sdOK = false, displayOK = false, dirty = true;
String sdTest = "NOT RUN", cwd = "/", selectedName;
bool selectedDir = false;
int selected = 0, total = 0;
uint32_t selectedSize = 0, lastFrame = 0, lastAnimation = 0, lastLog = 0;
const char *tabs[] = {"STAT", "DATA", "RADIO", "SYS"};
constexpr uint32_t WALK_FRAME_MS = 160;

void label(int x, int y, const String &text, uint8_t scale = 2, uint16_t color = GREEN) {
    screen.setTextSize(scale); screen.setTextColor(color); screen.setCursor(x,y); screen.print(text);
}
String clipped(const String &s, unsigned n) { return s.length() > n ? s.substring(0,n-1)+"~" : s; }
void centered(int y, const String &text, uint8_t scale = 2, uint16_t color = GREEN) {
    int width = text.length() ? (text.length()*6-1)*scale : 0;
    label((240-width)/2,y,text,scale,color);
}
void menuRow(int y, const String &text, bool active) {
    if (active) screen.fillRect(6,y-4,228,24,GREEN);
    label(12,y,clipped(text,18),2,active?BLACK:GREEN);
}
void sdRoundtrip() {
    if (!sdOK) { sdTest = "NO CARD"; return; }
    char path[64]; snprintf(path,sizeof(path),"/pipboy-check-%08lx.tmp",(unsigned long)esp_random());
    if (SD_MMC.exists(path)) { sdTest = "NAME EXISTS"; return; }
    const char payload[] = "PIPBOY S3AI SDMMC 1-bit roundtrip\n";
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) { sdTest = "CREATE FAIL"; return; }
    bool ok = f.write((const uint8_t*)payload,sizeof(payload)-1) == sizeof(payload)-1;
    f.close(); f = SD_MMC.open(path,FILE_READ);
    char actual[sizeof(payload)] = {};
    ok = ok && f && f.size() == sizeof(payload)-1;
    if (f) ok = (f.readBytes(actual,sizeof(payload)-1) == sizeof(payload)-1) && ok;
    f.close(); ok = ok && memcmp(actual,payload,sizeof(payload)-1) == 0;
    bool removed = SD_MMC.remove(path);
    sdTest = ok && removed ? "PASS" : !removed ? "DELETE FAIL" : "R/W FAIL";
    Serial.printf("SD VERIFY path=%s read/write=%s delete=%s\n",path,ok?"PASS":"FAIL",removed?"PASS":"FAIL");
}
void mountCard() {
    SD_MMC.setPins(40,39,41);
    sdOK = SD_MMC.begin("/sdcard",true,false);
    Serial.printf("SDMMC 1-bit mount=%s\n",sdOK?"PASS":"FAIL");
    sdRoundtrip();
}
void mascot() {
    screen.draw16bitRGBBitmap(84, 109, walkFrames[(millis()/WALK_FRAME_MS)%WALK_FRAME_COUNT], 72, 104);
}
void rule(int y) {
    screen.drawFastHLine(8,y,224,GREEN);
    screen.drawFastHLine(8,y+1,224,GREEN);
}
void footer(const String &text) {
    screen.drawFastHLine(8,216,224,DIM);
    centered(222,clipped(text,19));
}
void drawStat() {
    tm local = {};
    char clock[6] = "--:--", date[20] = "DATE NOT SYNCED";
    if (clockLocalTime(local)) {
        strftime(clock,sizeof(clock),"%H:%M",&local);
        const char *days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
        snprintf(date,sizeof(date),"%s %04d-%02d-%02d",days[local.tm_wday],
            local.tm_year+1900,local.tm_mon+1,local.tm_mday);
    }
    centered(40,clock,5);
    centered(89,date);
    mascot();
    int percent = batteryLevel();
    screen.drawRect(24,133,32,16,GREEN);
    screen.drawRect(25,134,30,14,GREEN);
    screen.fillRect(56,138,4,6,GREEN);
    if (percent > 0) screen.fillRect(28,137,(24*percent+50)/100,8,GREEN);
    String charge = percent >= 0 ? "~"+String(percent)+"%" : "--%";
    label((84-(charge.length()*12-2))/2,160,charge);
    label(percent >= 0 ? 24 : 12,186,percent >= 0 ? "BAT" : "CHECK",2,DIM);
    int bars = wifiSignalBars();
    for (int i=0;i<4;++i) screen.fillRect(182+i*7,144-i*4,4,4+i*4,i<bars?GREEN:DIM);
    label(168,160,"WI-FI");
    String state = deviceInfoProvisioning() ? "AP" : bars ? "ON" : "OFF";
    label(156+(84-(state.length()*12-2))/2,186,state,2,DIM);
    footer("L/R PAGE");
}
void drawFiles() {
    label(8,41,clipped(cwd,12));
    selectedName = ""; total = 0;
    selectedDir = false; selectedSize = 0;
    if (!sdOK) { label(8,84,"NO TF CARD"); label(8,115,"A: RETRY MOUNT"); footer("A RETRY  B BACK"); return; }
    File dir = SD_MMC.open(cwd);
    if (!dir || !dir.isDirectory()) { label(8,84,"CANNOT OPEN DIR"); footer("B BACK"); return; }
    const int start = (selected / 4)*4;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (total == selected) { selectedName = f.name(); selectedDir = f.isDirectory(); selectedSize = f.size(); }
        if (total >= start && total < start+4) {
            int y = 72+(total-start)*28;
            menuRow(y,String(f.isDirectory()?"+ ":"  ")+clipped(String(f.name()),16),total==selected);
        }
        ++total; f.close();
        if ((total & 31) == 0) yield();
    }
    dir.close();
    if (!total) label(8,88,"EMPTY DIRECTORY");
    else if (selected >= total) { selected = total-1; dirty = true; }
    label(164,41,clipped(String(total?selected+1:0)+"/"+String(total),6),2,DIM);
    if (total) label(8,192,selectedDir?"DIRECTORY":String(selectedSize)+" BYTES",2,DIM);
    footer("A OPEN  B PARENT");
}
void drawMenu() {
    if (page == RADIO) {
        label(8,41,"WIRELESS",2,DIM);
        menuRow(78,"WI-FI",radioSelection==0);
        menuRow(112,"BLUETOOTH LE",radioSelection==1);
        String wifi, ble;
        radioStatus(wifi,ble);
        label(8,166,clipped("WI-FI: "+wifi,19));
        label(8,190,clipped("BLE: "+ble,19),2,DIM);
    } else {
        label(8,41,"SYSTEM",2,DIM);
        for (int i=0;i<4;++i) menuRow(70+i*29,systemItems[i],systemSelection==i);
        label(8,194,systemHints[systemSelection],2,DIM);
    }
    footer("A OPEN  B BACK");
}
void drawDetail() {
    if (infoSection == INFO_KEYS) {
        label(8,41,"KEY  PIN RAW STATE",2,DIM);
        for (int j=0;j<5;++j) {
            int i=keyBank*5+j;
            if (i >= KEY_COUNT) break;
            char row[32]; snprintf(row,sizeof(row),"%-6s %2u %d %s",keys[i].name,keys[i].pin,keys[i].raw?0:1,keys[i].down?"DOWN":"UP");
            label(8,66+j*28,row);
        }
        footer("U/D "+String(keyBank+1)+"/"+String(KEY_BANKS)+" B BACK");
    } else if (infoSection == INFO_DEVICE) {
        label(8,41,"DEVICE",2,DIM);
        label(8,74,"CPU       "+String(ESP.getCpuFreqMHz())+" MHZ");
        label(8,100,"PSRAM        "+String(ESP.getPsramSize()/1024/1024)+" MB");
        label(8,126,"HEAP       "+String(ESP.getFreeHeap()/1024)+" KB");
        label(8,152,sdOK?"TF          READY":"TF        NO CARD");
        label(8,178,clipped("SD TEST "+sdTest,19));
        footer("A TEST  B BACK");
    } else {
        String rows[6], hint;
        deviceInfoRows(infoSection,rows,hint);
        label(8,41,clipped(rows[0],12),2,DIM);
        label(152,41,"B BACK",2,DIM);
        for (int i=1;i<6;++i) label(8,74+(i-1)*26,clipped(rows[i],19));
        footer(hint);
    }
}
void draw() {
    if (!displayOK || screenSleeping) return;
    screen.fillScreen(BLACK);
    for (int i=0;i<4;++i) {
        if (page==i) screen.fillRect(i*60+2,4,56,22,GREEN);
        int width = strlen(tabs[i])*11-1;
        int x = i*60+(60-width)/2;
        screen.setTextColor(page==i?BLACK:GREEN);
        for (const char *p=tabs[i]; *p; ++p,x+=11) {
            screen.setTextSize(2); screen.setCursor(x,8); screen.write(*p);
        }
    }
    rule(29);
    if (page == STAT) drawStat();
    else if (page == FILES) drawFiles();
    else if (!detailOpen) drawMenu();
    else drawDetail();
    present();
}

void restoreScreen() {
    if (displayOK) panel.displayOn();
    screenSleeping = false;
    fullRefresh = true;
    draw();
    if (displayOK) digitalWrite(9, HIGH);
    dirty = true;
    lastInteraction = millis();
}

void requestScreenSleep(bool automatic) {
    if (screenSleeping) return;
    digitalWrite(9, LOW);
    screenSleeping = true;
    if (displayOK) panel.displayOff();
    Serial.printf("SCREEN SLEEP pending source=%s idle_ms=%lu\n",
        automatic ? "AUTO" : "APP", (unsigned long)(millis() - lastInteraction));
}

void sleepWhenReady() {
    if (!screenSleeping || !deviceInfoCanSleep()) return;
    // Enter only after every key has been released and debounced, so the key
    // that wakes the device cannot send it straight back to sleep.
    for (const auto &key : keys) {
        if (key.raw || key.down || millis()-key.changed < 30) return;
    }
    // Every button wakes the device; APP remains the one that puts it to sleep.
    esp_err_t error = ESP_OK;
    for (auto &key : keys) {
        error = gpio_wakeup_enable(static_cast<gpio_num_t>(key.pin), GPIO_INTR_LOW_LEVEL);
        if (error != ESP_OK) break;
    }
    if (error == ESP_OK) error = esp_sleep_enable_gpio_wakeup();
    if (error == ESP_OK && !suspendDeviceInfo()) {
        Serial.println("LIGHT SLEEP: radio shutdown failed");
        resumeDeviceInfo();
        error = ESP_FAIL;
    } else if (error == ESP_OK) {
        Serial.println("LIGHT SLEEP enter; APP GPIO10 wake; RTC time retained");
        Serial.flush();
        int64_t started = esp_timer_get_time();
        error = esp_light_sleep_start();
        Serial.printf("LIGHT SLEEP return=%s wake=%d elapsed=%lldms\n",
            esp_err_to_name(error), (int)esp_sleep_get_wakeup_cause(),
            (long long)((esp_timer_get_time()-started)/1000));
        resumeDeviceInfo();
    }
    for (auto &key : keys) gpio_wakeup_disable(static_cast<gpio_num_t>(key.pin));
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (error != ESP_OK) Serial.printf("LIGHT SLEEP failed: %s\n", esp_err_to_name(error));
    // Consume the wake press: a held APP key must not send us straight back to sleep.
    for (auto &key : keys) {
        key.raw = key.down = digitalRead(key.pin) == LOW;
        key.changed = millis();
    }
    restoreScreen();
}

void pressed(int i) {
    if (i == KEY_APP) {
        if (screenSleeping) restoreScreen();
        else requestScreenSleep(false);
        Serial.printf("SCREEN %s via APP GPIO10\n", screenSleeping ? "SLEEP PENDING" : "AWAKE");
        dirty = true;
        return;
    }
    if (screenSleeping) return;
    if(i==KEY_START) {
        toggleProvisioning(); page=RADIO; radioSelection=0; infoSection=INFO_WIFI; detailOpen=true;
        return;
    }
    // SELECT is the system/diagnostics key across every S3AI app.
    if(i==KEY_SELECT) {
        page=SYSTEM;
        detailOpen=false;
        return;
    }
    if(i==KEY_LEFT || i==KEY_RIGHT) {
        page=Page((page+(i==KEY_LEFT?3:1))%4);
        detailOpen=false;
        return;
    }
    if(page==FILES) {
        if(i==KEY_UP && selected>0) --selected;
        if(i==KEY_DOWN && selected+1<total) ++selected;
        if(i==KEY_A) {
            if(!sdOK) mountCard();
            else if(selectedName.length() && selectedDir) { cwd = (cwd=="/"?"/":cwd+"/")+selectedName; selected=0; }
        }
        if(i==KEY_B && cwd!="/") { int slash=cwd.lastIndexOf('/'); cwd=slash<=0?"/":cwd.substring(0,slash); selected=0; }
    }
    if(page==RADIO || page==SYSTEM) {
        if (!detailOpen) {
            int &selection = page==RADIO ? radioSelection : systemSelection;
            int count = page==RADIO ? 2 : 4;
            if (i==KEY_UP || i==KEY_DOWN) selection=(selection+(i==KEY_UP?count-1:1))%count;
            if (i==KEY_A) {
                infoSection=page==RADIO ? (selection==0?INFO_WIFI:INFO_BLE) : systemSections[selection];
                detailOpen=true;
            }
            if (i==KEY_B) page=STAT;
            return;
        }
        if (i==KEY_B) {
            if (!deviceInfoBack(infoSection)) detailOpen=false;
            return;
        }
        if (infoSection==INFO_KEYS) {
            if (i==KEY_UP || i==KEY_DOWN) keyBank=(keyBank+(i==KEY_UP?KEY_BANKS-1:1))%KEY_BANKS;
        } else if (infoSection==INFO_DEVICE && i==KEY_A) {
            if(!sdOK) mountCard(); else sdRoundtrip();
        } else {
            bool provisioning = deviceInfoProvisioning();
            deviceInfoAction(infoSection,i,sdOK);
            if (!provisioning && deviceInfoProvisioning()) {
                page=RADIO; radioSelection=0; infoSection=INFO_WIFI;
            }
        }
    }
}
void setup() {
    Serial.begin(115200);
    for(auto &key:keys) pinMode(key.pin,INPUT_PULLUP);
    pinMode(9,OUTPUT); digitalWrite(9,LOW);
    displayOK = bus.begin(40000000,SPI_MODE0) && panel.begin(GFX_SKIP_DATABUS_BEGIN);
    displayOK = displayOK && screen.begin();
    if (displayOK) {
        bus.beginWrite();
        bus.writeC8D8(ST7789_MADCTL, ST7789_MADCTL_MX | 0x08);
        bus.endWrite();
    }
    previousFrame = (uint16_t*)ps_malloc(240*240*2);
    if (previousFrame) memset(previousFrame,0xff,240*240*2);
    screen.setTextWrap(false); digitalWrite(9,HIGH);
    delay(400);
    Serial.println("PIPBOY v1.3 BOOT; no UART0; USB Serial/JTAG");
    const esp_partition_t *p=esp_ota_get_running_partition();
    Serial.printf("RUNNING label=%s offset=0x%08lx size=0x%08lx display=%s\n",p->label,(unsigned long)p->address,(unsigned long)p->size,displayOK?"INIT OK":"FAIL");
    mountCard(); beginDeviceInfo(sdOK);
    lastInteraction = millis();
    if (deviceInfoProvisioning()) { page=RADIO; infoSection=INFO_WIFI; detailOpen=true; }
    draw();
}
void loop() {
    uint32_t now=millis();
    updateDeviceInfo();
    for(int i=0;i<KEY_COUNT;++i) {
        auto &k=keys[i]; bool raw=digitalRead(k.pin)==LOW;
        if(raw!=k.raw) { k.raw=raw; k.changed=now; dirty=true; }
        if(k.down!=k.raw && now-k.changed>=30) {
            k.down=k.raw;
            lastInteraction=now;
            Serial.printf("KEY %s GPIO=%u raw=%d %s\n",k.name,k.pin,k.raw?0:1,k.down?"PRESSED":"RELEASED");
            if(k.down) pressed(i);
            now=millis(); dirty=true;
        }
        if (k.raw || k.down) lastInteraction=now;
    }
    now = millis();
    if (!deviceInfoCanSleep()) lastInteraction=now;
    if (!screenSleeping && now-lastInteraction >= AUTO_SLEEP_MS && deviceInfoCanSleep()) {
        requestScreenSleep(true);
    }
    sleepWhenReady();
    now = millis();
    if(now-lastLog>=1000) { lastLog=now; Serial.printf("PIPBOY alive uptime=%lus heap=%u page=%s SD=%s battery=%s\n",(unsigned long)(now/1000),ESP.getFreeHeap(),tabs[page],sdTest.c_str(),batterySummary().c_str()); if(page!=FILES) dirty=true; }
    if(!screenSleeping && page==STAT && now/WALK_FRAME_MS != lastAnimation/WALK_FRAME_MS) {
        lastAnimation = now;
        dirty = true;
    }
    if(!screenSleeping && dirty && now-lastFrame>=80) { dirty=false; lastFrame=now; draw(); }
    delay(3);
}
