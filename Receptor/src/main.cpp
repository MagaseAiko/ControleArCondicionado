// Receptor (ESP32): controle remoto universal.
//
// Grava sinais IR do controle original e manda o Emissor (ESP-01) repetir.
// Os sinais ficam organizados em perfis (um por controle remoto), montados
// pela página: o receptor só guarda os perfis e os sinais, e envia o que a
// página pedir.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <deque>
#include <vector>
#include "config.h"
#include "link.h"
#include "signals.h"
#include "profiles.h"
#include "transport.h"

#ifndef IR_TIMER
#define IR_TIMER kDefaultESP32Timer
#endif

// ---------------------------------------------------------------- estado

static IRrecv irrecv(PIN_IR_RX, IR_CAPTURE_BUFFER, IR_TIMEOUT_MS, true, IR_TIMER);
static decode_results results;

struct Learning {
  bool active = false;
  String key;
  uint32_t startedAt = 0;
};
static Learning learning;

enum class JobKind : uint8_t { Signal, Ping };
struct Job {
  JobKind kind;
  String key;
  bool forceRaw = false;  // envia os tempos gravados, sem recodificar o protocolo
};
static std::deque<Job> jobs;

struct Pending {
  bool active = false;
  uint16_t seq = 0;
  uint32_t since = 0;
  bool usedCode = false;
  Job job;
};
static Pending pending;
static uint32_t lastJobDone = 0;

// ---------------------------------------------------------------- LED de status
// RGB: cores abaixo. LED simples (PIN_LED): aceso = ok, pisca lento = emissor
// offline, pisca rápido = gravando, aceso 0,3 s = enviado, pisca muito rápido = erro.

#define COLOR_IDLE 0x140E24      // roxo fraco: tudo ok
#define COLOR_OFFLINE 0x201000   // laranja fraco: emissor offline
#define COLOR_LEARN 0x5030C0     // roxo forte piscando: gravando
#define COLOR_OK 0x00A040
#define COLOR_ERROR 0xA00000

#if PIN_RGB >= 0
static Adafruit_NeoPixel rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
#endif
static uint32_t flashColor = 0;
static uint32_t flashUntil = 0;

static void ledFlash(uint32_t color, uint16_t ms = 300) {
  flashColor = color;
  flashUntil = millis() + ms;
}

static void ledUpdate() {
#if PIN_RGB >= 0
  static uint32_t shown = 0xFFFFFFFF;
  uint32_t c;
  if ((int32_t)(flashUntil - millis()) > 0) c = flashColor;
  else if (learning.active) c = (millis() / 200) % 2 ? COLOR_LEARN : 0;
  else c = linkEmitterOnline() ? COLOR_IDLE : COLOR_OFFLINE;
  if (c != shown) {
    rgb.setPixelColor(0, c);
    rgb.show();
    shown = c;
  }
#elif PIN_LED >= 0
  static int shown = -1;
  const uint32_t now = millis();
  bool on;
  if ((int32_t)(flashUntil - now) > 0) on = flashColor == COLOR_ERROR ? (now / 60) % 2 : true;
  else if (learning.active) on = (now / 200) % 2;
  else if (linkEmitterOnline()) on = true;
  else on = now % 1000 < 100;
  if (on != shown) {
    digitalWrite(PIN_LED, on);
    shown = on;
  }
#endif
}

// ---------------------------------------------------------------- mensagens para a página

static void sendJson(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  transportSend(out);
}

static void notifyError(const String &msg) {
  Serial.println("Erro: " + msg);
  JsonDocument d;
  d["t"] = "error";
  d["msg"] = msg;
  sendJson(d);
  ledFlash(COLOR_ERROR);
}

static String protoName(int16_t p) { return p < 0 ? String("RAW") : typeToString((decode_type_t)p); }

static void sendStatus() {
  JsonDocument d;
  d["t"] = "status";
  d["fw"] = FW_VERSION;
  d["tr"] = transportName();

  JsonObject em = d["emitter"].to<JsonObject>();
  em["paired"] = linkPaired();
  em["online"] = linkEmitterOnline();
  em["mac"] = linkEmitterMac();
  em["fw"] = linkEmitterFw();

  JsonObject le = d["learning"].to<JsonObject>();
  le["active"] = learning.active;
  le["key"] = learning.key;
  const uint32_t elapsed = millis() - learning.startedAt;
  le["left"] = learning.active && elapsed < LEARN_TIMEOUT_MS ? LEARN_TIMEOUT_MS - elapsed : 0;

  d["busy"] = pending.active;
  d["queue"] = jobs.size();
  sendJson(d);
}

