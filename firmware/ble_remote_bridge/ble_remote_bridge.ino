/*
  =============================================================================
   BLE Remote Bridge  v0.9.0
   Bluetooth remote  ->  ESP32-C3  ->  Android / Google TV devices (TV + Chromecast)
  =============================================================================
  Project: https://github.com/ItisMoe/ble-remote-bridge   (README, diagrams, docs)

  The ESP32-C3 is paired to a Bluetooth LE remote (to the remote it looks like the TV)
  and at the same time it is a Bluetooth LE remote paired to a TV and to a Chromecast.
  Every button press is looked up in a mapping table and sent to the TV, the
  Chromecast, both, or neither. Built for the TCL RC902V remote; see the README for
  using another remote.

  This folder must contain build_opt.h (next to this file). It raises the Bluetooth
  library's storage to 8 pairings and 32 subscriptions (the defaults, 3 and 8, are
  exactly full with a remote + TV + Chromecast).

  Built and checked with:
    Board package : esp32 by Espressif Systems 3.3.12  (Tools > Board > ESP32C3 Dev Module)
    Library       : NimBLE-Arduino 2.5.1 WITH the patch in library-patch/ (a one-time fix:
                    the stock 2.5.1 writes past a table when the radio hands out connection
                    number 4, which crashed the bridge while pairing the Chromecast).
                    The sketch refuses to compile with the stock library.
    Tools > USB CDC On Boot > Enabled.    Serial Monitor: 115200 baud, "Newline".

  -----------------------------------------------------------------------------
  FIRST-TIME SETUP
  -----------------------------------------------------------------------------
   1. TV and Chromecast: Settings > Remotes & Accessories: remove "Relay Test" if
      listed. On the TV also remove the TCL remote, so the TV doesn't try to
      reconnect to it.
   2. Flash this sketch. On its very first start it erases any pairings left on
      the ESP32 by earlier sketches.
   3. Remote: the bridge starts in remote pairing mode by itself. Hold the remote
      next to the board and put it in pairing mode (TCL RC902V: hold OK + Home for
      3 seconds). Wait for "REMOTE PAIRED".
   4. Type   pair tv   then on the TV: Remotes & Accessories > Pair accessory >
      "Relay Test". Wait for "TV PAIRED".
   5. Type   pair cc   then do the same on the Chromecast. Wait for
      "CHROMECAST PAIRED".
      ("pair tv"/"pair cc" pause the remote link while pairing, so the radio is in the
      same state as in the two-host test. Without them, a new device takes the free
      slot, TV first; "swap" exchanges TV and Chromecast if needed.)
   6. Type   status   : remote, TV and Chromecast must all show as connected.

  -----------------------------------------------------------------------------
  CONSOLE COMMANDS  (codes are hex; every button press is printed with its code)
  -----------------------------------------------------------------------------
   help                     this list
   status                   connections, pairing mode, advertising
   map                      show the mapping table
   set <key> <tv> <cc>      change one button. <tv> and <cc> are a code,
                            "same" (= the button's own code) or "-" (nothing):
                              set 0x0221 0x0223 -    mic button = Home, TV only
                              set 0x0059 0x0224 -    that TCL key = Back, TV only
                              set 0x0055 - 0x00CD    that TCL key = Play/Pause,
                                                     Chromecast only
   del <key>                remove a line (that button then goes to the TV unchanged)
   defaults                 restore the default table (DEFAULT_MAP below)
   send <tv|cc> <code>      send one test press + release to one device
   pair remote|tv|cc        pair a device            pair stop   cancels it
   forget remote|tv|cc      delete one pairing
   swap                     exchange which paired device is the TV and which the Chromecast
   log on|off               print every button press (default: on)
   ids tcl|neutral          Vendor/Product IDs the bridge reports (saved; see CLONE_RC902V_IDS)
   inputkey <code>|off      the remote's Input button: opens the TV input menu and starts a
                            "TV session": arrows, OK and Back go to the TV only, until OK is
                            pressed or no arrow/OK/Back is pressed for <tvsession> seconds
   switchkey <code>|off     a button that toggles "TV mode" (arrows, OK, Back -> TV only) and
                            back; TV mode ends by itself after <tvmode> seconds without presses
   tvsession <seconds>      TV session timeout (default 5)
   tvmode <seconds>         TV mode auto-return (default 60, 0 = never)
   factory yes              erase ALL pairings and settings, then restart
  Changes apply immediately and are saved in flash.
  Hold BOOT for 5 seconds: erase all pairings (the mapping is kept) and restart.

  License: MIT (see LICENSE).
*/

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <stdarg.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "nimble/nimble/host/include/host/ble_store.h"
#include "nimble/porting/nimble/include/os/os_mbuf.h"

#if !__has_include(<NimBLE_BridgeFix.h>)
#error "Install the patched NimBLE-Arduino 2.5.1 - see library-patch/README.md in the repository."
#endif

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Use the esp32 board package 3.x (this sketch was built with 3.3.12)."
#endif
#if NIMBLE_CPP_VERSION_MAJOR != 2 || NIMBLE_CPP_VERSION_MINOR < 5
#error "Use NimBLE-Arduino 2.5.x (this sketch was built with 2.5.1)."
#endif
static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= 3, "Needs 3 BLE connections: remote + TV + Chromecast.");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_BONDS) >= 8 && MYNEWT_VAL(BLE_STORE_MAX_CCCDS) >= 32,
              "build_opt.h is missing: put the build_opt.h file in the same folder as this .ino");

// ============================================================================
// Settings you may want to change
// ============================================================================
static const char* DEVICE_NAME = "Relay Test";  // shown on the TV and the Chromecast (the name used throughout development)

// Factory default for the IDs the bridge reports (change at any time with "ids tcl|neutral"):
// 0 = the IDs of the two-host test sketch (tools/two_host_test), which the TV paired with (default).
// 1 = the RC902V's own Vendor/Product IDs (experimental; not used in the verified setup).
#define CLONE_RC902V_IDS 0

// 0 = report descriptor = the two-host test's keyboard report (ID 1) + the RC902V's consumer report (ID 3).
//     Needed for Home, Back, volume, mute, channel and TCL keys. (default)
// 1 = report descriptor = ONLY the two-host test's keyboard report, byte for byte. Diagnostic: proves
//     whether the TV pairs with the exact descriptor. Only arrows and OK can be sent then
//     (as arrow keys and Enter); Back, Home, volume etc. are impossible with that descriptor.
#define KEYBOARD_ONLY_LIKE_TEST 0

// ============================================================================
// DEFAULT MAPPING.  key = code the RC902V sends.  tv / cc = code sent to that
// device, 0 = send nothing to that device.  All codes come from a capture of the RC902V (tools/remote_sniffer).
// Buttons that are not in the table are sent to the TV unchanged.
// Edits made with the "set" command are saved in flash and override this table;
// the "defaults" command brings this table back.
// ============================================================================
struct MapEntry {
  uint16_t key;
  uint16_t tv;
  uint16_t cc;
};

static const MapEntry DEFAULT_MAP[] = {
  //  key     TV      Chromecast
  {0x0042, 0x0042, 0x0042},  // Up     -> TV + Chromecast
  {0x0043, 0x0043, 0x0043},  // Down   -> TV + Chromecast
  {0x0044, 0x0044, 0x0044},  // Left   -> TV + Chromecast
  {0x0045, 0x0045, 0x0045},  // Right  -> TV + Chromecast
  {0x0041, 0x0041, 0x0041},  // OK     -> TV + Chromecast
  {0x0224, 0x0000, 0x0224},  // Back   -> Chromecast only (TV stays on the HDMI input)
  {0x0223, 0x0000, 0x0223},  // Home   -> Chromecast only (TV stays on the HDMI input)
  {0x00E2, 0x00E2, 0x0000},  // Mute   -> TV
  {0x00E9, 0x00E9, 0x0000},  // Vol+   -> TV
  {0x00EA, 0x00EA, 0x0000},  // Vol-   -> TV
  {0x009C, 0x009C, 0x0000},  // Ch+    -> TV
  {0x009D, 0x009D, 0x0000},  // Ch-    -> TV
  {0x0221, 0x0221, 0x0000},  // Assistant/mic -> TV (the voice audio itself can't pass through)
  // TCL-specific buttons -> TV.  #n = position in the capture order (top-left to bottom-right)
  {0x0059, 0x0059, 0x0000},  // #2
  {0x0055, 0x0055, 0x0000},  // #4
  {0x005B, 0x005B, 0x0000},  // #12
  {0x0049, 0x0049, 0x0000},  // #13
  {0x0050, 0x0050, 0x0000},  // #16
  {0x003D, 0x003D, 0x0000},  // #19
  {0x0052, 0x0052, 0x0000},  // #20
  {0x003A, 0x003A, 0x0000},  // #21
  {0x0051, 0x0051, 0x0000},  // #22
  {0x005F, 0x005F, 0x0000},  // #23
  {0x003F, 0x003F, 0x0000},  // #24
  {0x0058, 0x0058, 0x0000},  // #25
  {0x0057, 0x0057, 0x0000},  // #26
  {0x0053, 0x0053, 0x0000},  // #27
};

// ============================================================================
// What the RC902V is (from a capture with tools/remote_sniffer). Change these to use another remote.
// ============================================================================
// The RC902V's report map, byte for byte (used to check the remote when pairing it):
// Consumer Control, report ID 3, one 16-bit code 0x0000-0x029C.
static const uint8_t RC902V_REPORT_MAP[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x19,
                                           0x00, 0x2A, 0x9C, 0x02, 0x15, 0x00, 0x26, 0x9C, 0x02,
                                           0x95, 0x01, 0x75, 0x10, 0x81, 0x00, 0xC0};

// What the bridge shows to the TV and the Chromecast:
//  - report ID 1: the keyboard report of the two-host test, byte for byte
//  - report ID 3: the RC902V's consumer report, byte for byte (carries Home, Back,
//    volume, mute, channel and the TCL keys, which a keyboard report can't)
static const uint8_t BRIDGE_REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,                    // keyboard, ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,        //
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,                                // modifiers
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                                // reserved
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,                    //
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,                    // 6 keys
    0xC0,                                                              //
#if !KEYBOARD_ONLY_LIKE_TEST
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x19, 0x00, 0x2A,  // consumer, ID 3
    0x9C, 0x02, 0x15, 0x00, 0x26, 0x9C, 0x02, 0x95, 0x01, 0x75, 0x10,  //
    0x81, 0x00, 0xC0
#endif
};
static const uint8_t HID_LAYOUT_VERSION = KEYBOARD_ONLY_LIKE_TEST ? 4 : 3;  // re-pair needed when it changes
static const uint8_t REPORT_ID = 3;
static const uint16_t MAX_CODE = 0x029C;  // highest code this report map can carry
static const uint8_t TV_REPORT_LEN = 6;   // the RC902V sends 6 bytes: the TV gets exactly that
static const uint8_t CC_REPORT_LEN = 2;   // exactly the size the report map declares
// PnP ID: source 0x01 (Bluetooth SIG), vendor 0x0417, product 0x0304, version 0x0003.
static const uint8_t RC902V_PNP[7] = {0x01, 0x17, 0x04, 0x04, 0x03, 0x03, 0x00};
static const char* ATVV_SERVICE = "AB5E0001-5A21-4F05-BC7D-AF01F617B664";  // Google voice service

