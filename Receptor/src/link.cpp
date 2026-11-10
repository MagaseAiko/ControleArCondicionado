#include "link.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "config.h"

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#error "Use platform espressif32@6.x (Arduino core 2.x): IRremoteESP8266 ainda nao suporta o core 3."
#endif

namespace {

struct RxPacket {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

QueueHandle_t rxQueue = nullptr;
volatile bool sendDone = false;
volatile bool sendOk = false;

Preferences prefs;
wifi_interface_t iface = WIFI_IF_STA;
uint8_t emitter[6];
bool paired = false;
uint32_t lastSeen = 0;
bool seen = false;
char emitterFw[11] = "";
uint16_t nextSeq = 1;
LinkResultCallback resultCb = nullptr;

uint16_t newSeq() {
  if (++nextSeq == 0) nextSeq = 1;
  return nextSeq;
}

void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!rxQueue || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) return;
  RxPacket p;
  memcpy(p.mac, mac, 6);
  p.len = len;
  memcpy(p.data, data, len);
  xQueueSend(rxQueue, &p, 0);
}

void onSent(const uint8_t *, esp_now_send_status_t status) {
  sendOk = status == ESP_NOW_SEND_SUCCESS;
  sendDone = true;
}

bool ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = LINK_CHANNEL;
  peer.ifidx = iface;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// Envia e espera a confirmação de entrega do ESP-NOW (com uma nova tentativa).
bool sendPacket(const uint8_t *mac, const void *data, size_t len) {
  if (!ensurePeer(mac)) return false;
  for (int attempt = 0; attempt < 2; attempt++) {
    sendDone = false;
    sendOk = false;
    if (esp_now_send(mac, (const uint8_t *)data, len) != ESP_OK) continue;
    const uint32_t start = millis();
    while (!sendDone && millis() - start < 120) delay(1);
    if (sendDone && sendOk) return true;
  }
  return false;
}

void sendHelloAck() {
  LinkHeader h;
  linkInitHeader(h, LINK_HELLO_ACK, newSeq());
  sendPacket(emitter, &h, sizeof(h));
}

void handlePacket(const RxPacket &p) {
  if (!linkValid(p.data, p.len)) return;
  LinkHeader h;
  memcpy(&h, p.data, sizeof(h));

  if (h.type == LINK_HELLO) {
    if (!paired) {
      // Sem emissor salvo: pareia com o primeiro que aparecer
      memcpy(emitter, p.mac, 6);
      paired = true;
      prefs.putBytes("mac", emitter, 6);
      Serial.printf("Emissor pareado: %s\n", linkEmitterMac().c_str());
    }
    if (memcmp(emitter, p.mac, 6) != 0) return;
    lastSeen = millis();
    seen = true;
    bool emitterPaired = false;
    if (p.len >= (int)sizeof(LinkHello)) {
      LinkHello hello;
      memcpy(&hello, p.data, sizeof(hello));
      emitterPaired = hello.paired;
      memcpy(emitterFw, hello.fw, sizeof(hello.fw));
      emitterFw[sizeof(emitterFw) - 1] = 0;
    }
    if (!emitterPaired) sendHelloAck();
    return;
  }

  if (h.type == LINK_RESULT && paired && memcmp(emitter, p.mac, 6) == 0 && p.len >= (int)sizeof(LinkResult)) {
    LinkResult r;
    memcpy(&r, p.data, sizeof(r));
    lastSeen = millis();
    seen = true;
    if (resultCb) resultCb(r.ackSeq, r.code);
  }
}

}  // namespace

void linkBegin() {
  rxQueue = xQueueCreate(16, sizeof(RxPacket));

  if (WiFi.getMode() & WIFI_MODE_AP) {
    // Modo Wi-Fi: o AP já foi criado no canal LINK_CHANNEL
    iface = WIFI_IF_AP;
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    iface = WIFI_IF_STA;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Falha ao iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);

  prefs.begin("link", false);
  paired = prefs.getBytes("mac", emitter, 6) == 6;
  Serial.printf("ESP-NOW pronto (canal %d). Emissor salvo: %s\n", LINK_CHANNEL,
                paired ? linkEmitterMac().c_str() : "nenhum");
}

void linkLoop() {
  RxPacket p;
  while (rxQueue && xQueueReceive(rxQueue, &p, 0) == pdTRUE) handlePacket(p);
}

void linkOnResult(LinkResultCallback cb) { resultCb = cb; }

uint16_t linkSendCode(int16_t protocol, uint16_t bits, bool isState, const uint8_t *data, uint8_t nbytes) {
  if (!paired || nbytes > LINK_MAX_STATE) return 0;
  LinkCode m;
  const uint16_t seq = newSeq();
  linkInitHeader(m.h, LINK_CODE, seq);
  m.protocol = protocol;
  m.bits = bits;
  m.isState = isState;
  m.nbytes = nbytes;
  memcpy(m.data, data, nbytes);
  return sendPacket(emitter, &m, offsetof(LinkCode, data) + nbytes) ? seq : 0;
}

uint16_t linkSendRaw(const uint16_t *timings, uint16_t count, uint8_t khz, bool sequence) {
  if (!paired || count == 0 || count > LINK_MAX_RAW) return 0;
  const uint16_t seq = newSeq();
  for (uint16_t offset = 0; offset < count;) {
    LinkRaw m;
    linkInitHeader(m.h, sequence ? LINK_RAW_SEQ : LINK_RAW, seq);
    const uint8_t n = min<uint16_t>(LINK_RAW_PER_CHUNK, count - offset);
    m.total = count;
    m.offset = offset;
    m.count = n;
    m.khz = khz;
    memcpy(m.t, timings + offset, n * sizeof(uint16_t));
    if (!sendPacket(emitter, &m, offsetof(LinkRaw, t) + n * sizeof(uint16_t))) return 0;
    offset += n;
  }
  return seq;
}

uint16_t linkPing() {
  if (!paired) return 0;
  LinkHeader h;
  const uint16_t seq = newSeq();
  linkInitHeader(h, LINK_PING, seq);
  return sendPacket(emitter, &h, sizeof(h)) ? seq : 0;
}

bool linkPaired() { return paired; }

bool linkEmitterOnline() { return paired && seen && millis() - lastSeen < EMITTER_TIMEOUT_MS; }

String linkEmitterMac() {
  if (!paired) return "";
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", emitter[0], emitter[1], emitter[2], emitter[3],
           emitter[4], emitter[5]);
  return buf;
}

String linkEmitterFw() { return emitterFw; }

void linkUnpair() {
  if (paired) esp_now_del_peer(emitter);
  paired = false;
  seen = false;
  emitterFw[0] = 0;
  prefs.remove("mac");
}

const char *linkErrorText(uint8_t code) {
  switch (code) {
    case LINK_OK: return "";
    case LINK_ERR_UNSUPPORTED: return "protocolo nao suportado pelo emissor";
    case LINK_ERR_BAD_PACKET: return "pacote invalido";
    case LINK_ERR_RAW_SEQUENCE: return "sinal chegou incompleto";
    case LINK_ERR_TOO_LONG: return "sinal longo demais";
    default: return "erro desconhecido";
  }
}
