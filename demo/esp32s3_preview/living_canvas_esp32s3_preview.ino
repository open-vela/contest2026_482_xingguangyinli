#include <Adafruit_XCA9554.h>
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrv.hpp>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <esp_system.h>
#include <mbedtls/md.h>

#include <cstring>

#include "HWCDC.h"
#include "board_pins.h"
#include "src/living_canvas_cat_ui.h"
#include "src/living_canvas_input_policy.h"
#include "src/living_canvas_local_link.h"
#include "src/living_canvas_render_policy.h"

#if __has_include("generated/living_canvas_private_config.h")
#include "generated/living_canvas_private_config.h"
#define LC_HAS_PRIVATE_CONFIG 1
#else
#define LC_HAS_PRIVATE_CONFIG 0
#define LC_CAT_DEVICE_ID ""
#define LC_WIFI_SSID ""
#define LC_WIFI_PASSWORD ""
#define LC_LOCAL_S1_HOST ""
#define LC_LOCAL_S1_PORT 0
#define LC_LOCAL_PAIRING_KEY_HEX ""
#endif

// Keep older ignored preview headers buildable, but never treat them as a
// configured local link unless every new field is explicitly present.
#ifndef LC_LOCAL_S1_HOST
#define LC_LOCAL_S1_HOST ""
#endif
#ifndef LC_LOCAL_S1_PORT
#define LC_LOCAL_S1_PORT 0
#endif
#ifndef LC_LOCAL_PAIRING_KEY_HEX
#define LC_LOCAL_PAIRING_KEY_HEX ""
#endif

HWCDC USBSerial;

Arduino_DataBus *displayBus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *display = new Arduino_CO5300(
    displayBus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);
Adafruit_XCA9554 ioExpander;
TouchDrvCST816 touch;
WiFiUDP localUdp;
LivingCanvasLocalLink *localLink = nullptr;
LivingCanvasCatUi catUi(LC_CAT_DEVICE_ID);

bool touchReady = false;
bool touchWasDown = false;
bool buttonWasDown = false;
bool localUdpReady = false;
uint32_t lastCatRenderSignature = 0xFFFFFFFFu;

constexpr uint16_t COLOR_INK = 0x2945;
constexpr uint16_t COLOR_CREAM = 0xFF3A;
constexpr uint16_t COLOR_ORANGE = 0xFBA6;
constexpr uint16_t COLOR_GREEN = 0x4D8A;
constexpr uint16_t COLOR_WHITE = 0xFFFF;

static bool checkinConfigured();

static void drawCatFace(
    uint16_t accent, LivingCanvasCatExpression expression) {
  display->fillTriangle(104, 132, 136, 86, 160, 142, accent);
  display->fillTriangle(208, 142, 232, 86, 264, 132, accent);
  display->fillRoundRect(104, 118, 160, 126, 40, accent);

  if (expression == LivingCanvasCatExpression::Blink) {
    display->drawLine(142, 178, 166, 178, COLOR_INK);
    display->drawLine(202, 178, 226, 178, COLOR_INK);
  } else if (expression == LivingCanvasCatExpression::Happy) {
    display->drawLine(142, 184, 154, 174, COLOR_INK);
    display->drawLine(154, 174, 166, 184, COLOR_INK);
    display->drawLine(202, 184, 214, 174, COLOR_INK);
    display->drawLine(214, 174, 226, 184, COLOR_INK);
  } else {
    const int16_t pupilOffset =
        expression == LivingCanvasCatExpression::Working ? 3 : 0;
    display->fillCircle(154 + pupilOffset, 178, 10, COLOR_INK);
    display->fillCircle(214 + pupilOffset, 178, 10, COLOR_INK);
    display->fillCircle(151 + pupilOffset, 174, 3, COLOR_WHITE);
    display->fillCircle(211 + pupilOffset, 174, 3, COLOR_WHITE);
    if (expression == LivingCanvasCatExpression::Concerned) {
      display->drawLine(140, 156, 166, 162, COLOR_INK);
      display->drawLine(202, 162, 228, 156, COLOR_INK);
    }
  }

  display->fillTriangle(176, 194, 192, 194, 184, 207, COLOR_ORANGE);
  if (expression == LivingCanvasCatExpression::Concerned) {
    display->drawLine(184, 207, 174, 218, COLOR_INK);
    display->drawLine(174, 218, 194, 218, COLOR_INK);
    display->drawLine(194, 218, 184, 207, COLOR_INK);
  } else {
    display->drawLine(184, 207, 164, 218, COLOR_INK);
    display->drawLine(184, 207, 204, 218, COLOR_INK);
  }
}