#if CONFIG_IDF_TARGET_ESP32C3
static const int BOOT_BUTTON_PIN = 9;  // BOOT button on ESP32-C3 boards
#else
static const int BOOT_BUTTON_PIN = 0;
#endif

static const uint16_t NO_CONN = BLE_HS_CONN_HANDLE_NONE;
static const uint8_t MAX_MAP = 64;
static const uint32_t REMOTE_PAIR_MS = 120000;
static const uint32_t HOST_PAIR_MS = 180000;

// ============================================================================
// Types
// ============================================================================
struct Addr {  // a Bluetooth address as NimBLE stores it (val[0] = lowest byte)
  bool set;
  uint8_t type;
  uint8_t val[6];
};

enum : uint8_t { ROLE_NONE = 0, ROLE_TV = 1, ROLE_CC = 2 };

struct Host {  // one TV / Chromecast connection
  bool used;
  uint16_t conn;
  uint8_t role;
  bool enc;
  bool sub;
  uint32_t since;      // when it connected
  uint32_t lastKick;   // last time an unidentified connection was asked to leave
  uint32_t nextNudge;  // next test-style key report while it hasn't paired yet
  int32_t encStatus;   // last pairing/encryption result for this link (-1 = none yet)
};

struct Held {  // the button currently held down, and where its press went
  bool active;
  uint16_t key;
  uint16_t tvConn;
  uint16_t ccConn;
};

// Events: the Bluetooth callbacks only copy data into a queue. All decisions,
// sending and printing happen in loop(), in order.
enum : uint8_t {
  EV_REPORT,     // button report from the remote
  EV_HOST_CONN,  // TV/Chromecast side
  EV_HOST_DISC,
  EV_HOST_AUTH,
  EV_HOST_SUB,
  EV_REM_CONN,  // remote side
  EV_REM_FAIL,
  EV_REM_DISC,
  EV_REM_SETUP,
  EV_SCAN_HIT,
  EV_SCAN_NOTE,  // a remote-looking device with an invalid all-zero address was skipped
  EV_DIAG        // a TV/Chromecast-side Bluetooth step (MTU, pairing result, subscription)
};

struct Event {
  uint8_t type;
  uint8_t len;
  uint8_t flag;
  uint8_t addrType;
  uint16_t conn;
  uint16_t handle;
  int32_t reason;
  uint8_t addr[6];
  uint8_t data[8];
  int8_t rssi;
  char name[20];
};

enum : uint8_t { SETUP_RECONNECT = 1, SETUP_PAIR = 2 };
enum : uint8_t { SF_OK = 0, SF_NOT_CONNECTED, SF_SECURITY, SF_NO_HID, SF_NO_INPUT };

struct SetupRequest {
  uint8_t mode;
  uint32_t session;
};

struct SetupResult {
  uint32_t session;  // which connection this result belongs to
  uint8_t mode;
  uint8_t fail;
  uint8_t nHandles;
  uint16_t handles[4];
  int8_t pnpMatch;  // 1 = same as RC902V, 0 = different, -1 = not read
  int8_t mapMatch;
  bool notBonded;
  bool subscribeFailed;
  uint8_t idType;
  uint8_t idVal[6];
};

enum RemState : uint8_t { R_NONE, R_IDLE, R_CONNECTING, R_SETUP, R_READY };
enum PairMode : uint8_t { PAIR_NONE, PAIR_REMOTE, PAIR_TV, PAIR_CC };
enum : uint8_t { PS_PREP, PS_SCAN, PS_CONNECT, PS_SETUP, PS_RESCAN };

// ============================================================================
// Globals
// ============================================================================
static Preferences g_prefs;
static QueueHandle_t g_evQ = nullptr;
static QueueHandle_t g_setupQ = nullptr;
static std::atomic<uint32_t> g_evDropped{0};
static std::atomic<bool> g_storeOverflow{false};
static std::atomic<bool> g_setupBusy{false};
static std::atomic<bool> g_scanArmed{false};
static std::atomic<bool> g_nullNoted{false};

static NimBLEServer* g_server = nullptr;
static NimBLEHIDDevice* g_hid = nullptr;
static NimBLECharacteristic* g_inputChr = nullptr;
static NimBLECharacteristic* g_kbChr = nullptr;  // keyboard report (ID 1), as in the two-host test
static NimBLEClient* g_client = nullptr;

static MapEntry g_map[MAX_MAP];
static uint8_t g_mapCount = 0;

static Addr g_remote{};    // address the remote is connected at
static Addr g_remoteId{};  // the remote's identity address (what its pairing is saved under)
static Addr g_tv{};        // TV identity address
static Addr g_cc{};        // Chromecast identity address
static Addr g_candidate{};

static Host g_hosts[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];
static Held g_held{false, 0, NO_CONN, NO_CONN};

// Input report handles of the remote. Read by the Bluetooth task, written by loop().
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t g_inHandles[4];
static uint8_t g_inCount = 0;

// Addresses to skip while looking for a remote (devices that turned out not to be one).
static uint8_t g_ignore[4][7];
static uint8_t g_ignoreCount = 0;

static SetupResult g_setupResult{};

static RemState g_rem = R_NONE;
static uint32_t g_remSince = 0;
static uint32_t g_remRetryAt = 0;
static uint8_t g_remSecFails = 0;
static uint32_t g_remSession = 0;  // bumped for every remote setup; stale results are ignored

static PairMode g_pair = PAIR_NONE;
static uint32_t g_pairSince = 0;
static uint8_t g_pairStage = PS_PREP;
static uint32_t g_lastScanStart = 0;
static uint32_t g_bootMs = 0;
static bool g_initCancelSent = false;
static bool g_pairCancelSent = false;
static bool g_pairDiscSent = false;

static bool g_logKeys = true;

// Input button / switch button (codes 0 = not set). Saved in flash.
static uint16_t g_inputKey = 0;
static uint16_t g_switchKey = 0;
static uint32_t g_tvSessionMs = 5000;
static uint32_t g_tvModeMs = 60000;
static bool g_tvSession = false;     // after Input: arrows, OK, Back -> TV only
static bool g_tvMode = false;        // switch button: arrows, OK, Back -> TV only
static uint32_t g_navLastMs = 0;     // last arrow/OK/Back press during a TV session
static uint32_t g_anyKeyLastMs = 0;  // last button press of any kind
static bool g_tclIds = (CLONE_RC902V_IDS != 0);
static bool g_advFailLogged = false;
static uint32_t g_lastHousekeeping = 0;
static uint32_t g_bootDownSince = 0;
static uint32_t g_lastDroppedShown = 0;
static char g_line[96];
static uint8_t g_lineLen = 0;

// ============================================================================
// Small helpers
// ============================================================================
static void out(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
  Serial.write((const uint8_t*)buf, (size_t)n);
}

static void postEvent(const Event& e, TickType_t wait = 0) {
  if (g_evQ == nullptr || xQueueSend(g_evQ, &e, wait) != pdTRUE) g_evDropped++;
}

static bool timeReached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

static bool addrEq(const Addr& a, const uint8_t* val, uint8_t type) {
  return a.set && ((a.type & 1) == (type & 1)) && memcmp(a.val, val, 6) == 0;
}

static NimBLEAddress toNim(const Addr& a) {
  ble_addr_t b;
  b.type = a.type;
  memcpy(b.val, a.val, 6);
  return NimBLEAddress(b);
}

static void fromRaw(Addr& a, const uint8_t* val, uint8_t type) {
  a.set = true;
  a.type = type;
  memcpy(a.val, val, 6);
}

static const char* addrText(const Addr& a, char* buf, size_t n) {
  if (!a.set) {
    snprintf(buf, n, "-");
  } else {
    snprintf(buf, n, "%02x:%02x:%02x:%02x:%02x:%02x", a.val[5], a.val[4], a.val[3], a.val[2], a.val[1],
             a.val[0]);
  }
  return buf;
}