static void sendSignals() {
  JsonDocument d;
  d["t"] = "signals";
  JsonArray list = d["list"].to<JsonArray>();
  signalList([&](const Signal &s) {
    JsonObject o = list.add<JsonObject>();
    o["k"] = s.key;
    o["p"] = s.hasCode ? protoName(s.protocol) : String("RAW");
    o["b"] = s.bits;
    o["n"] = s.rawLen;
    o["code"] = s.hasCode;
  });
  sendJson(d);
}

// Um perfil por mensagem (perfis podem ser grandes), depois "profilesDone".
static void sendProfiles() {
  int n = 0;
  profileList([&](const String &id, const String &json) {
    JsonDocument d;
    d["t"] = "profile";
    d["id"] = id;
    d["data"] = serialized(json);
    sendJson(d);
    n++;
  });
  JsonDocument d;
  d["t"] = "profilesDone";
  d["n"] = n;
  sendJson(d);
}

// ---------------------------------------------------------------- fila de envios

static String jobLabel(const Job &job) {
  switch (job.kind) {
    case JobKind::Ping: return "ping";
    default: return job.key;
  }
}

static void reportSent(const Job &job, bool ok, const String &err) {
  JsonDocument d;
  d["t"] = "sent";
  d["ok"] = ok;
  d["what"] = jobLabel(job);
  if (!ok) d["err"] = err;
  sendJson(d);
  ledFlash(ok ? COLOR_OK : COLOR_ERROR);
  Serial.printf("Envio %s: %s %s\n", jobLabel(job).c_str(), ok ? "ok" : "falhou", err.c_str());
}

static bool queueJob(const Job &job) {
  if (jobs.size() >= MAX_JOBS) {
    notifyError("Fila de comandos cheia");
    return false;
  }
  jobs.push_back(job);
  return true;
}

static void queueSignal(const String &key, bool forceRaw = false) {
  Job job;
  job.kind = JobKind::Signal;
  job.key = key;
  job.forceRaw = forceRaw;
  queueJob(job);
}

static uint16_t sendSignalRaw(const Signal &sig) {
  return sig.raw.empty() ? 0 : linkSendRaw(sig.raw.data(), sig.raw.size(), 38);
}

static void startJob(const Job &job) {
  uint16_t seq = 0;
  bool usedCode = false;
  switch (job.kind) {
    case JobKind::Ping: seq = linkPing(); break;
    case JobKind::Signal: {
      Signal sig;
      if (!signalLoad(job.key, sig)) {
        notifyError("Sinal nao encontrado: " + job.key);
        lastJobDone = millis();
        return;
      }
      if (sig.hasCode && !(job.forceRaw && !sig.raw.empty())) {
        seq = linkSendCode(sig.protocol, sig.bits, sig.isState, sig.data, sig.nbytes);
        usedCode = true;
      } else {
        seq = sendSignalRaw(sig);
      }
      break;
    }
  }
  if (!seq) {
    lastJobDone = millis();
    reportSent(job, false, linkPaired() ? "emissor nao respondeu" : "nenhum emissor pareado");
    return;
  }
  pending.active = true;
  pending.seq = seq;
  pending.since = millis();
  pending.usedCode = usedCode;
  pending.job = job;
}

static void onLinkResult(uint16_t seq, uint8_t code) {
  if (!pending.active || seq != pending.seq) return;

  // Protocolo decodificado mas o emissor não sabe enviar: tenta o sinal bruto
  if (code != LINK_OK && pending.job.kind == JobKind::Signal && pending.usedCode) {
    Signal sig;
    if (signalLoad(pending.job.key, sig)) {
      const uint16_t rawSeq = sendSignalRaw(sig);
      if (rawSeq) {
        pending.seq = rawSeq;
        pending.since = millis();
        pending.usedCode = false;
        return;
      }
    }
  }
  pending.active = false;
  lastJobDone = millis();
  reportSent(pending.job, code == LINK_OK, linkErrorText(code));
}

static void processJobs() {
  if (pending.active) {
    if (millis() - pending.since > RESULT_TIMEOUT_MS) {
      pending.active = false;
      lastJobDone = millis();
      reportSent(pending.job, false, "tempo esgotado");
    }
    return;
  }
  if (jobs.empty() || millis() - lastJobDone < JOB_GAP_MS) return;
  const Job job = jobs.front();
  jobs.pop_front();
  startJob(job);
}

// ---------------------------------------------------------------- gravação

