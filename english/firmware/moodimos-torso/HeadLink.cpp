#include "HeadLink.h"
#include <Arduino.h>
#include <BLEDevice.h>

namespace HeadLink {

namespace {
const BLEUUID SERVICE_UUID(MOODI_HEAD_SERVICE_UUID);
const BLEUUID RING_UUID(MOODI_HEAD_RING_UUID);

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
HeadFinder finder;
volatile bool active = false;
BLEClient* client = nullptr;

bool sendRing(BLEAdvertisedDevice& dev, uint8_t tenths) {
  if (!client->connect(&dev)) return false;
  BLERemoteService* svc = client->getService(SERVICE_UUID);
  BLERemoteCharacteristic* ch = svc ? svc->getCharacteristic(RING_UUID) : nullptr;
  bool ok = ch && ch->canWrite();
  if (ok) ch->writeValue(&tenths, 1, true);
  client->disconnect();
  return ok;
}

// The deep sleep head state triggers every 1.5 s out of every 30 s—stop scanning immediately upon its first announcement to connect while it’s still listening (not 2 s later).
BLEAdvertisedDevice* headDev = nullptr;
volatile bool headFound = false;

class ScanEvents : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (headFound || !dev.haveServiceUUID() || !dev.isAdvertisingService(SERVICE_UUID)) return;
    delete headDev;
    headDev = new BLEAdvertisedDevice(dev);
    headFound = true;
    BLEDevice::getScan()->stop();
  }
};

void task(void*) {
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanEvents());
  scan->setActiveScan(true);
  for (;;) {
    if (!active) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    headFound = false;
    scan->start(2, false);   // Give hand upon first detection of head.
    if (headFound && headDev) {
      BLEAdvertisedDevice& dev = *headDev;
      MoodiHeadStatus st;
      if (dev.haveManufacturerData()) {
        auto md = dev.getManufacturerData();
        if (moodiHeadAdvDecode((const uint8_t*)md.c_str(), md.length(), st)) {
          portENTER_CRITICAL(&lock);
          finder.seen(st, millis());
          portEXIT_CRITICAL(&lock);
        }
      }
      portENTER_CRITICAL(&lock);
      bool want = finder.wantsRing(millis());
      uint8_t tenths = finder.ringTenths();
      portEXIT_CRITICAL(&lock);
      if (want && sendRing(dev, tenths)) {
        portENTER_CRITICAL(&lock);
        finder.ringSent(millis());
        portEXIT_CRITICAL(&lock);
      }
    }
    scan->clearResults();
    // Alert bell pending: continuously sweep to capture the 30-second head wake-up trigger.
    portENTER_CRITICAL(&lock);
    bool urgent = finder.wantsRing(millis());
    portEXIT_CRITICAL(&lock);
    vTaskDelay(pdMS_TO_TICKS(urgent ? 50 : 5000));
  }
}
}  // <source> namespace </source>

void begin() {
  BLEDevice::init("MoodiMOS-TORSE");
  client = BLEDevice::createClient();
  xTaskCreatePinnedToCore(task, "headlink", 6144, nullptr, 1, nullptr, 0);
}

void setActive(bool headAway) {
  if (active && !headAway) {
    portENTER_CRITICAL(&lock);
    finder.forget();
    portEXIT_CRITICAL(&lock);
  }
  active = headAway;
}

bool status(MoodiHeadStatus& s, uint32_t& ageMs) {
  portENTER_CRITICAL(&lock);
  bool ok = finder.status(millis(), s, ageMs);
  portEXIT_CRITICAL(&lock);
  return ok;
}

void ring(uint8_t tenths) {
  portENTER_CRITICAL(&lock);
  finder.requestRing(tenths, millis());
  portEXIT_CRITICAL(&lock);
}

HeadFinder::Ring ringState() {
  portENTER_CRITICAL(&lock);
  HeadFinder::Ring r = finder.ring(millis());
  portEXIT_CRITICAL(&lock);
  return r;
}

}  // <source> namespace HeadLink </source>