static bool isInputHandle(uint16_t h) {
  bool found = false;
  portENTER_CRITICAL(&g_mux);
  for (uint8_t i = 0; i < g_inCount; i++) {
    if (g_inHandles[i] == h) {
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&g_mux);
  return found;
}

static void setInputHandles(const uint16_t* h, uint8_t n) {
  if (n > 4) n = 4;
  portENTER_CRITICAL(&g_mux);
  for (uint8_t i = 0; i < n; i++) g_inHandles[i] = h[i];
  g_inCount = n;
  portEXIT_CRITICAL(&g_mux);
}

static uint8_t getInputHandles(uint16_t* h) {
  portENTER_CRITICAL(&g_mux);
  uint8_t n = g_inCount;
  for (uint8_t i = 0; i < n; i++) h[i] = g_inHandles[i];
  portEXIT_CRITICAL(&g_mux);
  return n;
}

static bool isIgnored(const uint8_t* val, uint8_t type) {
  bool found = false;
  portENTER_CRITICAL(&g_mux);
  for (uint8_t i = 0; i < g_ignoreCount; i++) {
    if (g_ignore[i][0] == (type & 1) && memcmp(&g_ignore[i][1], val, 6) == 0) {
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&g_mux);
  return found;
}

static void addIgnored(const Addr& a) {
  if (!a.set || isIgnored(a.val, a.type)) return;
  portENTER_CRITICAL(&g_mux);
  uint8_t i = (g_ignoreCount < 4) ? g_ignoreCount++ : 3;
  g_ignore[i][0] = a.type & 1;
  memcpy(&g_ignore[i][1], a.val, 6);
  portEXIT_CRITICAL(&g_mux);
}

static const char* codeName(uint16_t c) {  // standard codes (checked against Linux + Android key tables)
  switch (c) {
    case 0x0030: return "Power";
    case 0x0040: return "Menu";
    case 0x0041: return "OK";
    case 0x0042: return "Up";
    case 0x0043: return "Down";
    case 0x0044: return "Left";
    case 0x0045: return "Right";
    case 0x008D: return "Guide";
    case 0x009C: return "Ch+";
    case 0x009D: return "Ch-";
    case 0x00B0: return "Play";
    case 0x00B3: return "Fast-forward";
    case 0x00B4: return "Rewind";
    case 0x00B5: return "Next";
    case 0x00B6: return "Previous";
    case 0x00B7: return "Stop";
    case 0x00CD: return "Play/Pause";
    case 0x00E2: return "Mute";
    case 0x00E9: return "Vol+";
    case 0x00EA: return "Vol-";
    case 0x0221: return "Assistant/Search";
    case 0x0223: return "Home";
    case 0x0224: return "Back";
    default: return nullptr;
  }
}

static const char* keyLabel(uint16_t c, char* buf, size_t n) {
  static const uint16_t TCL[][2] = {{0x0059, 2},  {0x0055, 4},  {0x005B, 12}, {0x0049, 13}, {0x0050, 16},
                                    {0x003D, 19}, {0x0052, 20}, {0x003A, 21}, {0x0051, 22}, {0x005F, 23},
                                    {0x003F, 24}, {0x0058, 25}, {0x0057, 26}, {0x0053, 27}};
  const char* s = codeName(c);
  if (s) {
    snprintf(buf, n, "%s", s);
    return buf;
  }
  for (const auto& k : TCL) {
    if (k[0] == c) {
      snprintf(buf, n, "TCL key #%u", (unsigned)k[1]);
      return buf;
    }
  }
  snprintf(buf, n, "%s", "");
  return buf;
}

// ============================================================================
// Settings in flash
// ============================================================================
static void saveAddr(const char* key, const Addr& a) {
  if (!a.set) {
    if (g_prefs.isKey(key)) g_prefs.remove(key);
    return;
  }
  uint8_t b[7];
  b[0] = a.type;
  memcpy(b + 1, a.val, 6);
  g_prefs.putBytes(key, b, sizeof(b));
}

static void loadAddr(const char* key, Addr& a) {
  a.set = false;
  if (!g_prefs.isKey(key)) return;
  uint8_t b[7];
  if (g_prefs.getBytesLength(key) == sizeof(b) && g_prefs.getBytes(key, b, sizeof(b)) == sizeof(b)) {
    a.set = true;
    a.type = b[0];
    memcpy(a.val, b + 1, 6);
  }
}

static void saveHandles() {
  uint16_t h[4];
  uint8_t n = getInputHandles(h);
  if (n == 0) {
    if (g_prefs.isKey("rh")) g_prefs.remove("rh");
    return;
  }
  uint8_t b[8];
  for (uint8_t i = 0; i < n; i++) {
    b[i * 2] = h[i] & 0xFF;
    b[i * 2 + 1] = h[i] >> 8;
  }
  g_prefs.putBytes("rh", b, n * 2);
}

static void loadHandles() {
  uint16_t h[4];
  uint8_t n = 0;
  size_t len = g_prefs.isKey("rh") ? g_prefs.getBytesLength("rh") : 0;
  if (len >= 2 && len <= 8 && (len % 2) == 0) {
    uint8_t b[8];
    if (g_prefs.getBytes("rh", b, len) == len) {
      n = len / 2;
      for (uint8_t i = 0; i < n; i++) h[i] = b[i * 2] | (b[i * 2 + 1] << 8);
    }
  }
  setInputHandles(h, n);
}

static void loadDefaultMap() {
  g_mapCount = 0;
  for (const auto& m : DEFAULT_MAP) {
    if (g_mapCount < MAX_MAP) g_map[g_mapCount++] = m;
  }
}

static void saveMap() {
  uint8_t b[4 + MAX_MAP * 6];
  b[0] = 'M';
  b[1] = 1;  // format version
  b[2] = g_mapCount;
  b[3] = 0;
  for (uint8_t i = 0; i < g_mapCount; i++) {
    uint8_t* p = b + 4 + i * 6;
    p[0] = g_map[i].key & 0xFF;
    p[1] = g_map[i].key >> 8;
    p[2] = g_map[i].tv & 0xFF;
    p[3] = g_map[i].tv >> 8;
    p[4] = g_map[i].cc & 0xFF;
    p[5] = g_map[i].cc >> 8;
  }
  g_prefs.putBytes("map", b, 4 + g_mapCount * 6);
}

static void loadMap() {
  uint8_t b[4 + MAX_MAP * 6];
  size_t len = g_prefs.isKey("map") ? g_prefs.getBytesLength("map") : 0;
  if (len >= 4 && len <= sizeof(b) && g_prefs.getBytes("map", b, len) == len && b[0] == 'M' && b[1] == 1 &&
      b[2] <= MAX_MAP && len == (size_t)(4 + b[2] * 6)) {
    g_mapCount = b[2];
    for (uint8_t i = 0; i < g_mapCount; i++) {
      const uint8_t* p = b + 4 + i * 6;
      g_map[i].key = p[0] | (p[1] << 8);
      g_map[i].tv = p[2] | (p[3] << 8);
      g_map[i].cc = p[4] | (p[5] << 8);
    }
    return;
  }
  loadDefaultMap();
}

static int findMap(uint16_t key) {
  for (uint8_t i = 0; i < g_mapCount; i++) {
    if (g_map[i].key == key) return i;
  }
  return -1;
}

// ============================================================================
// Bluetooth callbacks (run in the Bluetooth task: copy data, never block, never print)
// ============================================================================
// Sees every notification before the library handles it. Button reports are taken
// from here, by handle, so a press is caught even before service discovery has run.
static int gapListener(ble_gap_event* event, void* arg) {
  (void)arg;
  if (event->type == BLE_GAP_EVENT_ENC_CHANGE || event->type == BLE_GAP_EVENT_MTU ||
      event->type == BLE_GAP_EVENT_SUBSCRIBE) {
    Event e{};
    e.type = EV_DIAG;
    e.data[0] = event->type;
    if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
      e.conn = event->enc_change.conn_handle;
      e.reason = event->enc_change.status;
    } else if (event->type == BLE_GAP_EVENT_MTU) {
      e.conn = event->mtu.conn_handle;
      e.reason = event->mtu.value;
    } else {
      e.conn = event->subscribe.conn_handle;
      e.handle = event->subscribe.attr_handle;
      e.flag = event->subscribe.cur_notify;
    }
    ble_gap_conn_desc d;
    // Only TV/Chromecast-side links (where the ESP32 is the peripheral).
    if (ble_gap_conn_find(e.conn, &d) == 0 && d.role != BLE_GAP_ROLE_MASTER) postEvent(e);
    return 0;
  }
  if (event->type != BLE_GAP_EVENT_NOTIFY_RX) return 0;
  const uint16_t conn = event->notify_rx.conn_handle;
  ble_gap_conn_desc desc;
  // The remote link is the only connection where the ESP32 is the central (master).
  if (ble_gap_conn_find(conn, &desc) != 0 || desc.role != BLE_GAP_ROLE_MASTER) return 0;
  if (!isInputHandle(event->notify_rx.attr_handle)) return 0;
  Event e{};
  e.type = EV_REPORT;
  e.conn = conn;
  e.handle = event->notify_rx.attr_handle;
  uint16_t len = os_mbuf_len(event->notify_rx.om);
  if (len > sizeof(e.data)) len = sizeof(e.data);
  if (len > 0 && os_mbuf_copydata(event->notify_rx.om, 0, len, e.data) != 0) return 0;
  e.len = (uint8_t)len;
  postEvent(e);
  return 0;
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
    (void)s;
    Event e{};
    e.type = EV_HOST_CONN;
    e.conn = ci.getConnHandle();
    NimBLEAddress a = ci.getAddress();
    e.addrType = a.getType();
    memcpy(e.addr, a.getVal(), 6);
    postEvent(e);
    if (s->getConnectedCount() < 2) NimBLEDevice::startAdvertising();  // as in the two-host test: let the 2nd in
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
    (void)s;
    Event e{};
    e.type = EV_HOST_DISC;
    e.conn = ci.getConnHandle();
    e.reason = reason;
    postEvent(e);
    NimBLEDevice::startAdvertising();  // as in the two-host test
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    Event e{};
    e.type = EV_HOST_AUTH;
    e.conn = ci.getConnHandle();
    e.flag = (ci.isEncrypted() && ci.isBonded()) ? 1 : 0;  // a failed pairing can leave "encrypted" set
    NimBLEAddress id = ci.getIdAddress();
    e.addrType = id.getType();
    memcpy(e.addr, id.getVal(), 6);
    postEvent(e);
  }
} g_serverCb;

class InputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& ci, uint16_t subValue) override {
    (void)c;
    Event e{};
    e.type = EV_HOST_SUB;
    e.conn = ci.getConnHandle();
    e.handle = subValue;
    postEvent(e);
  }
} g_inputCb;

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* c) override {
    (void)c;
    Event e{};
    e.type = EV_REM_CONN;
    postEvent(e);
  }
  void onConnectFail(NimBLEClient* c, int reason) override {
    (void)c;
    Event e{};
    e.type = EV_REM_FAIL;
    e.reason = reason;
    postEvent(e);
  }
  void onDisconnect(NimBLEClient* c, int reason) override {
    (void)c;
    Event e{};
    e.type = EV_REM_DISC;
    e.reason = reason;
    postEvent(e);
  }
} g_clientCb;

// When pairing storage is full, the library's default is to delete an existing
// pairing to make room. This refuses instead, so a pairing is never lost silently.
class DeviceCallbacks : public NimBLEDeviceCallbacks {
  int onStoreStatus(ble_store_status_event* event, void* arg) override {
    (void)arg;
    if (event->event_code == BLE_STORE_EVENT_OVERFLOW) {
      g_storeOverflow = true;
      return BLE_HS_ESTORE_CAP;
    }
    return 0;
  }
} g_deviceCb;

// The RC902V advertises appearance "remote control" and the name TCL_RC902V_MH.
static bool looksLikeRemote(const NimBLEAdvertisedDevice* d) {
  if (d->haveAppearance()) {
    uint16_t a = d->getAppearance();
    if (a >= 0x0180 && a <= 0x01BF) return true;  // appearance = remote control
  }
  String name = d->getName().c_str();
  name.toUpperCase();
  return name.startsWith("TCL_RC");
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (!g_scanArmed) return;
    if (!d->isConnectable() || d->getRSSI() < -75) return;  // the remote is held next to the board
    if (!looksLikeRemote(d)) return;
    NimBLEAddress a = d->getAddress();
    Event e{};
    e.addrType = a.getType();
    memcpy(e.addr, a.getVal(), 6);
    e.rssi = (int8_t)d->getRSSI();
    snprintf(e.name, sizeof(e.name), "%s", d->getName().c_str());
    static const uint8_t ZERO[6] = {0};
    if (memcmp(e.addr, ZERO, 6) == 0) {  // all-zero address: impossible to connect to, never pick it
      if (!g_nullNoted.exchange(true)) {
        e.type = EV_SCAN_NOTE;
        postEvent(e);
      }
      return;
    }
    if (isIgnored(a.getVal(), a.getType())) return;
    g_scanArmed = false;  // one candidate at a time
    e.type = EV_SCAN_HIT;
    postEvent(e);
  }
} g_scanCb;

// ============================================================================
// Remote setup task: encryption, discovery and subscriptions block while they wait
// for the remote, so they run here. loop() keeps forwarding buttons meanwhile.
// ============================================================================
static bool remoteEncrypted() { return g_client->isConnected() && g_client->getConnInfo().isEncrypted(); }

