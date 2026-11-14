// ============================================================================
//  ESP32-BlueJammer - source-level reconstruction (official combo firmware)
// ----------------------------------------------------------------------------
//  Reconstructed by static reverse engineering of the closed-source binary
//  Firmware_Files/Official/Official-Combo-Channel_BT-BLE-WiFi-RC.ino.bin
//  published at github.com/EmenstaNougat/ESP32-BlueJammer.
//
//  Recovered build facts: ESP32 application image, arduino-esp32 core 2.0.10
//  (ESP-IDF v4.4.5), FQBN esp32:esp32:esp32, default 4MB partition table,
//  entry 0x40082f0c, segments DROM 0x3f400020 / DRAM 0x3ffbdb60 /
//  IROM 0x400d0020 / DRAM 0x3ffc04b0 / IRAM 0x40080000.
//
//  Recovered sketch layer (IROM): setup 0x400d294c, RadioTask 0x400d25a4,
//  loop 0x400d309c, state screens 0x400d2b24..0x400d2bfc, idle/status screen
//  0x400d2c70, BatStat 0x400d3544, OLED init 0x400d3630, bitmap renderer
//  0x400d3674, LED blink helper 0x400d2ab4, static ctors 0x400d2c44/0x400d37d4.
//  Evidence: analysis/ida/, analysis/ghidra/.
//
//  Scope: application firmware only. Bootloader, partition table and the
//  3D-print files are excluded by project scope. The FlipperZero ZeroWave image
//  is a separate ESP32-S3 build and is not covered here.
//
//  Jamming is illegal in most jurisdictions - educational reconstruction only.
// ============================================================================

#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <RF24.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_adc_cal.h>
#include <driver/adc.h>
#include "esp_bt.h"

#include "bitmaps.h"
#include "banners.h"

// ---------------------------------------------------------------------------
// Recovered hardware map (literal-pool pins from the sketch layer)
// ---------------------------------------------------------------------------
#define PIN_STATUS_LED   27
#define PIN_BOOT_BUTTON   0
#define PIN_I2C_SDA       4
#define PIN_I2C_SCL       5

#define HSPI_SCK         14
#define HSPI_MISO        12
#define HSPI_MOSI        13
#define HSPI_CSN         15
#define HSPI_CE          16

#define VSPI_SCK         18
#define VSPI_MISO        19
#define VSPI_MOSI        23
#define VSPI_CSN         21
#define VSPI_CE          22

#define RF24_SPI_HZ      16000000UL

#define OLED_WIDTH       128
#define OLED_HEIGHT       64
#define OLED_ADDR        0x3C

// ---------------------------------------------------------------------------
// Recovered application state
// ---------------------------------------------------------------------------
enum JamState : int {
  STATE_IDLE = 0,
  STATE_BT   = 1,
  STATE_BLE  = 2,
  STATE_WIFI = 3,
  STATE_RC   = 4,
};

// static ctors at 0x400d2c44: RF24(16,15,16MHz) @0x3ffc3b98,
// RF24(22,21,16MHz) @0x3ffc3b70 (declared VSPI object first in .bss order)
RF24 radioVspi(VSPI_CE, VSPI_CSN, RF24_SPI_HZ);
RF24 radioHspi(HSPI_CE, HSPI_CSN, RF24_SPI_HZ);

// static ctor at 0x400d37d4: Adafruit_SSD1306(128,64,&Wire,-1,400000)
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1, 400000, 100000);

TaskHandle_t radioTaskHandle = nullptr;

volatile bool radiosPaused       = false;  // 0x3ffc3b48 (1 = radios stopped)
volatile bool buttonConsumed     = false;  // 0x3ffc3b2c
volatile uint32_t pressStartMs   = 0;      // 0x3ffc3b44
volatile uint32_t lastClickMs    = 0;      // 0x3ffc3b40 (click backstop)
uint32_t lastBlinkMs             = 0;      // 0x3ffc3b34
uint32_t blinkCounter            = 0;      // 0x3ffc3b30
uint8_t  ledLevel                = 1;      // 0x3ffbdb6c (.data init = 1)
volatile int currentState        = STATE_BT;    // 0x3ffbdb7c (.data init = 1)
volatile int savedState          = STATE_BT;    // 0x3ffbdb68
volatile int screenCachedState   = -1;     // 0x3ffc3b3c (screen cache)
uint8_t  hspiChannel             = 45;     // 0x3ffbdb74 (.data init 0x2D)
uint8_t  vspiChannel             = 45;     // 0x3ffbdb70 (.data init 0x2D)