static uint32_t catRenderSignature(
    const LivingCanvasCatRenderPlan &plan, LivingCanvasCatUiState state,
    bool configured) {
  return static_cast<uint32_t>(state) |
         (static_cast<uint32_t>(plan.expression) << 4) |
         (static_cast<uint32_t>(plan.pulseStep) << 8) |
         (static_cast<uint32_t>(plan.showAction) << 12) |
         (static_cast<uint32_t>(plan.networkReady) << 13) |
         (static_cast<uint32_t>(configured) << 14);
}

static void drawCatCompanionScreen(uint32_t nowMs, bool force) {
  const bool configured = checkinConfigured();
  const bool networkReady =
      WiFi.status() == WL_CONNECTED && localUdpReady;
  const LivingCanvasCatUiState state = catUi.state();
  const LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      state, configured, networkReady, nowMs);
  const uint32_t signature = catRenderSignature(plan, state, configured);
  if (!force && signature == lastCatRenderSignature) return;
  lastCatRenderSignature = signature;

  const uint16_t accent = catUi.accent565();
  display->fillScreen(COLOR_CREAM);
  display->fillRect(0, 0, LCD_WIDTH, 72, accent);
  display->setTextColor(COLOR_WHITE);
  display->setTextSize(2);
  display->setCursor(28, 25);
  display->print(catUi.label());
  display->setTextSize(1);
  display->setCursor(220, 30);
  display->print("FRAME COMPANION");
  if (plan.networkReady) {
    display->fillCircle(338, 58, 6, COLOR_GREEN);
  } else {
    display->drawCircle(338, 58, 6, COLOR_WHITE);
  }

  for (uint8_t step = 0; step <= plan.pulseStep; ++step) {
    display->drawCircle(184, 176, 112 + step * 8, accent);
  }
  drawCatFace(accent, plan.expression);

  display->fillRoundRect(20, 274, 328, 102, 20, COLOR_WHITE);
  display->drawRoundRect(20, 274, 328, 102, 20, accent);
  display->setTextColor(COLOR_INK);
  display->setTextSize(2);
  display->setCursor(44, 304);
  display->print(plan.headline);
  display->setTextSize(1);
  display->setCursor(52, 348);
  display->print(plan.detail);

  if (plan.showAction) {
    display->fillRoundRect(36, 392, 296, 42, 14, accent);
    display->setTextColor(COLOR_WHITE);
    display->setTextSize(2);
    display->setCursor(98, 405);
    display->print("TAP ANYWHERE");
  } else {
    display->setTextColor(accent);
    display->setTextSize(1);
    display->setCursor(94, 414);
    display->print("LIVING CANVAS LOCAL LINK");
  }

  USBSerial.printf("[CAT_UI] state=%u network=%u sequence=%lu\n",
                   static_cast<unsigned>(state),
                   static_cast<unsigned>(networkReady),
                   static_cast<unsigned long>(catUi.pendingSequence()));
}

static bool checkinConfigured() {
  return LC_HAS_PRIVATE_CONFIG && LC_CAT_DEVICE_ID[0] != '\0' &&
         LC_WIFI_SSID[0] != '\0' && LC_WIFI_PASSWORD[0] != '\0' &&
         LC_LOCAL_S1_HOST[0] != '\0' && LC_LOCAL_S1_PORT >= 1024 &&
         std::strlen(LC_LOCAL_PAIRING_KEY_HEX) ==
             LIVING_CANVAS_LOCAL_KEY_BYTES * 2;
}

static int lowerHexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