static void runSetup(uint8_t mode, SetupResult& r) {
  r = SetupResult{};
  r.mode = mode;
  r.pnpMatch = -1;
  r.mapMatch = -1;
  if (!g_client->isConnected()) {
    r.fail = SF_NOT_CONNECTED;
    return;
  }

  // 1. Encryption: with the saved keys (reconnect) or a new pairing (pair mode).
  if (!remoteEncrypted()) {
    g_client->secureConnection(true);  // asynchronous; the remote may also request it itself
    uint32_t t0 = millis();
    while (g_client->isConnected() && !remoteEncrypted() && millis() - t0 < 20000) vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (!remoteEncrypted()) {
    r.fail = g_client->isConnected() ? SF_SECURITY : SF_NOT_CONNECTED;
    return;
  }

  // 2. Services, as tools/remote_sniffer does.
  if (mode == SETUP_PAIR) {
    g_client->getServices(true);  // full discovery
    NimBLERemoteService* dis = g_client->getService(NimBLEUUID((uint16_t)0x180A));
    NimBLERemoteCharacteristic* pnp = dis ? dis->getCharacteristic(NimBLEUUID((uint16_t)0x2A50)) : nullptr;
    if (pnp) {
      NimBLEAttValue v = pnp->readValue();
      r.pnpMatch = (v.length() == sizeof(RC902V_PNP) && memcmp(v.data(), RC902V_PNP, sizeof(RC902V_PNP)) == 0) ? 1 : 0;
    }
  }

  NimBLERemoteService* hid = g_client->getService(NimBLEUUID((uint16_t)0x1812));
  if (!hid) {
    r.fail = g_client->isConnected() ? SF_NO_HID : SF_NOT_CONNECTED;
    return;
  }
  // Discover ALL characteristics of the HID service first. (A lookup of a single
  // characteristic by UUID fills the cache with only that one; that was the v1.0 bug.)
  if (hid->getCharacteristics(false).empty()) hid->getCharacteristics(true);
  if (mode == SETUP_PAIR) {
    for (NimBLERemoteCharacteristic* m : hid->getCharacteristics(false)) {
      if (m->getUUID() != NimBLEUUID((uint16_t)0x2A4B)) continue;  // report map
      NimBLEAttValue v = m->readValue();
      r.mapMatch =
          (v.length() == sizeof(RC902V_REPORT_MAP) && memcmp(v.data(), RC902V_REPORT_MAP, sizeof(RC902V_REPORT_MAP)) == 0)
              ? 1
              : 0;
      break;
    }
  }

  uint16_t known[4];
  uint8_t nKnown = (mode == SETUP_PAIR) ? 0 : getInputHandles(known);

  for (NimBLERemoteCharacteristic* c : hid->getCharacteristics(false)) {
    if (c->getUUID() != NimBLEUUID((uint16_t)0x2A4D) || !c->canNotify()) continue;
    bool ours = false;
    if (nKnown > 0) {
      for (uint8_t i = 0; i < nKnown; i++) {
        if (known[i] == c->getHandle()) ours = true;
      }
    } else {
      NimBLERemoteDescriptor* ref = c->getDescriptor(NimBLEUUID((uint16_t)0x2908));  // Report Reference
      if (ref) {
        NimBLEAttValue v = ref->readValue();
        ours = v.length() >= 1 && v.data()[0] == REPORT_ID;  // report ID 3 (same check as tools/remote_sniffer)
      }
    }
    if (!c->subscribe(true, nullptr) && ours) r.subscribeFailed = true;
    if (ours && r.nHandles < 4) r.handles[r.nHandles++] = c->getHandle();
  }
  if (r.nHandles == 0) {
    r.fail = g_client->isConnected() ? SF_NO_INPUT : SF_NOT_CONNECTED;
    return;
  }

  // 3. The same subscriptions tools/remote_sniffer makes on the voice service (its data is ignored).
  NimBLERemoteService* voice = g_client->getService(NimBLEUUID(ATVV_SERVICE));
  if (voice) {
    if (voice->getCharacteristics(false).empty()) voice->getCharacteristics(true);
    for (NimBLERemoteCharacteristic* c : voice->getCharacteristics(false)) {
      if (c->canNotify()) c->subscribe(true, nullptr);
    }
  }

  // 4. After a new pairing, wait until the keys are stored before reading the identity.
  if (mode == SETUP_PAIR) {
    uint32_t t0 = millis();
    while (g_client->isConnected() && !g_client->getConnInfo().isBonded() && millis() - t0 < 5000) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    r.notBonded = g_client->isConnected() && !g_client->getConnInfo().isBonded();
  }
  if (!g_client->isConnected()) {
    r.fail = SF_NOT_CONNECTED;
    return;
  }
  NimBLEAddress id = g_client->getConnInfo().getIdAddress();
  r.idType = id.getType();
  memcpy(r.idVal, id.getVal(), 6);
  r.fail = SF_OK;
}

static void setupTask(void* arg) {
  (void)arg;
  SetupRequest req;
  for (;;) {
    if (xQueueReceive(g_setupQ, &req, portMAX_DELAY) != pdTRUE) continue;
    SetupResult r;
    runSetup(req.mode, r);
    r.session = req.session;
    g_setupResult = r;  // loop() reads it after EV_REM_SETUP arrives
    Event e{};
    e.type = EV_REM_SETUP;
    e.flag = (r.fail == SF_OK) ? 1 : 0;
    postEvent(e, portMAX_DELAY);
    g_setupBusy = false;
  }
}

static void requestSetup(uint8_t mode) {
  SetupRequest req{mode, ++g_remSession};
  g_setupBusy = true;
  if (xQueueSend(g_setupQ, &req, 0) != pdTRUE) g_setupBusy = false;
}

// ============================================================================
// Sending buttons to the TV / Chromecast
// ============================================================================
static Host* hostFind(uint16_t conn) {
  for (auto& h : g_hosts) {
    if (h.used && h.conn == conn) return &h;
  }
  return nullptr;
}

static Host* hostAdd(uint16_t conn) {
  Host* h = hostFind(conn);
  if (h) return h;
  for (auto& s : g_hosts) {
    if (!s.used) {
      uint32_t now = millis();
      s = Host{true, conn, ROLE_NONE, false, false, now, 0, now + 3000, -1};
      return &s;
    }
  }
  return nullptr;
}

static Host* hostByRole(uint8_t role) {
  for (auto& h : g_hosts) {
    if (h.used && h.role == role) return &h;
  }
  return nullptr;
}

static uint16_t connForRole(uint8_t role) {  // only a connection that is identified and encrypted
  Host* h = hostByRole(role);
  return (h && h->enc) ? h->conn : NO_CONN;
}

static uint8_t hostCount() {
  uint8_t n = 0;
  for (auto& h : g_hosts) {
    if (h.used) n++;
  }
  return n;
}

#if KEYBOARD_ONLY_LIKE_TEST
// Only the two-host test's keyboard report exists: the D-pad and OK are sent as keyboard keys.
// That report (keys 0x00-0x65) cannot carry Back, Home, volume, mute, channel or TCL keys.
static uint8_t keyboardKeyFor(uint16_t code) {
  switch (code) {
    case 0x0042: return 0x52;  // Up    -> Up arrow
    case 0x0043: return 0x51;  // Down  -> Down arrow
    case 0x0044: return 0x50;  // Left  -> Left arrow
    case 0x0045: return 0x4F;  // Right -> Right arrow (what the two-host test sends)
    case 0x0041: return 0x28;  // OK    -> Enter
    default: return 0;
  }
}
#endif

static bool canSend(uint16_t code) {
#if KEYBOARD_ONLY_LIKE_TEST
  return keyboardKeyFor(code) != 0;
#else
  return code != 0 && code <= MAX_CODE;
#endif
}

static bool sendCode(uint16_t conn, uint16_t code, uint8_t len) {
#if KEYBOARD_ONLY_LIKE_TEST
  (void)len;
  uint8_t buf[8] = {0};  // the same 8-byte keyboard report the two-host test sends
  if (code != 0) buf[2] = keyboardKeyFor(code);
  return g_inputChr->notify(buf, sizeof(buf), conn);
#else
  uint8_t buf[TV_REPORT_LEN] = {0};  // code low byte, high byte, then zeros (same layout as the RC902V)
  buf[0] = code & 0xFF;
  buf[1] = code >> 8;
  return g_inputChr->notify(buf, len, conn);
#endif
}

static void sendRelease(uint16_t conn, uint8_t len) {
  for (int i = 0; i < 3; i++) {  // a lost release would leave the key held (auto-repeat)
    if (sendCode(conn, 0, len)) return;
    delay(5);
  }
  out("WARNING: could not send a button release\n");
}

static void releaseHeld() {
  if (!g_held.active) return;
  if (g_held.tvConn != NO_CONN) sendRelease(g_held.tvConn, TV_REPORT_LEN);
  if (g_held.ccConn != NO_CONN) sendRelease(g_held.ccConn, CC_REPORT_LEN);
  g_held = Held{false, 0, NO_CONN, NO_CONN};
}

static bool isNavKey(uint16_t key) {  // arrows, OK, Back
  return (key >= 0x0041 && key <= 0x0045) || key == 0x0224;
}

static void setTvMode(bool on, const char* why) {
  g_tvMode = on;
  g_tvSession = false;
  g_anyKeyLastMs = millis();
  if (on) {
    out(">> TV MODE (%s): arrows, OK and Back now control the TV only\n", why);
  } else {
    out(">> CHROMECAST MODE (%s): arrows, OK and Back follow the table again\n", why);
  }
}

static void pressKey(uint16_t key) {
  const uint32_t now = millis();
  g_anyKeyLastMs = now;
  g_held = Held{true, key, NO_CONN, NO_CONN};
  if (g_switchKey != 0 && key == g_switchKey) {  // the switch button is never sent anywhere
    setTvMode(!g_tvMode, "switch button");
    return;
  }
  int i = findMap(key);
  uint16_t tvCode = (i >= 0) ? g_map[i].tv : key;  // not in the table: TV gets it unchanged
  uint16_t ccCode = (i >= 0) ? g_map[i].cc : 0;
  const char* note = (i >= 0) ? " " : " (not in table) ";
  bool endSession = false;
  if (g_inputKey != 0 && key == g_inputKey) {  // Input: open the TV input menu, start a TV session
    tvCode = key;
    ccCode = 0;
    if (!g_tvMode) {
      g_tvSession = true;
      g_navLastMs = now;
      note = " (TV session started) ";
    }
  } else if (isNavKey(key) && (g_tvMode || g_tvSession)) {  // arrows, OK, Back -> TV only
    tvCode = key;
    ccCode = 0;
    g_navLastMs = now;
    note = g_tvMode ? " (TV mode) " : " (TV session) ";
    if (g_tvSession && key == 0x0041) endSession = true;  // OK picks the input: session done
  }

  char tvTxt[64], ccTxt[64], lbl[24];
  if (tvCode == 0) {
    snprintf(tvTxt, sizeof(tvTxt), "TV -");
  } else if (!canSend(tvCode)) {
    snprintf(tvTxt, sizeof(tvTxt), "TV 0x%04X (not sent: keyboard-only mode)", tvCode);
  } else {
    uint16_t c = connForRole(ROLE_TV);
    if (c != NO_CONN && sendCode(c, tvCode, TV_REPORT_LEN)) {
      g_held.tvConn = c;
      snprintf(tvTxt, sizeof(tvTxt), "TV 0x%04X", tvCode);
    } else {
      snprintf(tvTxt, sizeof(tvTxt), "TV 0x%04X (not sent: TV not connected)", tvCode);
    }
  }
  if (ccCode == 0) {
    snprintf(ccTxt, sizeof(ccTxt), "Chromecast -");
  } else if (!canSend(ccCode)) {
    snprintf(ccTxt, sizeof(ccTxt), "Chromecast 0x%04X (not sent: keyboard-only mode)", ccCode);
  } else {
    uint16_t c = connForRole(ROLE_CC);
    if (c != NO_CONN && sendCode(c, ccCode, CC_REPORT_LEN)) {
      g_held.ccConn = c;
      snprintf(ccTxt, sizeof(ccTxt), "Chromecast 0x%04X", ccCode);
    } else {
      snprintf(ccTxt, sizeof(ccTxt), "Chromecast 0x%04X (not sent: not connected)", ccCode);
    }
  }
  if (g_logKeys) {
    out("Key 0x%04X %s%s-> %s | %s\n", key, keyLabel(key, lbl, sizeof(lbl)), note, tvTxt, ccTxt);
  }
  if (endSession) {
    g_tvSession = false;
    out(">> TV session ended (OK pressed): arrows, OK and Back follow the table again\n");
  }
}

static void checkTvTimers(uint32_t now) {
  if (g_tvSession && now - g_navLastMs >= g_tvSessionMs) {
    g_tvSession = false;
    out(">> TV session ended (no arrow/OK/Back for %u s): they follow the table again\n",
        (unsigned)(g_tvSessionMs / 1000));
  }
  if (g_tvMode && g_tvModeMs != 0 && now - g_anyKeyLastMs >= g_tvModeMs) {
    char why[40];
    snprintf(why, sizeof(why), "no button pressed for %u s", (unsigned)(g_tvModeMs / 1000));
    setTvMode(false, why);
  }
}

static void handleReport(const Event& e) {
  if (e.len < 2) return;
  uint16_t code = e.data[0] | (e.data[1] << 8);
  if (code == 0) {  // all zeros = button released
    releaseHeld();
    return;
  }
  if (g_held.active && g_held.key == code) return;  // same button still held
  if (g_held.active) releaseHeld();                 // a different button: release the old one first
  pressKey(code);
}

// ============================================================================
// Pairing records
// ============================================================================
static void startScan();

// NimBLE refuses to delete a pairing that holds an identity key (all Android TVs send one)
// while advertising or scanning is running: it returns "busy" and keeps the pairing.
// So both are paused around every deletion; loop() restarts advertising if still wanted.
static bool deleteBondSafe(const NimBLEAddress& a) {
  if (!NimBLEDevice::isBonded(a)) return false;  // nothing saved for it
  NimBLEDevice::stopAdvertising();
  bool scanWasOn = NimBLEDevice::getScan()->isScanning();
  if (scanWasOn) NimBLEDevice::getScan()->stop();
  bool ok = NimBLEDevice::deleteBond(a);
  if (scanWasOn) startScan();
  return ok;
}

static bool hasBond(const Addr& a) {
  if (!a.set) return false;
  int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; i++) {
    NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
    if (!b.isNull() && addrEq(a, b.getVal(), b.getType())) return true;
  }
  return false;
}