static void startLearning(const String &rawKey) {
  String key = signalSanitizeKey(rawKey);
  if (!key.length()) key = QUICK_SLOT;
  learning.active = true;
  learning.key = key;
  learning.startedAt = millis();
  Serial.println("Gravando sinal: " + key);
  sendStatus();
}

static void stopLearning() {
  learning.active = false;
  sendStatus();
}

// ---------------------------------------------------------------- leitura do controle original

// Diagnóstico: mostra na página o último sinal recebido.
static void reportIr() {
  JsonDocument d;
  d["t"] = "ir";
  d["proto"] = results.decode_type == decode_type_t::UNKNOWN ? String("RAW") : typeToString(results.decode_type);
  d["bits"] = results.bits;
  if (hasACState(results.decode_type)) {
    String hex;
    for (uint16_t i = 0; i < results.bits / 8; i++) {
      char b[3];
      snprintf(b, sizeof(b), "%02X", results.state[i]);
      hex += b;
    }
    d["hex"] = hex;
  } else {
    d["hex"] = uint64ToString(results.value, 16);
  }
  sendJson(d);
  Serial.printf("IR: %s %d bits %s\n", d["proto"].as<const char *>(), results.bits, d["hex"].as<const char *>());
}

static void finishLearning() {
  if (results.overflow) {
    notifyError("Sinal longo demais para o buffer");
    stopLearning();
    return;
  }
  if (results.repeat) return;  // código de repetição (botão segurado)
  const decode_type_t type = results.decode_type;
  if (type == decode_type_t::UNKNOWN && results.rawlen < IR_MIN_UNKNOWN_LEN) return;  // ruído

  Signal sig;
  sig.key = learning.key;
  sig.protocol = (int16_t)type;
  sig.bits = results.bits;
  if (type != decode_type_t::UNKNOWN) {
    sig.hasCode = true;
    if (hasACState(type)) {
      sig.isState = true;
      sig.nbytes = min<uint16_t>(results.bits / 8, LINK_MAX_STATE);
      memcpy(sig.data, results.state, sig.nbytes);
    } else {
      sig.isState = false;
      sig.nbytes = 8;
      for (int i = 0; i < 8; i++) sig.data[i] = (results.value >> (8 * i)) & 0xFF;
    }
  }
  uint16_t *raw = resultToRawArray(&results);
  const uint16_t rawLen = getCorrectedRawLength(&results);
  if (raw) {
    sig.raw.assign(raw, raw + min<uint16_t>(rawLen, LINK_MAX_RAW));
    delete[] raw;
  }

  learning.active = false;
  if (!signalSave(sig)) {
    notifyError("Falha ao salvar o sinal");
    sendStatus();
    return;
  }

  Serial.printf("Sinal \"%s\" gravado: %s, %d bits, %d tempos\n", sig.key.c_str(), protoName(sig.protocol).c_str(),
                sig.bits, (int)sig.raw.size());
  JsonDocument d;
  d["t"] = "learned";
  d["key"] = sig.key;
  d["proto"] = sig.hasCode ? protoName(sig.protocol) : String("RAW");
  d["bits"] = sig.bits;
  d["n"] = sig.raw.size();
  sendJson(d);
  ledFlash(COLOR_OK, 600);
  sendSignals();
  sendStatus();
}

static void handleIr() {
  if (!irrecv.decode(&results)) return;  // com save_buffer a captura continua sozinha
  const bool ours = pending.active || millis() - lastJobDone < 1500;  // nosso próprio emissor
  if (!ours) reportIr();
  if (learning.active) finishLearning();
}

// ---------------------------------------------------------------- comandos da página