static bool decodePairingKey(
    const char *hex,
    uint8_t output[LIVING_CANVAS_LOCAL_KEY_BYTES]) {
  if (hex == nullptr || output == nullptr ||
      std::strlen(hex) != LIVING_CANVAS_LOCAL_KEY_BYTES * 2) {
    return false;
  }
  for (std::size_t index = 0; index < LIVING_CANVAS_LOCAL_KEY_BYTES;
       ++index) {
    const int high = lowerHexValue(hex[index * 2]);
    const int low = lowerHexValue(hex[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

static bool localHmac(
    void *, const uint8_t key[LIVING_CANVAS_LOCAL_KEY_BYTES],
    const uint8_t *message, std::size_t messageLength,
    uint8_t output[LIVING_CANVAS_LOCAL_TAG_BYTES]) {
  const mbedtls_md_info_t *sha256 =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return sha256 != nullptr &&
         mbedtls_md_hmac(sha256, key, LIVING_CANVAS_LOCAL_KEY_BYTES,
                         message, messageLength, output) == 0;
}

static void startCheckinNetwork() {
  if (!checkinConfigured()) return;
  uint8_t pairingKey[LIVING_CANVAS_LOCAL_KEY_BYTES];
  if (!decodePairingKey(LC_LOCAL_PAIRING_KEY_HEX, pairingKey)) {
    USBSerial.println("[CHECKIN] private config rejected");
    return;
  }

  char sessionId[LIVING_CANVAS_LOCAL_SESSION_MAX];
  std::snprintf(sessionId, sizeof(sessionId), "boot_%08lx%08lx",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()));
  localLink = new LivingCanvasLocalLink(LC_CAT_DEVICE_ID, sessionId,
                                        pairingKey, localHmac, nullptr);
  std::memset(pairingKey, 0, sizeof(pairingKey));
  if (localLink == nullptr || !localLink->ready()) {
    localLink = nullptr;
    USBSerial.println("[CHECKIN] local link unavailable");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(LC_WIFI_SSID, LC_WIFI_PASSWORD);
  USBSerial.println("[CHECKIN] authenticated local link started");
}

static void receiveLocalAcks(uint32_t nowMs) {
  if (localLink == nullptr || !localUdpReady) return;
  for (int packetSize = localUdp.parsePacket(); packetSize > 0;
       packetSize = localUdp.parsePacket()) {
    uint8_t packet[LIVING_CANVAS_LOCAL_PACKET_MAX];
    if (packetSize >= static_cast<int>(sizeof(packet))) {
      while (localUdp.available() > 0) {
        uint8_t discarded[32];
        localUdp.read(discarded, sizeof(discarded));
      }
      continue;
    }
    const int received = localUdp.read(packet, packetSize);
    if (received != packetSize ||
        !localLink->acceptAck(packet, static_cast<std::size_t>(received),
                              nowMs)) {
      continue;
    }
    catUi.onMatchingAck(localLink->sequence(), nowMs);
    drawCatCompanionScreen(nowMs, true);
    USBSerial.println("[CHECKIN] signed local acknowledgement verified");
  }
}

static void deliverPendingCheckin(uint32_t nowMs) {
  if (localLink == nullptr) return;

  if (WiFi.status() == WL_CONNECTED && !localUdpReady) {
    localUdpReady = localUdp.begin(0) != 0;
    if (localUdpReady) {
      USBSerial.println("[CHECKIN] local datagram socket ready");
    }
  }
  receiveLocalAcks(nowMs);

  const LivingCanvasLocalLinkState beforeUpdate = localLink->state();
  localLink->update(nowMs);
  if (beforeUpdate != LivingCanvasLocalLinkState::Retry &&
      localLink->state() == LivingCanvasLocalLinkState::Retry) {
    catUi.onDeliveryFailed(localLink->sequence(), nowMs);
    drawCatCompanionScreen(nowMs, true);
    USBSerial.println("[CHECKIN] local acknowledgement timeout");
  }

  if (!localLink->shouldSend(nowMs)) return;
  char request[LIVING_CANVAS_LOCAL_PACKET_MAX];
  const bool formatted = localLink->formatRequest(request, sizeof(request));
  if (formatted && WiFi.status() == WL_CONNECTED && localUdpReady &&
      localUdp.beginPacket(LC_LOCAL_S1_HOST,
                           static_cast<uint16_t>(LC_LOCAL_S1_PORT)) == 1) {
    localUdp.write(reinterpret_cast<const uint8_t *>(request),
                   std::strlen(request));
    localUdp.endPacket();
  }
  localLink->markSent(nowMs);
}

static void handleTap(int16_t x, int16_t y, uint32_t nowMs) {
  if (localLink != nullptr) {
    const LivingCanvasCatUiState uiState = catUi.state();
    bool started = false;
    if (uiState == LivingCanvasCatUiState::Idle) {
      started = localLink->beginCheckin(nowMs);
    } else if (uiState == LivingCanvasCatUiState::Retry) {
      started = localLink->retry(nowMs);
    }
    if (!started ||
        !catUi.onExplicitTouch(localLink->sequence(), nowMs)) return;
    drawCatCompanionScreen(nowMs, true);
    return;
  }
  (void)x;
  (void)y;
  constexpr uint32_t kOfflinePreviewSequence = 1;
  if (!catUi.onExplicitTouch(kOfflinePreviewSequence, nowMs)) return;
  catUi.onDeliveryFailed(kOfflinePreviewSequence, nowMs);
  drawCatCompanionScreen(nowMs, true);
  USBSerial.println("[CHECKIN] offline UI preview; no event was delivered");
}

static bool initBoardPower() {
  Wire.begin(IIC_SDA, IIC_SCL);
  if (!ioExpander.begin(0x20)) return false;
  const uint8_t enabledPins[] = {0, 1, 2, 6};
  for (uint8_t pin : enabledPins) ioExpander.pinMode(pin, OUTPUT);
  for (uint8_t pin : enabledPins) ioExpander.digitalWrite(pin, LOW);
  delay(20);
  for (uint8_t pin : enabledPins) ioExpander.digitalWrite(pin, HIGH);
  return true;
}

void setup() {
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);
  delay(200);
  USBSerial.println("[BOOT] Living Canvas ESP32-S3 preview");

  pinMode(0, INPUT_PULLUP);
  if (!initBoardPower()) {
    USBSerial.println("[BOOT] fatal: display power IO unavailable");
    while (true) delay(1000);
  }
  USBSerial.println("[BOOT] display power ok");

  display->begin();
  display->setBrightness(205);
  display->fillScreen(0x20C4);
  display->setTextColor(0xFFFF);
  display->setTextSize(2);
  display->setCursor(55, 200);
  display->println("Living Canvas");

  touchReady = touch.begin(Wire, CST816_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (touchReady) {
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    USBSerial.println("[BOOT] CST820 touch ok");
  } else if (checkinConfigured()) {
    USBSerial.println(
        "[BOOT] CST820 touch unavailable; touch check-in disabled");
  } else {
    USBSerial.println("[BOOT] CST820 touch unavailable; BOOT button enabled");
  }

  startCheckinNetwork();
  drawCatCompanionScreen(millis(), true);
  USBSerial.println("[BOOT] independent cat companion ready");
}

void loop() {
  uint32_t nowMs = millis();
  deliverPendingCheckin(nowMs);
  catUi.update(nowMs);
  drawCatCompanionScreen(nowMs, false);

  bool touchDown = false;
  int16_t touchX = 0;
  int16_t touchY = 0;
  if (touchReady) {
    int16_t x[1];
    int16_t y[1];
    touchDown = touch.getPoint(x, y, 1) > 0;
    if (touchDown) {
      touchX = x[0];
      touchY = y[0];
    }
  }
  bool buttonDown = digitalRead(0) == LOW;

  const LivingCanvasInputAction inputAction = livingCanvasInputAction(
      localLink != nullptr, touchDown && !touchWasDown,
      buttonDown && !buttonWasDown);
  if (inputAction == LivingCanvasInputAction::Touch) {
    USBSerial.printf("[TOUCH] x=%d y=%d\n", touchX, touchY);
    handleTap(touchX, touchY, nowMs);
  }
  if (inputAction == LivingCanvasInputAction::PreviewButton) {
    handleTap(180, 220, nowMs);
  }
  touchWasDown = touchDown;
  buttonWasDown = buttonDown;
  delay(12);
}
