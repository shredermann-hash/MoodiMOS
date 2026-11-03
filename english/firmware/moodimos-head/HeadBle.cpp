#include "HeadBle.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>

namespace HeadBle {

namespace {
BLEServer* server = nullptr;
BLECharacteristic* statusChar = nullptr;
BLEAdvertising* adv = nullptr;
bool started = false, advertising = false;
volatile bool ringFlag = false;
volatile uint8_t ringValue = 0;
volatile bool isConnected = false;

class RingWrite : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    auto v = c->getValue();
    if (v.length() != 1) return;
    ringValue = uint8_t(v[0]);
    ringFlag = true;
  }
};

class Connections : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { isConnected = true; }
  void onDisconnect(BLEServer*) override {
    isConnected = false;
    if (advertising) BLEDevice::startAdvertising();   // The connection announcement ends.
  }
};

void applyAdvert(const MoodiHeadStatus& s) {
  uint8_t md[MOODI_ADV_LEN];
  moodiHeadAdvEncode(s, md);
  BLEAdvertisementData data;
  data.setFlags(0x06);                                  // discoverable, BLE only
  data.setCompleteServices(BLEUUID(MOODI_HEAD_SERVICE_UUID));
  data.setManufacturerData(String((const char*)md, sizeof(md)));
  adv->setAdvertisementData(data);
  BLEAdvertisementData scan;
  scan.setName(MOODI_HEAD_NAME);
  adv->setScanResponseData(scan);
}
}  // <source> namespace </source>

void begin() {
  if (started) return;
  BLEDevice::init(MOODI_HEAD_NAME);
  server = BLEDevice::createServer();
  server->setCallbacks(new Connections());
  BLEService* svc = server->createService(MOODI_HEAD_SERVICE_UUID);
  BLECharacteristic* ring = svc->createCharacteristic(MOODI_HEAD_RING_UUID, BLECharacteristic::PROPERTY_WRITE);
  ring->setCallbacks(new RingWrite());
  statusChar = svc->createCharacteristic(MOODI_HEAD_STATUS_UUID,
                                         BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());
  svc->start();
  adv = BLEDevice::getAdvertising();
  applyAdvert({MOODI_UNKNOWN, MOODI_UNKNOWN, 0});
  started = true;
}

void setAdvertising(bool on) {
  if (!started || on == advertising) return;
  advertising = on;
  if (on) BLEDevice::startAdvertising();
  else BLEDevice::stopAdvertising();
}

void setStatus(const MoodiHeadStatus& s) {
  if (!started) return;
  statusChar->setValue((uint8_t*)&s, sizeof(s));
  if (server->getConnectedCount() > 0) statusChar->notify();
  applyAdvert(s);
}

bool takeRing(uint8_t& tenths) {
  if (!ringFlag) return false;
  ringFlag = false;
  tenths = ringValue;
  return true;
}

bool connected() { return isConnected; }

}  // <source> namespace HeadBle </source>
