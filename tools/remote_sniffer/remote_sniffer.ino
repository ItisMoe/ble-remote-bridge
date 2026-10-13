// =============================================================================
//  DIAGNOSTIC TOOL - NOT the bridge firmware. For testing and key capture only.
//  Part of https://github.com/ItisMoe/ble-remote-bridge
//
//  Use it to learn what a Bluetooth remote sends: it pairs with the remote and prints
//  its services, PnP ID, HID report map, and one line per button press (the key code
//  you can then use in the bridge's mapping table). Run it before adapting the bridge
//  to a remote other than the TCL RC902V. The bridge itself also prints the code of
//  every button you press (command "log on"), so for the RC902V you do not need this.
//
//  Works with the stock NimBLE-Arduino 2.5.x library as well as the patched one.
// =============================================================================

// Pre-test 1: Bluetooth remote sniffer   (version 3)
// Finds the TCL remote, pairs with it, and prints:
//  - every Bluetooth service on the remote, and whether Google's voice service is there
//  - the PnP ID (vendor/product) and the report map
//  - a line for every button you press
//
// Works on ESP32 and ESP32-C3. Forgets old pairings every time it starts.
// Board package: esp32 by Espressif, latest 3.x      Library: NimBLE-Arduino by h2zero, latest 2.x
// Serial Monitor: 115200 baud, "Newline". On a C3: Tools > USB CDC On Boot > Enabled.

#include <NimBLEDevice.h>

static const char* ATVV_SVC = "AB5E0001-5A21-4F05-BC7D-AF01F617B664";


static NimBLEAddress chosenAddr;
static bool chosen = false;
static volatile bool doConnect = false;
static NimBLEClient* client = nullptr;

struct HandleId { uint16_t handle; uint8_t id; };
static HandleId handleIds[8];
static size_t handleIdCount = 0;


void printHex(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) Serial.printf("%02X ", d[i]);
  Serial.println();
}

uint8_t idForHandle(uint16_t h) {
  for (size_t i = 0; i < handleIdCount; i++) if (handleIds[i].handle == h) return handleIds[i].id;
  return 0xFF;
}

void onReport(NimBLERemoteCharacteristic* chr, uint8_t* d, size_t n, bool isNotify) {
  bool release = true;
  for (size_t i = 0; i < n; i++) if (d[i]) release = false;
  if (release) { Serial.println("     (released)"); return; }
  Serial.printf("Button pressed: report %u, code ", idForHandle(chr->getHandle()));
  printHex(d, n > 8 ? 8 : n);
}

void onVoice(NimBLERemoteCharacteristic* chr, uint8_t* d, size_t n, bool isNotify) {
  Serial.printf("Voice data [handle %u, %u bytes]: ", chr->getHandle(), (unsigned)n);
  printHex(d, n > 16 ? 16 : n);
}

void startScan() { NimBLEDevice::getScan()->start(0, false, true); }

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* c) override { Serial.println("Connected"); }
  void onDisconnect(NimBLEClient* c, int reason) override {
    Serial.printf("Disconnected (reason %d) - press a button on the remote to reconnect\n", reason);
    startScan();
  }
} clientCallbacks;

// The TCL RC902V does not list the HID service (0x1812) in its advertisement, so a device is also
// accepted if its appearance is a remote/keyboard or its name looks like a TCL remote.
const char* whyRemote(const NimBLEAdvertisedDevice* d) {
  if (d->isAdvertisingService(NimBLEUUID((uint16_t)0x1812))) return "advertises the HID service";
  if (d->haveAppearance()) {
    uint16_t a = d->getAppearance();
    if (a >= 0x0180 && a <= 0x01BF) return "appearance = remote control";
    if (a >= 0x03C0 && a <= 0x03FF) return "appearance = HID device";
  }
  String name = d->getName().c_str();
  name.toUpperCase();
  if (name.indexOf("TCL") >= 0 || name.indexOf("RC8") >= 0 || name.indexOf("REMOTE") >= 0) return "name looks like a remote";
  return nullptr;
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (doConnect || (client && client->isConnected())) return;
    if (chosen) {   // after the first match: only reconnect to that remote
      if (dev->getAddress() == chosenAddr) { doConnect = true; NimBLEDevice::getScan()->stop(); }
      return;
    }
    const char* why = whyRemote(dev);
    if (!why) return;
    Serial.printf("Found %s  name='%s'  RSSI %d  (%s)\n", dev->getAddress().toString().c_str(),
                  dev->getName().c_str(), dev->getRSSI(), why);
    chosenAddr = dev->getAddress();
    chosen = true;
    doConnect = true;
    NimBLEDevice::getScan()->stop();
  }
} scanCallbacks;