// battery/ADC globals (recovered float storage in .bss 0x3ffc3bc0..0x3ffc3bd4)
float  batteryInstant   = 0.0f;
float  batteryVoltage   = 0.0f;
float  batteryPercentF  = 0.0f;
float  batteryOffset    = 0.0f;   // 0x3ffc3bc0 (never written by the firmware)
float  batteryLastRaw   = 0.0f;
int    batteryPercent   = 0;
int    batterySource    = 0;
int    batteryPrevSource = 1;   // init 1 (USB) per the original
uint8_t batteryStableCnt = 0;
uint32_t batteryLastUpdateMs = 0;
esp_adc_cal_characteristics_t adcChars;

// forward declarations (kept flat like the original single .ino)
void updateStatusLed();
void drawScreen(const char *bmpName, bool splash);
void oledInit();
void batStatInit();
void stateIdle();
void stateBt();
void stateBle();
void stateWifi();
void stateRc();
static void radioTask(void *arg);

// ---------------------------------------------------------------------------
// Debounced boot button (the recovered object at 0x3ffc3b4c; its constructor
// sets the pin up and the poll helper runs each loop() iteration)
// ---------------------------------------------------------------------------
class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin) : pin_(pin), debounceMs_(50) {
    pinMode(pin_, INPUT_PULLUP);
  }
  void setDebounce(uint32_t ms) { debounceMs_ = ms; }
  void update() {
    bool level = (digitalRead(pin_) == LOW);
    uint32_t now = millis();
    if (level != lastLevel_) {
      lastLevel_ = level;
      changedAt_ = now;
    }
    if ((now - changedAt_) >= debounceMs_ && level != stable_) {
      stable_ = level;
      changed_ = true;
    }
  }
  bool pressed() const { return stable_; }
  bool released() const { return !stable_; }
  bool changed() {
    bool c = changed_;
    changed_ = false;
    return c;
  }

 private:
  uint8_t pin_;
  uint32_t debounceMs_;
  uint32_t changedAt_ = 0;
  bool lastLevel_ = false;
  bool stable_ = false;
  bool changed_ = false;
};

DebouncedButton bootButton(PIN_BOOT_BUTTON);

// ---------------------------------------------------------------------------
// OLED (recovered: init 0x400d3630, renderer 0x400d3674)
// ---------------------------------------------------------------------------
void oledInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 0);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true)) {
    Serial.println(TEXT_OLED_FAIL);
    while (true) {
    }
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void drawScreen(const char *bmpName, bool splash) {
  display.clearDisplay();
  if (splash) {
    if (strcmp(bmpName, "bitmapboot") == 0) {
      display.drawBitmap(0, 0, bitmapboot, 128, 64, SSD1306_WHITE);
    }
  } else {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    String footer = TEXT_BY_EMENSTA;
    display.setCursor((128 - (int)footer.length() * 6) / 2, 2);
    display.print(footer);
    if (strcmp(bmpName, "bitmap1") == 0) {
      display.drawBitmap(0, 12, bitmap1, 128, 50, SSD1306_WHITE);
    } else if (strcmp(bmpName, "bitmap2") == 0) {
      display.drawBitmap(0, 12, bitmap2, 128, 50, SSD1306_WHITE);
    } else if (strcmp(bmpName, "bitmap3") == 0) {
      display.drawBitmap(0, 12, bitmap3, 128, 50, SSD1306_WHITE);
    } else if (strcmp(bmpName, "bitmap4") == 0) {
      display.drawBitmap(0, 12, bitmap4, 128, 50, SSD1306_WHITE);
    }
  }
  display.display();
}

// ---------------------------------------------------------------------------
// Battery instrumentation (recovered: BatStat 0x400d3544, sampling helper
// 0x400d3208, update 0x400d3254, percentage curve 0x400d33bc)
// ---------------------------------------------------------------------------
int readAdcOversampled(uint8_t channel, uint8_t samples) {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < samples; i++) {
    acc += adc1_get_raw((adc1_channel_t)channel);
  }
  return (int)(acc / samples);
}

// Recovered 0x400d3208: raw average -> calibrated volts (esp_adc_cal, 1/1000).
float adcVolts(uint8_t channel) {
  uint32_t raw = (uint32_t)readAdcOversampled(channel, 16);
  return esp_adc_cal_raw_to_voltage(raw, &adcChars) / 1000.0f;
}

