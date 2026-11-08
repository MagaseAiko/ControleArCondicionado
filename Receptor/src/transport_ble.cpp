#ifdef TRANSPORT_BLE
#include "transport.h"

#include <NimBLEDevice.h>
#include <deque>
#include <string>
#include "config.h"

// Mesmos UUIDs usados na página (docs/index.html)
#define BLE_SERVICE_UUID "c9b8f500-ac00-4c0d-9e5a-2b1d4e000001"
#define BLE_RX_UUID "c9b8f500-ac00-4c0d-9e5a-2b1d4e000002"  // celular -> ESP (write)
#define BLE_TX_UUID "c9b8f500-ac00-4c0d-9e5a-2b1d4e000003"  // ESP -> celular (notify)

namespace {

MessageHandler handler = nullptr;
NimBLECharacteristic *txChar = nullptr;
volatile bool connected = false;
volatile uint16_t mtu = 23;

// Os callbacks do NimBLE rodam em outra task: as linhas recebidas vão para
// uma fila protegida e são tratadas no loop().
SemaphoreHandle_t inboxLock = nullptr;
std::string rxBuffer;
std::deque<std::string> inbox;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server) override {
    connected = true;
    Serial.println("BLE: celular conectado");
  }
  void onDisconnect(NimBLEServer *server) override {
    connected = false;
    mtu = 23;
    Serial.println("BLE: celular desconectado");
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t value, ble_gap_conn_desc *desc) override { mtu = value; }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    const std::string value = c->getValue();
    xSemaphoreTake(inboxLock, portMAX_DELAY);
    rxBuffer += value;
    size_t nl;
    while ((nl = rxBuffer.find('\n')) != std::string::npos) {
      if (nl > 0) inbox.push_back(rxBuffer.substr(0, nl));
      rxBuffer.erase(0, nl + 1);
    }
    if (rxBuffer.size() > 16384) rxBuffer.clear();  // lixo sem quebra de linha
    xSemaphoreGive(inboxLock);
  }
};

}  // namespace

void transportBegin(MessageHandler h) {
  handler = h;
  inboxLock = xSemaphoreCreateMutex();

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *service = server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *rx =
      service->createCharacteristic(BLE_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  txChar = service->createCharacteristic(BLE_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  service->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);  // nome vai na resposta de scan
  adv->start();
  Serial.printf("BLE anunciando como \"%s\"\n", DEVICE_NAME);
}

void transportLoop() {
  while (true) {
    std::string line;
    xSemaphoreTake(inboxLock, portMAX_DELAY);
    if (!inbox.empty()) {
      line = inbox.front();
      inbox.pop_front();
    }
    xSemaphoreGive(inboxLock);
    if (line.empty()) break;
    if (handler) handler(String(line.c_str()));
  }
}

void transportSend(const String &json) {
  if (!connected || !txChar) return;
  const String data = json + "\n";
  size_t chunk = mtu > 23 ? mtu - 3 : 20;
  if (chunk > 244) chunk = 244;
  for (size_t i = 0; i < data.length(); i += chunk) {
    const size_t n = std::min(chunk, data.length() - i);
    txChar->setValue((const uint8_t *)data.c_str() + i, n);
    txChar->notify();
    delay(10);  // dá tempo da pilha BLE esvaziar o buffer
  }
}

bool transportConnected() { return connected; }

const char *transportName() { return "ble"; }

#endif  // TRANSPORT_BLE