uint8_t readReportId(NimBLERemoteCharacteristic* c) {
  NimBLERemoteDescriptor* ref = c->getDescriptor(NimBLEUUID((uint16_t)0x2908));
  if (!ref) return 0;
  NimBLEAttValue r = ref->readValue();
  return r.length() >= 1 ? r.data()[0] : 0;
}

bool connectToRemote() {
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&clientCallbacks, false);
  }
  Serial.printf("Connecting to %s ...\n", chosenAddr.toString().c_str());
  if (!client->connect(chosenAddr)) {
    Serial.println("Connect failed - keep the remote in pairing mode (hold OK + Home), retrying...");
    return false;
  }

  Serial.println("Securing link...");
  if (!client->secureConnection()) Serial.println("Pairing reported a problem (trying anyway)");

  Serial.println("SERVICES:");
  for (NimBLERemoteService* s : client->getServices(true)) Serial.printf("  %s\n", s->getUUID().toString().c_str());

  NimBLERemoteService* voice = client->getService(NimBLEUUID(ATVV_SVC));
  if (voice) {
    Serial.println("VOICE: Google voice service found");
    for (NimBLERemoteCharacteristic* c : voice->getCharacteristics(true)) {
      if (c->canNotify()) c->subscribe(true, onVoice);
    }
  } else {
    Serial.println("VOICE: no Google voice service");
  }

  NimBLERemoteService* dis = client->getService(NimBLEUUID((uint16_t)0x180A));
  if (dis) {
    NimBLERemoteCharacteristic* pnp = dis->getCharacteristic(NimBLEUUID((uint16_t)0x2A50));
    if (pnp) {
      NimBLEAttValue v = pnp->readValue();
      Serial.print("PNP ID: ");
      printHex(v.data(), v.length());
    }
  }

  NimBLERemoteService* hid = client->getService(NimBLEUUID((uint16_t)0x1812));
  if (!hid) {
    Serial.println("This device has no remote/keyboard service - it's probably not the remote.");
    Serial.println("Move that device away and press RESET to try again.");
    client->disconnect();
    return true;   // don't retry this device
  }

  NimBLERemoteCharacteristic* map = hid->getCharacteristic(NimBLEUUID((uint16_t)0x2A4B));
  if (map) {
    NimBLEAttValue v = map->readValue();
    Serial.printf("REPORT MAP (%u bytes):\n", (unsigned)v.length());
    printHex(v.data(), v.length());
  }

  handleIdCount = 0;
  for (NimBLERemoteCharacteristic* c : hid->getCharacteristics(true)) {
    if (c->getUUID() != NimBLEUUID((uint16_t)0x2A4D) || !c->canNotify()) continue;
    uint8_t id = readReportId(c);
    if (handleIdCount < 8) handleIds[handleIdCount++] = {c->getHandle(), id};
    if (c->subscribe(true, onReport)) Serial.printf("Input report id=%u (handle %u) subscribed\n", id, c->getHandle());
  }
  Serial.println("READY: press each button once, slowly.");
  return true;
}

void printInstructions() {
  Serial.println();
  Serial.println("REMOTE SNIFFER v3");
  Serial.println("Keep the Chromecast remote AWAY (other room, or batteries out) - it would be found too.");
  Serial.println("Hold the TCL remote next to the board and hold OK + Home for 3 seconds.");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);   // time for the USB serial port to open (ESP32-C3)

  NimBLEDevice::init("Remote Sniffer");
  NimBLEDevice::deleteAllBonds();   // a test: always start fresh, so it never sticks to an old remote
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setMTU(247);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(100);

  printInstructions();
  startScan();
}

void loop() {
  if (doConnect) {
    if (!connectToRemote()) { delay(500); startScan(); }
    doConnect = false;
  }
  delay(10);
}