static void validateRoles() {  // a TV/Chromecast slot without saved keys can never reconnect: clear it
  if (g_tv.set && !hasBond(g_tv)) {
    g_tv.set = false;
    saveAddr("tv", g_tv);
    out("TV slot had no saved pairing keys - cleared. Remove \"%s\" on the TV and type: pair tv\n", DEVICE_NAME);
  }
  if (g_cc.set && !hasBond(g_cc)) {
    g_cc.set = false;
    saveAddr("cc", g_cc);
    out("Chromecast slot had no saved pairing keys - cleared. Remove \"%s\" on the Chromecast and type: pair cc\n",
        DEVICE_NAME);
  }
}

static void pruneBonds() {  // delete any saved pairing that is not the remote, the TV or the Chromecast
  int n = NimBLEDevice::getNumBonds();
  for (int i = n - 1; i >= 0; i--) {
    NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    if (a.isNull()) continue;
    const uint8_t* v = a.getVal();
    uint8_t t = a.getType();
    if (addrEq(g_remote, v, t) || addrEq(g_remoteId, v, t) || addrEq(g_tv, v, t) || addrEq(g_cc, v, t)) continue;
    if (deleteBondSafe(a)) {
      out("Removed a leftover pairing: %s\n", a.toString().c_str());
    } else {
      out("Could not remove leftover pairing %s\n", a.toString().c_str());
    }
  }
}

static void forgetRole(uint8_t role) {
  for (auto& h : g_hosts) {
    if (h.used && h.role == role) {
      g_server->disconnect(h.conn);
      h.role = ROLE_NONE;
    }
  }
  Addr& a = (role == ROLE_TV) ? g_tv : g_cc;
  if (a.set) deleteBondSafe(toNim(a));
  a.set = false;
  saveAddr(role == ROLE_TV ? "tv" : "cc", a);
}

static void forgetRemoteRecords() {
  if (g_remote.set) deleteBondSafe(toNim(g_remote));
  if (g_remoteId.set && !addrEq(g_remote, g_remoteId.val, g_remoteId.type)) deleteBondSafe(toNim(g_remoteId));
  g_remote.set = false;
  g_remoteId.set = false;
  saveAddr("remote", g_remote);
  saveAddr("remid", g_remoteId);
  setInputHandles(nullptr, 0);
  saveHandles();
  g_remSession++;  // a setup still running for the forgotten remote is ignored
}

// ============================================================================
// Remote: connection and pairing
// ============================================================================
static bool remoteLinkIdle() { return !g_client->isConnected() && g_rem != R_CONNECTING && !g_setupBusy; }

static void startScan() {
  g_lastScanStart = millis();
  g_scanArmed = true;
  if (!NimBLEDevice::getScan()->start(0, false, true)) out("Could not start scanning, retrying...\n");
}

static void stopRemotePairing(const char* why) {
  g_scanArmed = false;
  if (NimBLEDevice::getScan()->isScanning()) NimBLEDevice::getScan()->stop();
  if (g_rem == R_CONNECTING) g_client->cancelConnect();
  if (g_client->isConnected() && !g_remote.set) g_client->disconnect();
  g_pair = PAIR_NONE;
  out("Remote pairing stopped: %s\n", why);
  if (!g_remote.set) out("No remote is paired. Type: pair remote\n");
}

static void startRemotePairing() {
  g_pair = PAIR_REMOTE;
  g_pairSince = millis();
  g_pairStage = PS_PREP;
  g_pairCancelSent = false;
  g_pairDiscSent = false;
  portENTER_CRITICAL(&g_mux);
  g_ignoreCount = 0;
  portEXIT_CRITICAL(&g_mux);
  g_nullNoted = false;
  out("\n>> REMOTE PAIRING (2 min): hold the RC902V next to the board and hold OK + Home for 3 s.\n");
  out("   Keep the Chromecast remote away (or its batteries out) - it would be found too.\n");
}

static void connectCandidate(const Event& e) {
  NimBLEDevice::getScan()->stop();
  char t[20];
  out("Found '%s' at %s (RSSI %d) - connecting...\n", e.name[0] ? e.name : "(no name)",
      addrText(g_candidate, t, sizeof(t)), e.rssi);
  deleteBondSafe(toNim(g_candidate));  // always a fresh pairing (like tools/remote_sniffer); no-op if none
  g_client->setConnectTimeout(15000);
  if (g_client->connect(toNim(g_candidate), true, true, true)) {  // fresh attribute cache, asynchronous
    g_rem = R_CONNECTING;
    g_pairStage = PS_CONNECT;
  } else {
    out("Connect failed (rc=%d) - skipping this device\n", g_client->getLastError());
    addIgnored(g_candidate);
    g_pairStage = PS_RESCAN;
  }
}

static void manageRemotePairing(uint32_t now) {
  if (now - g_pairSince > REMOTE_PAIR_MS) {
    stopRemotePairing("timed out");
    return;
  }
  switch (g_pairStage) {
    case PS_PREP:  // drop the current link, then forget the old remote
      if (g_rem == R_CONNECTING) {
        if (!g_pairCancelSent) {
          g_client->cancelConnect();
          g_pairCancelSent = true;
        }
        return;
      }
      if (g_client->isConnected()) {
        if (!g_pairDiscSent) {
          g_client->disconnect();
          g_pairDiscSent = true;
        }
        return;
      }
      if (g_setupBusy) return;
      forgetRemoteRecords();
      g_rem = R_NONE;
      startScan();
      g_pairStage = PS_SCAN;
      return;
    case PS_RESCAN:
      if (!remoteLinkIdle() || now - g_lastScanStart < 300) return;
      g_rem = R_NONE;
      startScan();
      g_pairStage = PS_SCAN;
      return;
    case PS_SCAN:
      // NimBLE pauses scanning while it stores a new TV/Chromecast identity key and
      // doesn't resume it; restart it here.
      if (g_scanArmed && !NimBLEDevice::getScan()->isScanning() && now - g_lastScanStart > 1000) startScan();
      return;
    default:
      return;  // PS_CONNECT / PS_SETUP: waiting for events
  }
}

// True while a TV/Chromecast link is still connecting or pairing, and for up to 15 s after boot
// while a paired TV/Chromecast hasn't reconnected yet. During that time the bridge does not
// listen for the remote, so the radio does exactly what the two-host test does.
static bool hostsSettling(uint32_t now) {
  for (auto& h : g_hosts) {
    if (h.used && !h.enc) return true;
  }
  const bool missing = (g_tv.set && !hostByRole(ROLE_TV)) || (g_cc.set && !hostByRole(ROLE_CC));
  return missing && now - g_bootMs < 15000;
}

static void manageRemote(uint32_t now) {
  if (g_pair == PAIR_REMOTE) {
    manageRemotePairing(now);
    return;
  }
  if (!g_remote.set) {
    if (g_rem == R_IDLE) g_rem = R_NONE;
    return;
  }
  if (g_pair == PAIR_TV || g_pair == PAIR_CC) return;  // remote paused while a TV/Chromecast pairs
  if (hostsSettling(now)) {
    if (g_rem == R_CONNECTING && !g_initCancelSent) {
      g_client->cancelConnect();  // stop listening for the remote until the TV/Chromecast is settled
      g_initCancelSent = true;
    }
    return;
  }
  if ((g_rem == R_NONE || g_rem == R_IDLE) && remoteLinkIdle() && timeReached(now, g_remRetryAt)) {
    // Wait for the remote with no time limit: as soon as it wakes up and advertises, it is connected.
    g_client->setConnectTimeout(BLE_HS_FOREVER);
    if (g_client->connect(toNim(g_remote), false, true, true)) {  // keep the attribute cache between connections
      g_rem = R_CONNECTING;
      g_initCancelSent = false;
    } else {
      g_remRetryAt = now + 2000;
    }
  }
  if (g_rem == R_SETUP && now - g_remSince > 60000 && !g_setupBusy && g_client->isConnected()) {
    out("Remote setup stuck - reconnecting\n");
    g_client->disconnect();
    g_remSince = now;
  }
}

static void onRemoteConnected(uint32_t now) {
  if (g_pair == PAIR_REMOTE) {
    if (g_pairStage == PS_CONNECT) {
      g_rem = R_SETUP;
      g_remSince = now;
      g_pairStage = PS_SETUP;
      out("Connected - pairing...\n");
      requestSetup(SETUP_PAIR);
    } else {
      g_client->disconnect();  // the old remote connected while pairing was being prepared
    }
    return;
  }
  if (!g_remote.set || g_pair == PAIR_TV || g_pair == PAIR_CC) {  // forgotten, or paused for pairing
    g_client->disconnect();
    return;
  }
  g_rem = R_SETUP;
  g_remSince = now;
  requestSetup(SETUP_RECONNECT);
}

static void onRemoteGone(uint32_t now, bool connectFailed, int reason) {
  releaseHeld();  // never leave a button "held" on the TV / Chromecast
  if (g_pair == PAIR_REMOTE) {
    if (g_pairStage == PS_CONNECT || g_pairStage == PS_SETUP) g_pairStage = PS_RESCAN;
    g_rem = R_IDLE;
    return;
  }
  if (!connectFailed && (g_rem == R_SETUP || g_rem == R_READY)) {
    out("Remote disconnected (reason 0x%X) - it reconnects when you press a button\n", reason);
  }
  g_rem = g_remote.set ? R_IDLE : R_NONE;
  g_remRetryAt = now + (connectFailed ? 500 : 100);
}