// Recovered piecewise curve (constants from the IROM literal pool, 0x400d33bc)
int batteryPercentFromVoltage(float v) {
  if (v >= 4.15f) return 100;
  if (v >= 4.10f) return (int)(90.0f + (v - 4.10f) * 200.0f);
  if (v >= 4.00f) return (int)(80.0f + (v - 4.00f) * 100.0f);
  if (v >= 3.90f) return (int)(60.0f + (v - 3.90f) * 200.0f);
  if (v >= 3.80f) return (int)(40.0f + (v - 3.80f) * 200.0f);
  if (v >= 3.70f) return (int)(20.0f + (v - 3.70f) * 200.0f);
  if (v >= 3.60f) return (int)(10.0f + (v - 3.60f) * 100.0f);
  if (v >= 3.50f) return (int)(5.0f  + (v - 3.50f) * 50.0f);
  if (v >= 3.40f) return (int)(1.0f  + (v - 3.40f) * 40.0f);
  if (v >= 3.30f) return (int)((v - 3.30f) * 10.0f);
  return 0;
}

// Recovered 0x400d3254: sample both ADC channels, corrected divider 1.564,
// EMA .85/.15, clamp 2.5..4.4 V, and TP4056 charge-source detection with
// 1.8/1.65 V and 0.25/0.4 V hysteresis plus a 6-sample confirmation.
void batterySample() {
  float v6 = adcVolts(6);   // battery divider
  float v5 = adcVolts(5);   // TP4056 status

  float vbat = batteryOffset + v6 * 1.564f;
  batteryLastRaw = v6;
  if (vbat < 2.5f) vbat = 2.5f;
  if (vbat > 4.4f) vbat = 4.4f;
  batteryVoltage = (batteryVoltage * 0.85f) + (vbat * 0.15f);

  int det;
  if (v5 >= 1.8f) {
    det = 2;                       // charging
  } else if (v5 >= 1.65f) {
    det = (batterySource == 2) ? 2 : 1;
  } else if (v5 >= 0.4f) {
    det = 1;                       // USB
  } else if (v5 >= 0.25f) {
    det = (batterySource == 1) ? 1 : 0;
  } else {
    det = 0;                       // battery
  }
  if (det == batterySource) {
    batteryStableCnt = 0;
  } else if (++batteryStableCnt >= 6) {
    batterySource = det;
    batteryStableCnt = 0;
  }
}

// Recovered 0x400d3390: the voltage/percent/source getters re-sample the ADC
// at most every ~20 ms, so the idle screen tracks USB/charger changes live.
void batteryPoll() {
  uint32_t now = millis();
  if ((now - batteryLastUpdateMs) > 19) {
    batteryLastUpdateMs = now;
    batterySample();
  }
}

void batStatInit() {
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(34, ADC_11db);
  analogSetPinAttenuation(35, ADC_11db);
  esp_adc_cal_value_t vref = esp_adc_cal_characterize(
      ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChars);

  Serial.print(TEXT_BAT_ADC_CAL);
  if (vref == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    Serial.println(TEXT_BAT_EFUSE_VREF);
  } else if (vref == ESP_ADC_CAL_VAL_EFUSE_TP) {
    Serial.println(TEXT_BAT_EFUSE_TP);
  } else {
    Serial.println(TEXT_BAT_DEFAULT_VREF);
  }
  Serial.printf(FMT_BAT_DIVIDER, 1.564f);
  Serial.printf("\n");
  Serial.printf(FMT_BAT_REFF, 100000.0f * 500000.0f / (100000.0f + 500000.0f));
  Serial.printf("\n");

  batterySource = 1;   // init USB per the original
  batteryPrevSource = 1;
  batteryVoltage = 0.0f;
  batteryInstant = 0.0f;
  for (int i = 0; i < 8; i++) {
    batterySample();
    delay(10);
  }
  batteryPercent = batteryPercentFromVoltage(batteryVoltage);
  const char *src = (batterySource == 2)   ? TEXT_BAT_CHARGING
                    : (batterySource == 1) ? TEXT_USB
                                           : TEXT_BAT_BATTERY;
  Serial.printf(FMT_BAT_INITIAL, batteryVoltage, batteryPercent, src);
  Serial.printf("\n");
}

