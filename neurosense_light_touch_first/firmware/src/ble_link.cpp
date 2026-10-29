#include "ble_link.h"
#include <NimBLEDevice.h>
#include "config.h"
#include "app.h"
#include "debug.h"

static char devName[12];                       // "NS-" + 4 hex + NUL
static NimBLEServer* server = nullptr;
static NimBLECharacteristic* chr = nullptr;
static volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;

// Callbacks run on the NimBLE host task: post an event, never touch app state.
class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
    connHandle = ci.getConnHandle();
    s->updateConnParams(connHandle, CONN_MIN, CONN_MAX, CONN_LATENCY, CONN_TIMEOUT);
    app_post(EV_BLE_CONNECT, 0);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    app_post(EV_BLE_DISCONNECT, 0);
  }
};

class ChrCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    app_post(EV_BLE_SUBSCRIBE, subValue ? 1 : 0);
  }
};

static ServerCb serverCb;   // static objects: the stack must not delete them
static ChrCb chrCb;

const char* ble_name() { return devName; }

bool ble_init() {
  // Last 2 bytes of the factory MAC (eFuse): unique per board, stable across reflashes.
  uint64_t mac = ESP.getEfuseMac();            // first MAC byte is the lowest byte
  snprintf(devName, sizeof devName, NS_NAME_PREFIX "%02X%02X", (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));

  NimBLEDevice::init(devName);
  NimBLEDevice::setPower(BLE_TX_DBM);

  server = NimBLEDevice::createServer();
  if (!server) return false;
  server->setCallbacks(&serverCb, false);
  server->advertiseOnDisconnect(false);        // the app task restarts advertising itself

  NimBLEService* svc = server->createService(NS_SERVICE_UUID);
  if (!svc) return false;
  chr = svc->createCharacteristic(NS_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);
  if (!chr) return false;
  chr->setCallbacks(&chrCb);
  if (!server->start()) return false;

  // The 128-bit service UUID goes in the advertisement; the name in the scan response.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad, sr;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.addServiceUUID(NS_SERVICE_UUID);
  sr.setName(devName);
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  adv->enableScanResponse(true);

  DBG_LOG("ble", "init ok, name %s", devName);
  return true;
}

void ble_start_advertising(bool fast) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  adv->setMinInterval(fast ? ADV_FAST_MIN : ADV_SLOW_MIN);
  adv->setMaxInterval(fast ? ADV_FAST_MAX : ADV_SLOW_MAX);
  bool ok = adv->start();
  DBG_INC(advRestarts);
  DBG_LOG("ble", "advertising %s %s", fast ? "fast" : "slow", ok ? "ok" : "FAILED");
}

bool ble_notify(const char* cmd) {
  if (!chr || !server || server->getConnectedCount() == 0) return false;
  chr->setValue((const uint8_t*)cmd, strlen(cmd));
  bool ok = chr->notify();
  if (ok) DBG_INC(notifyOk); else DBG_INC(notifyFail);
  return ok;
}

void ble_disconnect() {
  if (server && connHandle != BLE_HS_CONN_HANDLE_NONE) server->disconnect(connHandle);
}

void ble_stop() {
  NimBLEDevice::getAdvertising()->stop();
  ble_disconnect();
  NimBLEDevice::deinit(true);
  server = nullptr;
  chr = nullptr;
}