static void onRemoteSetupDone() {
  const SetupResult r = g_setupResult;
  if (r.session != g_remSession) return;  // result of an older connection: ignore
  char t[20];
  if (r.fail == SF_OK) {
    uint16_t cur[4];
    uint8_t nCur = getInputHandles(cur);
    if (nCur != r.nHandles || memcmp(cur, r.handles, r.nHandles * sizeof(uint16_t)) != 0) {
      setInputHandles(r.handles, r.nHandles);
      saveHandles();
    }
    if (r.mode == SETUP_PAIR) {
      if (g_pair == PAIR_REMOTE) {
        g_scanArmed = false;
        if (NimBLEDevice::getScan()->isScanning()) NimBLEDevice::getScan()->stop();
        g_pair = PAIR_NONE;
      }
      g_remote = g_candidate;
      fromRaw(g_remoteId, r.idVal, r.idType);
      saveAddr("remote", g_remote);
      saveAddr("remid", g_remoteId);
      g_remSecFails = 0;
      out("\n>> REMOTE PAIRED: %s\n", addrText(g_remote, t, sizeof(t)));
      out("   Report map %s RC902V capture, PnP ID %s\n",
          r.mapMatch == 1 ? "matches the" : (r.mapMatch == 0 ? "DIFFERS from the" : "not read, compared to the"),
          r.pnpMatch == 1 ? "matches" : (r.pnpMatch == 0 ? "DIFFERS" : "not read"));
      if (r.mapMatch == 0) out("   WARNING: this remote's report format is not the one this bridge was made for.\n");
      if (r.notBonded) out("   WARNING: the remote did not save the pairing; it may not reconnect after sleep.\n");
    } else {
      g_remSecFails = 0;
    }
    if (r.subscribeFailed) out("   Note: subscribing to button reports returned an error; test the buttons.\n");
    if (g_client->isConnected()) {
      g_rem = R_READY;
      out("Remote ready\n");
    }
    return;
  }

  static const char* why[] = {"", "link dropped", "encryption failed", "no HID service (not a remote)",
                              "no button report found"};
  out("Remote setup failed: %s\n", r.fail < 5 ? why[r.fail] : "?");
  if (r.mode == SETUP_PAIR) {
    deleteBondSafe(toNim(g_candidate));  // don't keep a pairing for a device we didn't accept
    if (r.fail == SF_NO_HID || r.fail == SF_NO_INPUT) addIgnored(g_candidate);
  }
  if (r.mode == SETUP_RECONNECT && r.fail == SF_SECURITY && ++g_remSecFails == 3) {
    out("The remote keeps refusing the saved pairing (paired to another device?). Type: pair remote\n");
  }
  if (g_client->isConnected()) g_client->disconnect();
  if (g_pair == PAIR_REMOTE && g_pairStage == PS_SETUP) g_pairStage = PS_RESCAN;
}

// ============================================================================
// TV / Chromecast connections
// ============================================================================
static const char* roleName(uint8_t role) { return role == ROLE_TV ? "TV" : (role == ROLE_CC ? "Chromecast" : "device"); }

static void onHostAuth(const Event& e) {
  Host* h = hostAdd(e.conn);
  if (!h) return;
  char t[20];
  Addr id{};
  fromRaw(id, e.addr, e.addrType);
  if (!e.flag || h->encStatus > 0) {
    out("Pairing with %s did not complete (status %d) - not saved; the device can simply try again.\n",
        addrText(id, t, sizeof(t)), (int)h->encStatus);
    out("  If it keeps failing: remove \"%s\" on that device, then pair again.\n", DEVICE_NAME);
    return;
  }
  h->enc = true;
  uint8_t role = ROLE_NONE;
  if (addrEq(g_tv, e.addr, e.addrType)) {
    role = ROLE_TV;
  } else if (addrEq(g_cc, e.addr, e.addrType)) {
    role = ROLE_CC;
  } else if (g_pair == PAIR_TV || g_pair == PAIR_CC || !g_tv.set || !g_cc.set) {
    const bool chosen = (g_pair == PAIR_TV || g_pair == PAIR_CC);
    if (chosen) {
      role = (g_pair == PAIR_TV) ? ROLE_TV : ROLE_CC;
    } else {
      role = !g_tv.set ? ROLE_TV : ROLE_CC;  // the free slot, TV first
    }
    Addr& slot = (role == ROLE_TV) ? g_tv : g_cc;
    slot = id;
    saveAddr(role == ROLE_TV ? "tv" : "cc", slot);
    if (chosen) g_pair = PAIR_NONE;
    out("\n>> %s PAIRED: %s\n", role == ROLE_TV ? "TV" : "CHROMECAST", addrText(id, t, sizeof(t)));
    if (!chosen) {
      out("   (it took the free %s slot; if this device is actually the %s, type: swap)\n", roleName(role),
          role == ROLE_TV ? "Chromecast" : "TV");
    }
  } else {
    out("A third device paired (%s) - rejected: the TV and Chromecast slots are taken (see: forget).\n",
        addrText(id, t, sizeof(t)));
    deleteBondSafe(toNim(id));
    g_server->disconnect(e.conn);
    return;
  }
  for (auto& o : g_hosts) {  // a role belongs to one connection only
    if (o.used && &o != h && o.role == role) o.role = ROLE_NONE;
  }
  h->role = role;
  out("%s connected\n", roleName(role));
}

static const char* reasonText(int32_t r) {  // NimBLE reports 0x200 + the Bluetooth error code
  switch (r) {
    case 0x205: return "authentication failure";
    case 0x206: return "pairing key missing";
    case 0x208: return "signal lost (supervision timeout)";
    case 0x213: return "the other device ended the connection";
    case 0x216: return "the ESP32 ended the connection";
    case 0x21F: return "unspecified error";
    case 0x222: return "link layer response timeout";
    case 0x23B: return "unacceptable connection parameters";
    case 0x23D: return "MIC failure (encryption)";
    case 0x23E: return "connection failed to be established";
    default: return "see Bluetooth error codes";
  }
}

static void onHostDisconnected(const Event& e) {
  if (g_held.tvConn == e.conn) g_held.tvConn = NO_CONN;
  if (g_held.ccConn == e.conn) g_held.ccConn = NO_CONN;
  Host* h = hostFind(e.conn);
  if (!h) {
    out("Device on connection %u disconnected (reason 0x%X: %s)\n", e.conn, (unsigned)e.reason, reasonText(e.reason));
    return;
  }
  if (h->role != ROLE_NONE) {
    out("%s disconnected (reason 0x%X: %s)\n", roleName(h->role), (unsigned)e.reason, reasonText(e.reason));
  } else {
    out("Device on connection %u disconnected after %u ms, %s (reason 0x%X: %s)\n", e.conn,
        (unsigned)(millis() - h->since), h->enc ? "after securing" : "BEFORE pairing/securing finished",
        (unsigned)e.reason, reasonText(e.reason));
  }
  h->used = false;
}

static void handleEvent(const Event& e) {
  const uint32_t now = millis();
  switch (e.type) {
    case EV_REPORT:
      handleReport(e);
      break;
    case EV_HOST_CONN: {
      Addr a{};
      fromRaw(a, e.addr, e.addrType);
      char t[20];
      if (!hostAdd(e.conn)) {
        out("No free connection slot for %s\n", addrText(a, t, sizeof(t)));
      } else {
        out("Device %s connected on connection %u - waiting for it to pair/secure...\n", addrText(a, t, sizeof(t)),
            e.conn);
      }
      break;
    }
    case EV_HOST_DISC:
      onHostDisconnected(e);
      break;
    case EV_HOST_AUTH:
      onHostAuth(e);
      break;
    case EV_HOST_SUB: {
      Host* h = hostFind(e.conn);
      if (h) h->sub = (e.handle & 0x0001) != 0;
      break;
    }
    case EV_REM_CONN:
      onRemoteConnected(now);
      break;
    case EV_REM_FAIL:
      onRemoteGone(now, true, e.reason);
      break;
    case EV_REM_DISC:
      onRemoteGone(now, false, e.reason);
      break;
    case EV_REM_SETUP:
      onRemoteSetupDone();
      break;
    case EV_SCAN_HIT:
      if (g_pair == PAIR_REMOTE && g_pairStage == PS_SCAN) {
        fromRaw(g_candidate, e.addr, e.addrType);
        connectCandidate(e);
      }
      break;
    case EV_DIAG: {
      Host* h = hostFind(e.conn);
      if (h && e.data[0] == BLE_GAP_EVENT_ENC_CHANGE) h->encStatus = e.reason;
      bool show = (g_pair == PAIR_TV || g_pair == PAIR_CC) || (h && h->role == ROLE_NONE);
      if (!show) break;
      if (e.data[0] == BLE_GAP_EVENT_MTU) {
        out("  [conn %u] data size (MTU) agreed: %d\n", e.conn, (int)e.reason);
      } else if (e.data[0] == BLE_GAP_EVENT_ENC_CHANGE) {
        out("  [conn %u] pairing/encryption finished: %s (status %d%s)\n", e.conn, e.reason == 0 ? "OK" : "FAILED",
            (int)e.reason, e.reason == BLE_HS_ENOTCONN ? ": link dropped during pairing" : "");
      } else {
        out("  [conn %u] subscription on handle %u turned %s\n", e.conn, e.handle, e.flag ? "on" : "off");
      }
      break;
    }
    case EV_SCAN_NOTE:
      out("Ignoring a nearby device '%s' (RSSI %d): it advertises an all-zero Bluetooth address,\n"
          "  so it is not your remote and can't be connected to.\n",
          e.name[0] ? e.name : "(no name)", e.rssi);
      break;
    default:
      break;
  }
}

// ============================================================================
// Housekeeping
// ============================================================================
static void manageAdvertising() {
  // Same rule as the two-host test: visible whenever fewer than 2 devices are connected.
  bool want = (g_pair == PAIR_TV || g_pair == PAIR_CC) || (hostCount() < 2);
  bool active = NimBLEDevice::getAdvertising()->isAdvertising();
  if (want && !active) {
    if (NimBLEDevice::startAdvertising()) {
      g_advFailLogged = false;
    } else if (!g_advFailLogged) {
      out("Advertising could not start yet (will retry)\n");
      g_advFailLogged = true;
    }
  } else if (!want && active) {
    NimBLEDevice::stopAdvertising();
  }
}

static void kickStrays(uint32_t now) {
  // A connection that never identifies itself as the TV or the Chromecast is dropped,
  // so it can't hold a connection slot.
  uint32_t limit = (g_pair == PAIR_TV || g_pair == PAIR_CC) ? 120000 : 60000;
  for (auto& h : g_hosts) {
    if (h.used && h.role == ROLE_NONE && now - h.since > limit && (h.lastKick == 0 || now - h.lastKick > 5000)) {
      out("Dropping an unidentified connection\n");
      g_server->disconnect(h.conn);
      h.lastKick = now;
    }
  }
}

// The two-host test sends a RIGHT key report to every connected device every 3 s, also
// before the device has paired; the TV paired right after the first one. The bridge sends the
// same report to a device that still hasn't paired 3 s after connecting (devices that pair on
// their own are done by then), repeats it every 15 s, and stops as soon as it has paired.
static void nudgeNewHosts(uint32_t now) {
  for (auto& h : g_hosts) {
    if (!h.used || h.enc || h.role != ROLE_NONE || !timeReached(now, h.nextNudge)) continue;
    uint8_t press[8] = {0, 0, 0x4F, 0, 0, 0, 0, 0};  // byte for byte what the two-host test sends
    uint8_t release[8] = {0};
    bool ok = g_kbChr->notify(press, sizeof(press), h.conn);
    delay(60);
    ok = g_kbChr->notify(release, sizeof(release), h.conn) && ok;
    h.nextNudge = millis() + 15000;
    out("  [conn %u] not paired yet: sent the RIGHT key report of the two-host test (%s)\n", h.conn,
        ok ? "ok" : "failed");
  }
}

