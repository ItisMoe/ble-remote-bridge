// =============================================================================
//  DIAGNOSTIC TEST - NOT the bridge firmware. For testing only.
//  Part of https://github.com/ItisMoe/ble-remote-bridge
//
//  Proves the TV/Chromecast half of the bridge on its own, without any remote: the
//  ESP32 shows up as a Bluetooth keyboard named "Relay Test", can be paired with a
//  TV and a Chromecast at the same time, and presses RIGHT on every connected device
//  every 3 seconds. The bridge firmware's TV/Chromecast side was built to behave like
//  this sketch (same setup calls, same name, same IDs, same keyboard report).
//  Needs only the stock NimBLE-Arduino 2.x library.
// =============================================================================

// Pre-test 3: can the ESP32 be a Bluetooth keyboard for the TV AND the Chromecast at once?
// When connected, it presses Right every 3 seconds on every connected device.
//
// Board package: esp32 by Espressif, latest 3.x   (Tools > Board > ESP32 Dev Module)
// Library:       NimBLE-Arduino by h2zero, latest 2.x
//
// Erase saved pairings: while it is running, hold BOOT for 3 seconds.

#include <NimBLEDevice.h>

#if CONFIG_IDF_TARGET_ESP32C3
const int ERASE_BUTTON_PIN = 9;   // BOOT button on ESP32-C3 boards
#else
const int ERASE_BUTTON_PIN = 0;   // BOOT button on classic ESP32 boards
#endif
#include <NimBLEHIDDevice.h>

// Standard keyboard, report ID 1
static const uint8_t reportMap[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             // modifiers
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,             // reserved
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, // 6 keys
  0xC0
};

NimBLEServer* server;
NimBLEHIDDevice* hid;
NimBLECharacteristic* input;
const int MAX_HOSTS = 2;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    Serial.printf("Host connected: %s (%d connected)\n",
                  info.getAddress().toString().c_str(), s->getConnectedCount());
    if (s->getConnectedCount() < MAX_HOSTS) {   // let the 2nd host in
      bool ok = NimBLEDevice::startAdvertising();
      Serial.printf("Advertising again for the second device: %s\n", ok ? "OK" : "FAILED");
    }
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    Serial.printf("Host disconnected (reason %d)\n", reason);
    NimBLEDevice::startAdvertising();
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    Serial.printf("Paired/encrypted with %s\n", info.getIdAddress().toString().c_str());
  }
} serverCallbacks;

void setup() {
  Serial.begin(115200);
  delay(1500);   // time for the USB serial port to open (ESP32-C3)
  pinMode(ERASE_BUTTON_PIN, INPUT_PULLUP);

  NimBLEDevice::init("Relay Test");
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);

  hid = new NimBLEHIDDevice(server);
  input = hid->getInputReport(1);
  hid->setManufacturer("DIY");
  hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->setName("Relay Test");
  adv->start();

  Serial.println("Pair 'Relay Test' on the TV, then on the Chromecast (Remotes & Accessories).");
}

// Hold BOOT for 3 seconds while running: forget all paired devices.
uint32_t bootDownSince = 0;
void checkEraseButton() {
  if (digitalRead(ERASE_BUTTON_PIN) != LOW) { bootDownSince = 0; return; }
  if (bootDownSince == 0) { bootDownSince = millis() | 1; Serial.println("Keep holding BOOT to erase pairings..."); return; }
  if (millis() - bootDownSince < 3000) return;
  NimBLEDevice::deleteAllBonds();
  Serial.println("ERASED: all pairings. Restarting...");
  delay(800);
  ESP.restart();
}

// Every second: if fewer than 2 devices are connected, make sure we are still visible for pairing.
uint32_t lastAdvCheck = 0;
bool lastVisible = false;
void keepAdvertising() {
  if (millis() - lastAdvCheck < 1000) return;
  lastAdvCheck = millis();
  int n = server->getConnectedCount();
  bool visible = NimBLEDevice::getAdvertising()->isAdvertising();
  if (n < MAX_HOSTS && !visible) {
    visible = NimBLEDevice::startAdvertising();
    Serial.printf("Advertising had stopped - restarted: %s\n", visible ? "OK" : "FAILED");
  }
  if (visible != lastVisible) {
    Serial.printf(">> 'Relay Test' is %s (%d device(s) connected)\n", visible ? "VISIBLE for pairing" : "NOT visible", n);
    lastVisible = visible;
  }
}

void loop() {
  checkEraseButton();
  keepAdvertising();
  if (server->getConnectedCount() > 0) {
    uint8_t press[8]   = {0, 0, 0x4F, 0, 0, 0, 0, 0};   // 0x4F = Right arrow
    uint8_t release[8] = {0};
    input->notify(press, sizeof(press));                 // sends to every connected host
    delay(60);
    input->notify(release, sizeof(release));
    Serial.printf("Sent RIGHT to %d host(s)\n", server->getConnectedCount());
  }
  for (int i = 0; i < 300; i++) { checkEraseButton(); keepAdvertising(); delay(10); }   // wait 3 s, watching BOOT
}