// ---------------------------------------------------------------------------
// Status LED blink (recovered: helper at 0x400d2ab4). Blinks savedState times,
// 150 ms apart, then a 1 s pause.
// ---------------------------------------------------------------------------
void updateStatusLed() {
  uint32_t now = millis();
  uint32_t interval = 150;
  if (ledLevel != 0 && blinkCounter == (uint32_t)savedState) {
    interval = 1000;
  }
  if ((now - lastBlinkMs) < interval) {
    return;
  }
  lastBlinkMs = now;
  ledLevel ^= 1;
  digitalWrite(PIN_STATUS_LED, ledLevel ? HIGH : LOW);
  if (ledLevel) {
    blinkCounter++;
    if (blinkCounter > (uint32_t)savedState) {
      blinkCounter = 1;
    }
  }
}

// ---------------------------------------------------------------------------
// State screens (recovered: 0x400d2b24 / 0x400d2b6c / 0x400d2bb4 / 0x400d2bfc)
// Each redraws once on entry, then runs the blink helper.
// ---------------------------------------------------------------------------
void stateBt() {
  if (screenCachedState != STATE_BT) {
    Serial.println(TEXT_STATE1);
    screenCachedState = STATE_BT;
    savedState = STATE_BT;
    drawScreen("bitmap1", false);
    lastBlinkMs = millis();
    blinkCounter = 0;
    ledLevel = 0;
    digitalWrite(PIN_STATUS_LED, LOW);
  }
  updateStatusLed();
}

void stateBle() {
  if (screenCachedState != STATE_BLE) {
    Serial.println(TEXT_STATE2);
    screenCachedState = STATE_BLE;
    savedState = STATE_BLE;
    drawScreen("bitmap2", false);
    lastBlinkMs = millis();
    blinkCounter = 0;
    ledLevel = 0;
    digitalWrite(PIN_STATUS_LED, LOW);
  }
  updateStatusLed();
}

void stateWifi() {
  if (screenCachedState != STATE_WIFI) {
    Serial.println(TEXT_STATE3);
    screenCachedState = STATE_WIFI;
    savedState = STATE_WIFI;
    drawScreen("bitmap3", false);
    lastBlinkMs = millis();
    blinkCounter = 0;
    ledLevel = 0;
    digitalWrite(PIN_STATUS_LED, LOW);
  }
  updateStatusLed();
}

void stateRc() {
  if (screenCachedState != STATE_RC) {
    Serial.println(TEXT_STATE4);
    screenCachedState = STATE_RC;
    savedState = STATE_RC;
    drawScreen("bitmap4", false);
    lastBlinkMs = millis();
    blinkCounter = 0;
    ledLevel = 0;
    digitalWrite(PIN_STATUS_LED, LOW);
  }
  updateStatusLed();
}

// Idle/status screen (recovered: 0x400d2c70): radio chip status, charging
// source icon and battery/voltage readout.
void stateIdle() {
  if (screenCachedState != STATE_IDLE) {
    Serial.println(TEXT_STATE0);
    screenCachedState = STATE_IDLE;
  }
  batteryPoll();
  batteryPercent = batteryPercentFromVoltage(batteryVoltage);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(TEXT_R1);
  display.print(radioHspi.isChipConnected() ? "OK" : "FAIL");
  display.setCursor(0, 8);
  display.print(TEXT_R2);
  display.print(radioVspi.isChipConnected() ? "OK" : "FAIL");

  if (batterySource == 1) {
    // USB present: "USB" label + plug icon + animated "Connected..."
    display.setCursor(55, 26);
    display.print(TEXT_USB);
    display.drawRect(60, 10, 8, 12, SSD1306_WHITE);
    display.fillRect(61, 11, 6, 3, SSD1306_WHITE);
    display.fillRect(56, 16, 16, 2, SSD1306_WHITE);
    display.fillRect(54, 20, 4, 2, SSD1306_WHITE);
    display.fillRect(62, 20, 4, 2, SSD1306_WHITE);
    display.fillRect(70, 20, 4, 2, SSD1306_WHITE);
    String text = TEXT_CONNECTED;
    for (int i = 0; i < (batteryPercent / 10); i++) {
      text += TEXT_DOT;
    }
    display.setCursor((128 - (int)text.length() * 6) / 2, 38);
    display.print(text);
  } else if (batterySource == 2) {
    // charging: battery outline with fill bars + charger hints
    display.drawRect(44, 10, 40, 18, SSD1306_WHITE);
    display.fillRect(84, 15, 3, 8, SSD1306_WHITE);
    int bars = batteryPercent / 8;
    for (int i = 0; i <= bars; i++) {
      display.fillRect(i * 9 + 46, 12, 8, 14, SSD1306_WHITE);
    }
    display.setCursor(5, 35);
    display.print(TEXT_CHARGING);
    display.setCursor(5, 45);
    display.print(TEXT_CHECK_LED);
    display.setCursor(5, 55);
    display.print(TEXT_BLUE_FULL);
  } else {
    // on battery: battery outline + percentage + voltage
    display.drawRect(44, 10, 40, 18, SSD1306_WHITE);
    display.fillRect(84, 15, 3, 8, SSD1306_WHITE);
    int bars = (batteryPercent + 24) / 25;
    for (int i = 0; i < bars; i++) {
      display.fillRect(i * 9 + 46, 12, 8, 14, SSD1306_WHITE);
    }
    String pct = String(batteryPercent) + TEXT_PERCENT;
    display.setCursor((128 - (int)pct.length() * 6) / 2, 35);
    display.print(pct);
    String volt = String(TEXT_VOLTAGE) + String(batteryVoltage, 2) + "V";
    display.setCursor((128 - (int)volt.length() * 6) / 2, 48);
    display.print(volt);
  }
  display.display();
}

