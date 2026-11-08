// Emissor (ESP-01): recebe comandos do Receptor por ESP-NOW e envia pelo LED IR.
// Toda a lógica fica no Receptor; aqui só traduzimos pacotes em pulsos IR.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include "link_protocol.h"

#ifndef IR_LED_INVERTED
#define IR_LED_INVERTED 0  // 1 = transistor PNP (LED acende com o pino em 0)
#endif

#ifndef IR_LED_PIN
#define IR_LED_PIN 3
#endif

#define FW_VERSION "2.0.0"
#define HELLO_INTERVAL_MS 3000
#define RX_QUEUE_LEN 12

static uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static IRsend irsend(IR_LED_PIN, IR_LED_INVERTED);

// Fila de pacotes recebidos (o callback do ESP-NOW só copia; o loop processa)
struct RxPacket {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[250];
};
static RxPacket rxQueue[RX_QUEUE_LEN];
static volatile uint8_t rxHead = 0;
static volatile uint8_t rxTail = 0;

// Remontagem de sinais brutos que chegam em pedaços
static uint16_t rawBuf[LINK_MAX_RAW];
static uint16_t rawTotal = 0;
static uint16_t rawReceived = 0;
static uint16_t rawSeq = 0;
static uint8_t rawKhz = 38;

static bool paired = false;
static uint32_t lastHello = 0;
static uint16_t helloSeq = 0;

static void logMsg(const char *msg) {
#if IR_LED_PIN != 1
  Serial.println(msg);
#endif
}

static void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  uint8_t next = (rxHead + 1) % RX_QUEUE_LEN;
  if (next == rxTail || len > sizeof(rxQueue[0].data)) return;  // fila cheia
  RxPacket &p = rxQueue[rxHead];
  memcpy(p.mac, mac, 6);
  p.len = len;
  memcpy(p.data, data, len);
  rxHead = next;
}

static void ensurePeer(uint8_t *mac) {
  if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, LINK_CHANNEL, NULL, 0);
}

static void reply(uint8_t *mac, uint16_t ackSeq, uint8_t code) {
  LinkResult r;
  linkInitHeader(r.h, LINK_RESULT, ackSeq);
  r.ackSeq = ackSeq;
  r.code = code;
  ensurePeer(mac);
  esp_now_send(mac, (uint8_t *)&r, sizeof(r));
}

static void sendHello() {
  LinkHello m;
  memset(&m, 0, sizeof(m));
  linkInitHeader(m.h, LINK_HELLO, ++helloSeq);
  m.paired = paired;
  strncpy(m.fw, FW_VERSION, sizeof(m.fw) - 1);
  esp_now_send(BROADCAST, (uint8_t *)&m, sizeof(m));
}

static void handleCode(RxPacket &p, uint16_t seq) {
  const size_t hdr = offsetof(LinkCode, data);
  if (p.len < hdr) return reply(p.mac, seq, LINK_ERR_BAD_PACKET);
  LinkCode m;
  memset(&m, 0, sizeof(m));
  memcpy(&m, p.data, min((size_t)p.len, sizeof(m)));
  if (m.nbytes > LINK_MAX_STATE || p.len < hdr + m.nbytes) return reply(p.mac, seq, LINK_ERR_BAD_PACKET);

  const decode_type_t type = (decode_type_t)m.protocol;
  bool ok;
  if (m.isState) {
    ok = irsend.send(type, m.data, m.nbytes);
  } else {
    uint64_t value = 0;
    for (int i = 7; i >= 0; i--) value = (value << 8) | m.data[i];
    ok = irsend.send(type, value, m.bits);
  }
  logMsg(ok ? "Codigo enviado" : "Codigo: protocolo nao suportado");
  reply(p.mac, seq, ok ? LINK_OK : LINK_ERR_UNSUPPORTED);
}

static void handleRaw(RxPacket &p, uint16_t seq) {
  const size_t hdr = offsetof(LinkRaw, t);
  if (p.len < hdr) return reply(p.mac, seq, LINK_ERR_BAD_PACKET);
  LinkRaw m;
  memcpy(&m, p.data, min((size_t)p.len, sizeof(m)));

  if (m.total == 0 || m.total > LINK_MAX_RAW) return reply(p.mac, seq, LINK_ERR_TOO_LONG);
  if (m.offset == 0) {
    rawSeq = seq;
    rawTotal = m.total;
    rawReceived = 0;
    rawKhz = m.khz ? m.khz : 38;
  }
  if (rawTotal == 0) return;  // sequência já descartada: responde só uma vez
  if (seq != rawSeq || m.offset != rawReceived || m.offset + m.count > rawTotal ||
      p.len < hdr + m.count * sizeof(uint16_t)) {
    rawTotal = 0;
    return reply(p.mac, seq, LINK_ERR_RAW_SEQUENCE);
  }
  memcpy(rawBuf + m.offset, m.t, m.count * sizeof(uint16_t));
  rawReceived += m.count;

  if (rawReceived == rawTotal) {
    irsend.sendRaw(rawBuf, rawTotal, rawKhz);
    rawTotal = 0;
    logMsg("Sinal bruto enviado");
    reply(p.mac, seq, LINK_OK);
  }
}

static void handlePacket(RxPacket &p) {
  if (!linkValid(p.data, p.len)) return;
  LinkHeader h;
  memcpy(&h, p.data, sizeof(h));
  switch (h.type) {
    case LINK_HELLO_ACK:
      if (!paired) logMsg("Pareado com o receptor");
      paired = true;
      break;
    case LINK_PING: reply(p.mac, h.seq, LINK_OK); break;
    case LINK_CODE: handleCode(p, h.seq); break;
    case LINK_RAW: handleRaw(p, h.seq); break;
  }
}

void setup() {
#if IR_LED_PIN != 1
  // TX apenas: o RX (GPIO3) fica livre para o LED IR
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
#endif
  irsend.begin();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  wifi_set_channel(LINK_CHANNEL);

  if (esp_now_init() != 0) {
    logMsg("Falha no ESP-NOW, reiniciando");
    delay(1000);
    ESP.restart();
  }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onRecv);
  esp_now_add_peer(BROADCAST, ESP_NOW_ROLE_COMBO, LINK_CHANNEL, NULL, 0);

  logMsg("Emissor pronto");
}

void loop() {
  while (rxTail != rxHead) {
    handlePacket(rxQueue[rxTail]);
    rxTail = (rxTail + 1) % RX_QUEUE_LEN;
  }
  if (millis() - lastHello > HELLO_INTERVAL_MS) {
    lastHello = millis();
    sendHello();
  }
}