static void handleMessage(const String &line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) {
    notifyError("JSON invalido");
    return;
  }
  const String c = doc["c"] | "";

  if (c == "poll") return;
  if (c == "status") return sendStatus();
  if (c == "hello") {
    sendStatus();
    sendSignals();
    sendProfiles();
    return;
  }
  if (c == "list") return sendSignals();
  if (c == "profiles") return sendProfiles();

  if (c == "learn") return startLearning(doc["key"] | QUICK_SLOT);
  if (c == "learnCancel") return stopLearning();

  if (c == "send") {
    const String key = signalSanitizeKey(doc["key"] | "");
    if (signalExists(key)) queueSignal(key, doc["raw"] | false);
    else notifyError("Comando ainda nao gravado: " + key);
    return;
  }

  if (c == "del") {
    if (!signalRemove(signalSanitizeKey(doc["key"] | ""))) notifyError("Nao foi possivel apagar");
    return sendSignals();
  }

  if (c == "rename") {
    const String from = signalSanitizeKey(doc["from"] | "");
    const String to = signalSanitizeKey(doc["to"] | "");
    if (!signalRename(from, to)) notifyError("Nao foi possivel renomear (nome vazio ou ja existe?)");
    return sendSignals();
  }

  // Perfis: o conteúdo ("data") é montado pela página e guardado como está
  if (c == "saveProfile") {
    const String id = profileSanitizeId(doc["id"] | "");
    if (!id.length()) return notifyError("Perfil sem identificador");
    bool exists = false;
    int count = 0;
    profileList([&](const String &pid, const String &) {
      exists |= pid == id;
      count++;
    });
    if (!exists && count >= PROFILES_MAX) return notifyError("Limite de perfis atingido");
    String json;
    serializeJson(doc["data"], json);
    if (!profileSave(id, json)) return notifyError("Nao foi possivel salvar o perfil (grande demais?)");
    JsonDocument d;
    d["t"] = "profileSaved";
    d["id"] = id;
    return sendJson(d);
  }

  if (c == "delProfile") {
    const String id = profileSanitizeId(doc["id"] | "");
    if (!id.length()) return notifyError("Perfil sem identificador");
    profileRemove(id);
    signalRemovePrefix(id + "-");  // apaga os comandos gravados do perfil
    JsonDocument d;
    d["t"] = "profileDeleted";
    d["id"] = id;
    sendJson(d);
    return sendSignals();
  }

  if (c == "ping") {
    Job job;
    job.kind = JobKind::Ping;
    queueJob(job);
    return;
  }

  if (c == "unpair") {
    linkUnpair();
    return sendStatus();
  }

  notifyError("Comando desconhecido: " + c);
}

// ---------------------------------------------------------------- botões físicos

struct Button {
  explicit Button(uint8_t p) : pin(p) {}
  uint8_t pin;
  bool reading = true;
  bool stable = true;
  uint32_t changedAt = 0;
};
static Button btnRec(PIN_BTN_REC);
static Button btnSend(PIN_BTN_SEND);

static bool buttonPressed(Button &b) {
  const bool r = digitalRead(b.pin);
  if (r != b.reading) {
    b.reading = r;
    b.changedAt = millis();
  }
  if (millis() - b.changedAt > 30 && r != b.stable) {
    b.stable = r;
    return !r;  // ativo em LOW
  }
  return false;
}

static void handleButtons() {
  if (buttonPressed(btnRec)) {
    if (learning.active) stopLearning();
    else startLearning(QUICK_SLOT);
  }
  if (buttonPressed(btnSend)) {
    if (signalExists(QUICK_SLOT)) queueSignal(QUICK_SLOT);
    else notifyError("Nenhum sinal gravado no botao (aperte GRAVAR primeiro)");
  }
}

// ---------------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n== Receptor (controle universal) " FW_VERSION " ==");

#if PIN_RGB >= 0
  rgb.begin();
  rgb.setBrightness(40);
  rgb.clear();
  rgb.show();
#elif PIN_LED >= 0
  pinMode(PIN_LED, OUTPUT);
#endif
  pinMode(PIN_BTN_REC, INPUT_PULLUP);
  pinMode(PIN_BTN_SEND, INPUT_PULLUP);

  if (!signalsBegin()) Serial.println("Falha ao montar o LittleFS");

  transportBegin(handleMessage);
  linkBegin();
  linkOnResult(onLinkResult);

  irrecv.setUnknownThreshold(IR_MIN_UNKNOWN_LEN);
  irrecv.enableIRIn();
  Serial.printf("Receptor IR no GPIO%d, botoes GPIO%d (gravar) e GPIO%d (enviar)\n", PIN_IR_RX, PIN_BTN_REC,
                PIN_BTN_SEND);
}

void loop() {
  transportLoop();
  linkLoop();
  handleIr();
  handleButtons();
  processJobs();

  if (learning.active && millis() - learning.startedAt > LEARN_TIMEOUT_MS) {
    learning.active = false;
    JsonDocument d;
    d["t"] = "learnTimeout";
    sendJson(d);
    ledFlash(COLOR_ERROR);
    sendStatus();
  }

  // Avisa a página quando o emissor entra/sai do ar
  static bool lastOnline = false, lastPaired = false;
  if (linkEmitterOnline() != lastOnline || linkPaired() != lastPaired) {
    lastOnline = linkEmitterOnline();
    lastPaired = linkPaired();
    sendStatus();
  }
  ledUpdate();
}