// ---------------------------------------------------------------------------
// Radio worker task (recovered: RadioTask 0x400d25a4)
//  - end()/re-create both SPI buses on the documented pins
//  - begin() both RF24 modules, print the per-module success/failure banners
//  - configure: autoAck off, listening off, retries 0, PA max + LNA,
//    2 Mbps, CRC disabled, continuous carrier at the current channel
//  - 100 channel hops per outer loop; carriers gated by `carriersOn`
// ---------------------------------------------------------------------------
static void radioTask(void *arg) {
  (void)arg;

  SPI.end();
  delay(2);

  SPIClass *hspi = new SPIClass(2);  // HSPI
  hspi->begin(HSPI_SCK, HSPI_MISO, HSPI_MOSI, HSPI_CSN);
  delay(5);

  SPIClass *vspi = new SPIClass(3);  // VSPI
  vspi->begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, VSPI_CSN);
  delay(5);

  bool hspiOk = radioHspi.begin(hspi);
  if (hspiOk) {
    Serial.println();
    Serial.println(BANNER_TILDES);
    Serial.println();
    for (int i = 0; i < 3; i++) {
      Serial.println(ART_HSPI_OK[i]);
    }
    Serial.println();
    Serial.println(TEXT_HSPI_OK);
  } else {
    Serial.println();
    Serial.println(BANNER_TILDES);
    Serial.println();
    for (int i = 0; i < 3; i++) {
      Serial.println(ART_HSPI_FAIL[i]);
    }
    Serial.println();
    Serial.println(TEXT_HSPI_FAIL);
  }
  if (hspiOk) {
    radioHspi.setAutoAck(false);
    radioHspi.stopListening();
    radioHspi.setRetries(0, 0);
    radioHspi.setPALevel(3, true);   // RF24_PA_MAX (+LNA)
    radioHspi.setDataRate((rf24_datarate_e)1);   // RF24_2MBPS        // RF24_2MBPS
    radioHspi.setCRCLength((rf24_crclength_e)0); // RF24_CRC_DISABLED       // RF24_CRC_DISABLED
    radioHspi.startConstCarrier((rf24_pa_dbm_e)3, hspiChannel);
  }

  bool vspiOk = radioVspi.begin(vspi);
  if (vspiOk) {
    Serial.println();
    Serial.println(BANNER_TILDES);
    Serial.println();
    for (int i = 0; i < 3; i++) {
      Serial.println(ART_VSPI_OK[i]);
    }
    Serial.println();
    Serial.println(TEXT_VSPI_OK);
  } else {
    Serial.println();
    Serial.println(BANNER_TILDES);
    Serial.println();
    for (int i = 0; i < 3; i++) {
      Serial.println(ART_VSPI_FAIL[i]);
    }
    Serial.println();
    Serial.println(TEXT_VSPI_FAIL);
  }
  if (vspiOk) {
    radioVspi.setAutoAck(false);
    radioVspi.stopListening();
    radioVspi.setRetries(0, 0);
    radioVspi.setPALevel(3, true);
    radioVspi.setDataRate((rf24_datarate_e)1);   // RF24_2MBPS
    radioVspi.setCRCLength((rf24_crclength_e)0); // RF24_CRC_DISABLED
    radioVspi.startConstCarrier((rf24_pa_dbm_e)3, vspiChannel);
  }

  for (;;) {
    for (int i = 0; i < 100; i++) {
      if (radiosPaused) {
        radioHspi.stopConstCarrier();
        radioVspi.stopConstCarrier();
        radioHspi.powerDown();
        radioVspi.powerDown();
        while (radiosPaused) {
          vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        radioHspi.powerUp();
        radioVspi.powerUp();
        radioHspi.startConstCarrier((rf24_pa_dbm_e)3, hspiChannel);
        radioVspi.startConstCarrier((rf24_pa_dbm_e)3, vspiChannel);
      }
      switch (currentState) {
        case STATE_BT: {
          radioHspi.setChannel((uint8_t)random(79));
          radioVspi.setChannel((uint8_t)random(79));
          break;
        }
        case STATE_BLE: {
          radioHspi.setChannel((uint8_t)random(39));
          radioVspi.setChannel((uint8_t)random(39));
          break;
        }
        case STATE_WIFI: {
          radioHspi.setChannel((uint8_t)random(14));
          radioVspi.setChannel((uint8_t)random(14));
          break;
        }
        case STATE_RC: {
          radioHspi.setChannel((uint8_t)random(125));
          radioVspi.setChannel((uint8_t)random(125));
          break;
        }
        default:
          break;
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// ---------------------------------------------------------------------------
// setup() (recovered: 0x400d294c)
// ---------------------------------------------------------------------------
void setup() {
  WiFi.mode(WIFI_OFF);
  btStop();

  Serial.begin(115200);
  Serial.setTimeout(50);

  pinMode(PIN_STATUS_LED, OUTPUT);
  oledInit();
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  drawScreen("bitmapboot", true);
  delay(2000);

  Serial.println(BANNER_RULE);
  Serial.println();
  for (int i = 0; i < 5; i++) {
    Serial.println(ART_BOOT[i]);
  }
  Serial.println();
  Serial.println(BANNER_RULE);
  Serial.println();
  Serial.println(BANNER_STATE_HEADER);
  Serial.println();
  for (int i = 0; i < 6; i++) {
    Serial.println(ART_BY_EMENSTA[i]);
  }
  Serial.println();
  Serial.println(BANNER_RULE);
  Serial.println();
  Serial.println(BANNER_EDU);
  Serial.println();
  Serial.println(BANNER_URL);
  Serial.println();
  Serial.println(BANNER_DISCLAIMER);
  Serial.println();
  Serial.println(BANNER_RULE);

  batStatInit();
  Serial.println();
  Serial.println(TEXT_INIT_VOLTAGE);
  Serial.println();

  xTaskCreatePinnedToCore(radioTask, TEXT_RADIO_TASK, 0x2000, NULL, 1,
                          &radioTaskHandle, 0);
}

// ---------------------------------------------------------------------------
// loop() (recovered: 0x400d309c) - debounced boot button toggles between the
// saved jam mode and the idle/status screen, then dispatches to the screens.
// ---------------------------------------------------------------------------
void loop() {
  bootButton.update();

  // Recovered button semantics (loop 0x400d309c):
  //  * hold the boot button >500 ms -> pause the radios, show the idle screen
  //  * a later click resumes the previous jam mode
  //  * a click while jamming cycles the mode 1 -> 2 -> 3 -> 4 -> 1
  bool btnDown = (digitalRead(PIN_BOOT_BUTTON) == LOW);
  bool resumed = false;

  if (btnDown) {
    if (pressStartMs == 0) {
      pressStartMs = millis();
    } else if (!radiosPaused && !buttonConsumed &&
               (millis() - pressStartMs) > 499) {
      savedState = currentState;   // backup at 0x3ffbdb78
      currentState = STATE_IDLE;
      radiosPaused = true;
      buttonConsumed = true;
      screenCachedState = -1;
    }
  } else if (pressStartMs != 0) {
    if ((millis() - pressStartMs) < 500 && radiosPaused) {
      currentState = savedState;   // resume previous jam mode
      radiosPaused = false;
      resumed = true;
      screenCachedState = -1;
    }
    pressStartMs = 0;
    buttonConsumed = false;
  }

  if (!radiosPaused && !resumed && bootButton.changed() && bootButton.pressed()) {
    currentState = (currentState % 4) + 1;   // 0x400d2a90: mode = (mode % 4) + 1
    savedState = currentState;
    screenCachedState = -1;
  }

  switch (currentState) {
    case STATE_BT:   stateBt();   break;
    case STATE_BLE:  stateBle();  break;
    case STATE_WIFI: stateWifi(); break;
    case STATE_RC:   stateRc();   break;
    default:         stateIdle(); break;
  }
}