static void checkTimeouts(uint32_t now) {
  if ((g_pair == PAIR_TV || g_pair == PAIR_CC) && now - g_pairSince > HOST_PAIR_MS) {
    out("%s pairing timed out\n", g_pair == PAIR_TV ? "TV" : "Chromecast");
    g_pair = PAIR_NONE;
  }
  if (g_storeOverflow.exchange(false)) {
    out("WARNING: pairing storage is full - a new pairing was not saved. Use \"forget\" first.\n");
  }
  uint32_t dropped = g_evDropped.load();
  if (dropped != g_lastDroppedShown) {
    out("WARNING: %u events dropped (event queue full)\n", (unsigned)dropped);
    g_lastDroppedShown = dropped;
  }
}

static void erasePairingsAndRestart() {
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::getScan()->stop();
  NimBLEDevice::deleteAllBonds();
  const char* keys[] = {"remote", "remid", "rh", "tv", "cc"};
  for (const char* k : keys) {
    if (g_prefs.isKey(k)) g_prefs.remove(k);
  }
  out("All pairings erased. Restarting...\n");
  delay(500);
  ESP.restart();
}

static void checkBootButton(uint32_t now) {
  if (digitalRead(BOOT_BUTTON_PIN) != LOW) {
    g_bootDownSince = 0;
    return;
  }
  if (g_bootDownSince == 0) {
    g_bootDownSince = now | 1;
    out("Keep holding BOOT for 5 s to erase all pairings...\n");
    return;
  }
  if (now - g_bootDownSince >= 5000) erasePairingsAndRestart();
}

// ============================================================================
// Console
// ============================================================================
static bool parseHex(const char* s, uint32_t& v) {
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  if (*s == 0) return false;
  v = 0;
  for (int n = 0; *s; s++, n++) {
    if (n >= 4) return false;
    char c = *s;
    int d;
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      d = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      d = c - 'A' + 10;
    } else {
      return false;
    }
    v = (v << 4) | (uint32_t)d;
  }
  return true;
}

static bool parseTarget(const char* s, uint16_t key, uint16_t& out) {
  if (strcmp(s, "same") == 0) {
    out = key;
    return out <= MAX_CODE;
  }
  if (strcmp(s, "-") == 0 || strcmp(s, "none") == 0) {
    out = 0;
    return true;
  }
  uint32_t v;
  if (!parseHex(s, v) || v > MAX_CODE) return false;
  out = (uint16_t)v;
  return true;
}

static void printMap() {
  char a[24], b[24], c[24];
  out("Button                       -> TV                  -> Chromecast\n");
  for (uint8_t i = 0; i < g_mapCount; i++) {
    const MapEntry& m = g_map[i];
    char tv[32], cc[32];
    if (m.tv) {
      snprintf(tv, sizeof(tv), "0x%04X %s", m.tv, keyLabel(m.tv, b, sizeof(b)));
    } else {
      snprintf(tv, sizeof(tv), "-");
    }
    if (m.cc) {
      snprintf(cc, sizeof(cc), "0x%04X %s", m.cc, keyLabel(m.cc, c, sizeof(c)));
    } else {
      snprintf(cc, sizeof(cc), "-");
    }
    out("0x%04X %-21s -> %-19s -> %s\n", m.key, keyLabel(m.key, a, sizeof(a)), tv, cc);
  }
  out("Buttons not listed: sent to the TV unchanged. (%u of %u lines used)\n", g_mapCount, MAX_MAP);
}

static void printHostLine(const char* label, const Addr& a, uint8_t role) {
  char t[20];
  const char* st;
  if (!a.set) {
    st = "not paired";
  } else {
    Host* h = hostByRole(role);
    if (!h) {
      st = "not connected";
    } else if (!h->enc) {
      st = "connected, not secured yet";
    } else {
      st = h->sub ? "connected, secured, receiving buttons" : "connected, secured";
    }
  }
  out("%-11s: %-17s  %s\n", label, addrText(a, t, sizeof(t)), st);
}

static void printStatus() {
  char t[20];
  const char* rs = "not paired";
  if (g_remote.set) {
    switch (g_rem) {
      case R_READY: rs = "connected, ready"; break;
      case R_SETUP: rs = "connected, setting up"; break;
      case R_CONNECTING: rs = "waiting for it to wake up (press any button)"; break;
      default: rs = "not connected"; break;
    }
  }
  out("Remote     : %-17s  %s\n", addrText(g_remote, t, sizeof(t)), rs);
  printHostLine("TV", g_tv, ROLE_TV);
  printHostLine("Chromecast", g_cc, ROLE_CC);
  const char* pm[] = {"none", "remote", "TV", "Chromecast"};
  out("Pairing    : %s\n", pm[g_pair]);
  out("Advertising: %s\n", NimBLEDevice::getAdvertising()->isAdvertising() ? "on" : "off");
  out("IDs        : %s\n", g_tclIds ? "TCL (RC902V)" : "neutral");
  char ik[8], sk[8];
  snprintf(ik, sizeof(ik), g_inputKey ? "0x%04X" : "off", g_inputKey);
  snprintf(sk, sizeof(sk), g_switchKey ? "0x%04X" : "off", g_switchKey);
  out("Input key  : %s (TV session %u s)   Switch key: %s (auto-return %u s)\n", ik,
      (unsigned)(g_tvSessionMs / 1000), sk, (unsigned)(g_tvModeMs / 1000));
  out("Now        : %s\n", g_tvMode ? "TV mode" : (g_tvSession ? "TV session" : "normal (table)"));
  out("Pairings saved on the ESP32: %d of %d.  This ESP32: %s\n", NimBLEDevice::getNumBonds(),
      (int)MYNEWT_VAL(BLE_STORE_MAX_BONDS), NimBLEDevice::getAddress().toString().c_str());
}

static void printHelp() {
  out("Commands (codes are hex):\n"
      "  status | map | defaults | help\n"
      "  set <key> <tv> <cc>   <tv>/<cc>: code, same, or -   e.g.  set 0x0221 0x0223 -\n"
      "  del <key>\n"
      "  send <tv|cc> <code>\n"
      "  pair remote|tv|cc     pair stop\n"
      "  forget remote|tv|cc     swap\n"
      "  log on|off\n"
      "  ids tcl|neutral\n"
      "  inputkey <code>|off   switchkey <code>|off   tvsession <s>   tvmode <s> (0 = never)\n"
      "  factory yes\n");
}

static void applyIds() {
  if (g_tclIds) {
    g_hid->setPnp(0x01, 0x0417, 0x0304, 0x0003);  // the RC902V's own IDs
  } else {
    g_hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);  // neutral IDs (as in the two-host test)
  }
}

static void startHostPairing(uint8_t role) {
  if (g_pair == PAIR_REMOTE) {
    out("Remote pairing is running. Finish it or type: pair stop\n");
    return;
  }
  forgetRole(role);  // a new TV/Chromecast replaces the old one
  pruneBonds();
  // Pause the remote link so the radio is in the same state as in the two-host test.
  if (g_rem == R_CONNECTING) g_client->cancelConnect();
  if (g_client->isConnected()) g_client->disconnect();
  g_pair = (role == ROLE_TV) ? PAIR_TV : PAIR_CC;
  g_pairSince = millis();
  out("\n>> %s PAIRING (3 min): on the %s go to Settings > Remotes & Accessories > Pair accessory,\n",
      role == ROLE_TV ? "TV" : "CHROMECAST", roleName(role));
  out("   select \"%s\". (If it was paired before, remove it there first.)\n", DEVICE_NAME);
  out("   The remote is paused until pairing finishes.\n");
}

