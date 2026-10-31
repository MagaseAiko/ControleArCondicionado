// Emissor (ESP-01): recebe comandos do Receptor por ESP-NOW e envia pelo LED IR.
// Toda a lógica fica no Receptor; aqui só traduzimos pacotes em pulsos IR.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include "link_protocol.h"
#include "link_ac.h"

#ifndef IR_LED_PIN
#define IR_LED_PIN 3
#endif

#define FW_VERSION "1.0.0"
#define HELLO_INTERVAL_MS 3000
#define RX_QUEUE_LEN 12

static uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static IRsend irsend(IR_LED_PIN);
static IRac irac(IR_LED_PIN);

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

#if SEND_WHIRLPOOL_AC
// Whirlpool/Consul: o sinal leva o "botão" apertado e o liga/desliga é de
// alternar. Montamos o sinal como o controle original faria.
static bool sendWhirlpool(const stdAc::state_t &st, bool prevPower, uint8_t cmd) {
  IRWhirlpoolAc w(IR_LED_PIN);
  w.begin();
  w.setModel((whirlpool_ac_remote_model_t)st.model);
  w.setMode(w.convertMode(st.mode));
  w.setTemp((uint8_t)lroundf(st.degrees));
  w.setFan(w.convertFan(st.fanspeed));
  w.setSwing(st.swingv != stdAc::swingv_t::kOff);
  w.setSuper(st.turbo);
  w.setLight(st.light);
  w.setSleep(st.sleep >= 0);
  w.setPowerToggle(st.power != prevPower);
  switch (cmd) {
    case AC_CMD_MODE:
      w.setCommand(st.mode == stdAc::opmode_t::kAuto ? kWhirlpoolAcCommand6thSense : kWhirlpoolAcCommandMode);
      break;
    case AC_CMD_TEMP: w.setCommand(kWhirlpoolAcCommandTemp); break;
    case AC_CMD_FAN: w.setCommand(kWhirlpoolAcCommandFanSpeed); break;
    case AC_CMD_SWING: w.setCommand(kWhirlpoolAcCommandSwing); break;
    case AC_CMD_TURBO: w.setCommand(kWhirlpoolAcCommandSuper); break;
    case AC_CMD_SLEEP: w.setCommand(kWhirlpoolAcCommandSleep); break;
    case AC_CMD_LIGHT: w.setCommand(kWhirlpoolAcCommandLight); break;
    default: w.setCommand(kWhirlpoolAcCommandPower); break;  // energia ou reenvio
  }
  w.send();
  return true;
}
#endif

static void handleAc(RxPacket &p, uint16_t seq) {
  if (p.len < sizeof(LinkAc)) return reply(p.mac, seq, LINK_ERR_BAD_PACKET);
  LinkAc m;
  memcpy(&m, p.data, sizeof(m));
  stdAc::state_t st;
  linkUnpackAc(m, st);
  bool ok = IRac::isProtocolSupported(st.protocol);
  if (ok) {
    const bool prevPower = m.flags & AC_F_PREV_POWER;
    const uint8_t cmd = (m.flags & AC_F_CMD_MASK) >> AC_F_CMD_SHIFT;
#if SEND_WHIRLPOOL_AC
    if ((m.flags & AC_F_HAS_PREV) && st.protocol == decode_type_t::WHIRLPOOL_AC) {
      ok = sendWhirlpool(st, prevPower, cmd);
    } else
#endif
    if (m.flags & AC_F_HAS_PREV) {
      // O receptor sabe o estado anterior: usa ele para os bits de "alternar"
      stdAc::state_t prev = st;
      prev.power = prevPower;
      ok = irac.sendAc(st, &prev);
    } else {
      irac.next = st;
      ok = irac.sendAc();  // usa o estado anterior guardado no emissor
    }
  }
  logMsg(ok ? "AC enviado" : "AC: protocolo nao suportado");
  reply(p.mac, seq, ok ? LINK_OK : LINK_ERR_UNSUPPORTED);
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
    case LINK_AC: handleAc(p, h.seq); break;
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