static void runCommand(char* line) {
  char* tok[5] = {nullptr};
  int n = 0;
  char* save = nullptr;
  for (char* p = strtok_r(line, " \t", &save); p && n < 5; p = strtok_r(nullptr, " \t", &save)) {
    for (char* q = p; *q; q++) *q = (char)tolower((unsigned char)*q);
    tok[n++] = p;
  }
  if (n == 0) return;
  const char* cmd = tok[0];

  if (!strcmp(cmd, "help")) {
    printHelp();
  } else if (!strcmp(cmd, "status")) {
    printStatus();
  } else if (!strcmp(cmd, "map")) {
    printMap();
  } else if (!strcmp(cmd, "set") && n == 4) {
    uint32_t key;
    uint16_t tv, cc;
    if (!parseHex(tok[1], key) || key == 0) {
      out("Bad button code: %s\n", tok[1]);
      return;
    }
    if (!parseTarget(tok[2], key, tv) || !parseTarget(tok[3], key, cc)) {
      out("Bad target. Use a code up to 0x%04X, \"same\" or \"-\"\n", MAX_CODE);
      return;
    }
    int i = findMap((uint16_t)key);
    if (i < 0) {
      if (g_mapCount >= MAX_MAP) {
        out("Table is full\n");
        return;
      }
      i = g_mapCount++;
    }
    g_map[i] = MapEntry{(uint16_t)key, tv, cc};
    saveMap();
    char tvs[8], ccs[8];
    snprintf(tvs, sizeof(tvs), tv ? "0x%04X" : "-", tv);
    snprintf(ccs, sizeof(ccs), cc ? "0x%04X" : "-", cc);
    out("Saved: 0x%04X -> TV %s, Chromecast %s\n", (unsigned)key, tvs, ccs);
  } else if (!strcmp(cmd, "del") && n == 2) {
    uint32_t key;
    int i = parseHex(tok[1], key) ? findMap((uint16_t)key) : -1;
    if (i < 0) {
      out("Not in the table\n");
      return;
    }
    for (uint8_t j = i; j + 1 < g_mapCount; j++) g_map[j] = g_map[j + 1];
    g_mapCount--;
    saveMap();
    out("Removed. 0x%04X now goes to the TV unchanged.\n", (unsigned)key);
  } else if (!strcmp(cmd, "defaults")) {
    loadDefaultMap();
    saveMap();
    out("Default table restored\n");
  } else if (!strcmp(cmd, "send") && n == 3) {
    uint32_t code;
    bool tv = !strcmp(tok[1], "tv");
    if ((!tv && strcmp(tok[1], "cc")) || !parseHex(tok[2], code) || !canSend((uint16_t)code)) {
      out("Use: send tv 0x0045   or   send cc 0x0045\n");
      return;
    }
    uint16_t conn = connForRole(tv ? ROLE_TV : ROLE_CC);
    if (conn == NO_CONN) {
      out("%s not connected\n", tv ? "TV" : "Chromecast");
      return;
    }
    uint8_t len = tv ? TV_REPORT_LEN : CC_REPORT_LEN;
    bool ok = sendCode(conn, (uint16_t)code, len);
    delay(80);
    ok = sendCode(conn, 0, len) && ok;
    out("Sent 0x%04X to the %s: %s\n", (unsigned)code, tv ? "TV" : "Chromecast", ok ? "OK" : "FAILED");
  } else if (!strcmp(cmd, "pair") && n == 2) {
    if (!strcmp(tok[1], "remote")) {
      if (g_pair == PAIR_TV || g_pair == PAIR_CC) g_pair = PAIR_NONE;
      startRemotePairing();
    } else if (!strcmp(tok[1], "tv")) {
      startHostPairing(ROLE_TV);
    } else if (!strcmp(tok[1], "cc")) {
      startHostPairing(ROLE_CC);
    } else if (!strcmp(tok[1], "stop")) {
      if (g_pair == PAIR_REMOTE) {
        stopRemotePairing("cancelled");
      } else {
        g_pair = PAIR_NONE;
        out("Pairing stopped\n");
      }
    } else {
      out("Use: pair remote | pair tv | pair cc | pair stop\n");
    }
  } else if (!strcmp(cmd, "forget") && n == 2) {
    if (!strcmp(tok[1], "tv")) {
      forgetRole(ROLE_TV);
      out("TV pairing deleted. Also remove \"%s\" on the TV.\n", DEVICE_NAME);
    } else if (!strcmp(tok[1], "cc")) {
      forgetRole(ROLE_CC);
      out("Chromecast pairing deleted. Also remove \"%s\" on the Chromecast.\n", DEVICE_NAME);
    } else if (!strcmp(tok[1], "remote")) {
      if (g_pair == PAIR_REMOTE) {
        out("Remote pairing is running. Type: pair stop\n");
        return;
      }
      if (g_rem == R_CONNECTING) g_client->cancelConnect();
      forgetRemoteRecords();  // deleting the pairing also disconnects it
      g_rem = R_NONE;
      out("Remote pairing deleted. Type \"pair remote\" to pair one.\n");
    } else {
      out("Use: forget remote | forget tv | forget cc\n");
    }
  } else if (!strcmp(cmd, "ids") && n == 2 && (!strcmp(tok[1], "tcl") || !strcmp(tok[1], "neutral"))) {
    g_tclIds = !strcmp(tok[1], "tcl");
    g_prefs.putUChar("ids", g_tclIds ? 1 : 0);
    applyIds();
    out("IDs set to %s and saved. Devices read them when they pair, so pair the TV after this.\n",
        g_tclIds ? "TCL (RC902V)" : "neutral");
  } else if ((!strcmp(cmd, "inputkey") || !strcmp(cmd, "switchkey")) && n == 2) {
    const bool isInput = !strcmp(cmd, "inputkey");
    uint32_t code = 0;
    if (strcmp(tok[1], "off") != 0) {
      if (!parseHex(tok[1], code) || code == 0) {
        out("Use a button code (press the button to see it) or \"off\"\n");
        return;
      }
      if (isNavKey((uint16_t)code)) {
        out("That is an arrow, OK or Back - pick another button\n");
        return;
      }
      if (code == (isInput ? g_switchKey : g_inputKey)) {
        out("That button is already the %s button\n", isInput ? "switch" : "Input");
        return;
      }
    }
    if (isInput) {
      g_inputKey = (uint16_t)code;
      g_prefs.putUShort("ik", g_inputKey);
      g_tvSession = false;
    } else {
      g_switchKey = (uint16_t)code;
      g_prefs.putUShort("sk", g_switchKey);
      g_tvMode = false;
    }
    if (code) {
      out("Saved: %s button = 0x%04X\n", isInput ? "Input" : "switch", (unsigned)code);
    } else {
      out("Saved: %s button off\n", isInput ? "Input" : "switch");
    }
  } else if ((!strcmp(cmd, "tvsession") || !strcmp(cmd, "tvmode")) && n == 2) {
    const bool isSession = !strcmp(cmd, "tvsession");
    char* end = nullptr;
    long v = strtol(tok[1], &end, 10);
    const long lo = isSession ? 1 : 0, hi = isSession ? 60 : 3600;
    if (end == tok[1] || *end != 0 || v < lo || v > hi) {
      out("Use whole seconds from %ld to %ld\n", lo, hi);
      return;
    }
    if (isSession) {
      g_tvSessionMs = (uint32_t)v * 1000;
      g_prefs.putULong("tss", g_tvSessionMs);
      out("Saved: TV session ends after %ld s without arrow/OK/Back\n", v);
    } else {
      g_tvModeMs = (uint32_t)v * 1000;
      g_prefs.putULong("tms", g_tvModeMs);
      if (v) {
        out("Saved: TV mode returns to normal after %ld s without presses\n", v);
      } else {
        out("Saved: TV mode stays until the switch button is pressed again\n");
      }
    }
  } else if (!strcmp(cmd, "swap")) {
    Addr tmp = g_tv;
    g_tv = g_cc;
    g_cc = tmp;
    saveAddr("tv", g_tv);
    saveAddr("cc", g_cc);
    for (auto& h : g_hosts) {
      if (h.used && h.role == ROLE_TV) {
        h.role = ROLE_CC;
      } else if (h.used && h.role == ROLE_CC) {
        h.role = ROLE_TV;
      }
    }
    out("TV and Chromecast swapped.\n");
    printStatus();
  } else if (!strcmp(cmd, "log") && n == 2) {
    g_logKeys = !strcmp(tok[1], "on");
    out("Button log %s\n", g_logKeys ? "on" : "off");
  } else if (!strcmp(cmd, "factory") && n == 2 && !strcmp(tok[1], "yes")) {
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::deleteAllBonds();
    g_prefs.clear();
    out("Everything erased. Restarting...\n");
    delay(500);
    ESP.restart();
  } else {
    out("Unknown command. Type: help\n");
  }
}

static void readConsole() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\r' || ch == '\n') {
      if (g_lineLen) {
        g_line[g_lineLen] = 0;
        g_lineLen = 0;
        runCommand(g_line);
      }
    } else if (g_lineLen < sizeof(g_line) - 1) {
      g_line[g_lineLen++] = (char)ch;
    }
  }
}

// ============================================================================
// setup / loop
// ============================================================================
void setup() {
  g_bootMs = millis();
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxBufferSize(2048);
#endif
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);  // printing must never stall the bridge (e.g. USB plugged in, monitor closed)
#endif
  delay(1500);  // time for the USB serial port to open
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  g_evQ = xQueueCreate(48, sizeof(Event));
  g_setupQ = xQueueCreate(2, sizeof(SetupRequest));
  for (auto& h : g_hosts) h = Host{false, NO_CONN, ROLE_NONE, false, false, 0, 0, 0, -1};

  g_prefs.begin("rcbridge", false);
  const bool firstRun = !g_prefs.isKey("init");
  loadAddr("remote", g_remote);
  loadAddr("remid", g_remoteId);
  loadAddr("tv", g_tv);
  loadAddr("cc", g_cc);
  loadHandles();
  loadMap();
  if (g_prefs.isKey("ids")) g_tclIds = g_prefs.getUChar("ids", 1) != 0;
  if (g_prefs.isKey("ik")) g_inputKey = g_prefs.getUShort("ik", 0);
  if (g_prefs.isKey("sk")) g_switchKey = g_prefs.getUShort("sk", 0);
  if (g_prefs.isKey("tss")) g_tvSessionMs = g_prefs.getULong("tss", 5000);
  if (g_prefs.isKey("tms")) g_tvModeMs = g_prefs.getULong("tms", 60000);
  bool hidChanged = false;
  if (!firstRun && g_prefs.getUChar("hidv", 0) != HID_LAYOUT_VERSION) {
    // The TV and Chromecast store the old profile; their pairings must be redone.
    g_tv.set = false;
    g_cc.set = false;
    saveAddr("tv", g_tv);
    saveAddr("cc", g_cc);
    if (g_prefs.isKey("ids")) g_prefs.remove("ids");
    g_tclIds = (CLONE_RC902V_IDS != 0);
    hidChanged = true;
  }
  g_prefs.putUChar("hidv", HID_LAYOUT_VERSION);

  out("\n=== BLE REMOTE BRIDGE v0.9.0 ===  %s + bridge fix  (storage: %d pairings, %d subscriptions)\n",
      NimBLEDevice::getVersion(), (int)MYNEWT_VAL(BLE_STORE_MAX_BONDS), (int)MYNEWT_VAL(BLE_STORE_MAX_CCCDS));
  if (hidChanged) {
    out("Bluetooth profile changed. TV and Chromecast pairings were reset:\n"
        "on BOTH, remove \"%s\" from Remotes & Accessories, then pair them again.\n", DEVICE_NAME);
  }

  NimBLEDevice::setDeviceCallbacks(&g_deviceCb);
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM, secure connections (as in the test sketches)
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  if (firstRun) {
    NimBLEDevice::deleteAllBonds();
    g_prefs.putUChar("init", 1);
    out("First start: old pairings on the ESP32 erased.\n");
  }
  validateRoles();
  pruneBonds();
  NimBLEDevice::setCustomGapHandler(gapListener);

  // ---- TV / Chromecast side: the same calls, in the same order, as the two-host test ----
  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(&g_serverCb);
  g_hid = new NimBLEHIDDevice(g_server);
#if KEYBOARD_ONLY_LIKE_TEST
  g_inputChr = g_hid->getInputReport(1);
  g_kbChr = g_inputChr;
#else
  g_kbChr = g_hid->getInputReport(1);
  g_inputChr = g_hid->getInputReport(REPORT_ID);  // consumer report (ID 3): buttons are sent here
#endif
  g_hid->setManufacturer("DIY");
  applyIds();  // default: setPnp(0x02, 0xE502, 0xA111, 0x0210), as in the two-host test
  g_hid->setHidInfo(0x00, 0x01);
  g_hid->setReportMap((uint8_t*)BRIDGE_REPORT_MAP, sizeof(BRIDGE_REPORT_MAP));
  g_hid->startServices();  // empty in NimBLE-Arduino 2.5.1 (deprecation warning); kept because the two-host test calls it
  g_hid->setBatteryLevel(100);
  g_inputChr->setCallbacks(&g_inputCb);  // only tells the sketch about subscriptions; nothing the TV sees

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(g_hid->getHidService()->getUUID());
  adv->setName(DEVICE_NAME);
  adv->start();  // starts the GATT server too, exactly as in the two-host test

  // ---- Remote side ----
  g_client = NimBLEDevice::createClient();
  g_client->setClientCallbacks(&g_clientCb, false);
  g_client->setSelfDelete(false, false);
  // Library-default connection parameters (as in tools/remote_sniffer); while waiting for the remote,
  // listen 30 ms out of every 60 ms so the radio stays free for the TV/Chromecast links.
  g_client->setConnectionParams(BLE_GAP_INITIAL_CONN_ITVL_MIN, BLE_GAP_INITIAL_CONN_ITVL_MAX,
                                BLE_GAP_INITIAL_CONN_LATENCY, BLE_GAP_INITIAL_SUPERVISION_TIMEOUT, 96, 48);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCb, true);  // report repeats: a remote first seen too far away is re-checked
  scan->setMaxResults(0);                   // callbacks only, nothing stored
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(100);

  xTaskCreate(setupTask, "rcSetup", 8192, nullptr, 1, nullptr);

  printStatus();
  out("Type \"help\" for commands.\n");
  if (!g_remote.set) {
    startRemotePairing();
  } else {
    g_rem = R_IDLE;
  }
}

void loop() {
  Event e;
  if (xQueueReceive(g_evQ, &e, pdMS_TO_TICKS(5)) == pdTRUE) {
    handleEvent(e);
    for (int i = 0; i < 16 && xQueueReceive(g_evQ, &e, 0) == pdTRUE; i++) handleEvent(e);
  }
  const uint32_t now = millis();
  manageRemote(now);
  if (now - g_lastHousekeeping >= 100) {
    g_lastHousekeeping = now;
    manageAdvertising();
    checkTvTimers(now);
    nudgeNewHosts(now);
    kickStrays(now);
    checkTimeouts(now);
    checkBootButton(now);
  }
  readConsole();
}
