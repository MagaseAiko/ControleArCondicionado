// Receptor (ESP32): toda a lógica do controle de ar-condicionado.
//
// Dois modos de controle:
//  - PROTOCOLO: o protocolo do ar é conhecido (detectado ao gravar o controle
//    original ou escolhido na página). O estado completo (liga, modo, temp,
//    ventilação...) é enviado ao Emissor, que gera o sinal com o IRac.
//  - APRENDIDO: cada função usa um sinal gravado com um nome padrão
//    (power_on, cool_23, temp_up, fan_high...). Veja o README.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>
#include <deque>
#include <vector>
#include "config.h"
#include "link.h"
#include "link_ac.h"
#include "signals.h"
#include "ac_json.h"
#include "transport.h"

#ifndef IR_TIMER
#define IR_TIMER kDefaultESP32Timer
#endif

// ---------------------------------------------------------------- estado

static Preferences prefs;
static stdAc::state_t ac;  // estado em que o ar deve estar
static bool learnedMode = false;
static bool stateDirty = false;
static uint32_t stateDirtySince = 0;

static IRrecv irrecv(PIN_IR_RX, IR_CAPTURE_BUFFER, IR_TIMEOUT_MS, true, IR_TIMER);
static decode_results results;

struct Learning {
  bool active = false;
  String key;
  uint32_t startedAt = 0;
};
static Learning learning;

enum class JobKind : uint8_t { Ac, Signal, Ping };
struct Job {
  JobKind kind;
  stdAc::state_t ac;
  String key;
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

static std::vector<String> missingKeys;

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

static bool protocolReady() {
  return ac.protocol != decode_type_t::UNKNOWN && IRac::isProtocolSupported(ac.protocol);
}

static void sendStatus() {
  JsonDocument d;
  d["t"] = "status";
  d["fw"] = FW_VERSION;
  d["tr"] = transportName();
  d["learnedMode"] = learnedMode;
  d["proto"] = ac.protocol == decode_type_t::UNKNOWN ? String("") : typeToString(ac.protocol);
  d["protoId"] = (int)ac.protocol;
  d["model"] = ac.model;
  d["protoOk"] = protocolReady();

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
  acToJson(ac, d["ac"].to<JsonObject>());
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

static void sendProtocols() {
  JsonDocument d;
  d["t"] = "protocols";
  JsonArray list = d["list"].to<JsonArray>();
  for (int i = 1; i <= (int)kLastDecodeType; i++) {
    const decode_type_t p = (decode_type_t)i;
    if (!IRac::isProtocolSupported(p)) continue;
    JsonObject o = list.add<JsonObject>();
    o["id"] = i;
    o["n"] = typeToString(p);
  }
  sendJson(d);
}

// ---------------------------------------------------------------- persistência

static void markDirty() {
  stateDirty = true;
  stateDirtySince = millis();
}

static void saveState() {
  LinkAc packed;
  linkPackAc(ac, packed);
  prefs.putBytes("ac", &packed, sizeof(packed));
  prefs.putBool("learned", learnedMode);
  stateDirty = false;
}

static void loadState() {
  LinkAc packed;
  if (prefs.getBytes("ac", &packed, sizeof(packed)) == sizeof(packed)) {
    linkUnpackAc(packed, ac);
  } else {
    IRac::initState(&ac);
    ac.protocol = decode_type_t::UNKNOWN;
    ac.power = false;
    ac.mode = stdAc::opmode_t::kCool;
    ac.degrees = 24;
    ac.fanspeed = stdAc::fanspeed_t::kAuto;
    ac.swingv = stdAc::swingv_t::kOff;
    ac.swingh = stdAc::swingh_t::kOff;
  }
  learnedMode = prefs.getBool("learned", false);
}

// ---------------------------------------------------------------- fila de envios

static String jobLabel(const Job &job) {
  switch (job.kind) {
    case JobKind::Ac: return "ar";
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

static void queueSignal(const String &key) {
  Job job;
  job.kind = JobKind::Signal;
  job.key = key;
  queueJob(job);
}

static void queueAc(const stdAc::state_t &state) {
  Job job;
  job.kind = JobKind::Ac;
  job.ac = state;
  queueJob(job);
}

static uint16_t sendSignalRaw(const Signal &sig) {
  return sig.raw.empty() ? 0 : linkSendRaw(sig.raw.data(), sig.raw.size(), 38);
}

static void startJob(const Job &job) {
  uint16_t seq = 0;
  bool usedCode = false;
  switch (job.kind) {
    case JobKind::Ac: seq = linkSendAc(job.ac); break;
    case JobKind::Ping: seq = linkPing(); break;
    case JobKind::Signal: {
      Signal sig;
      if (!signalLoad(job.key, sig)) {
        notifyError("Sinal nao encontrado: " + job.key);
        lastJobDone = millis();
        return;
      }
      if (sig.hasCode) {
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

// ---------------------------------------------------------------- modo aprendido

// Enfileira o primeiro sinal existente da lista; registra o primeiro como faltante.
static bool queueFirst(std::initializer_list<String> keys) {
  for (const String &k : keys) {
    if (signalExists(k)) {
      queueSignal(k);
      return true;
    }
  }
  missingKeys.push_back(*keys.begin());
  return false;
}

static void reportMissing() {
  if (missingKeys.empty()) return;
  JsonDocument d;
  d["t"] = "missing";
  JsonArray keys = d["keys"].to<JsonArray>();
  for (const String &k : missingKeys) keys.add(k);
  sendJson(d);
}

// Traduz a diferença entre dois estados em sinais gravados.
static void queueLearnedChange(const stdAc::state_t &from, const stdAc::state_t &to) {
  missingKeys.clear();
  const String mode = acModeName(to.mode);
  const int temp = lroundf(to.degrees);
  const String combo = mode + "_" + String(temp);  // ex.: cool_23 (estado completo)

  if (!to.power) {
    if (from.power) queueFirst({"power_off", "power"});
    reportMissing();
    return;
  }

  bool modeChanged = from.mode != to.mode;
  bool tempChanged = lroundf(from.degrees) != temp;

  if (!from.power) {
    if (signalExists(combo)) {
      queueSignal(combo);
      modeChanged = tempChanged = false;
    } else {
      queueFirst({"power_on", "power"});
    }
  }

  if ((modeChanged || tempChanged) && signalExists(combo)) {
    queueSignal(combo);
  } else {
    if (modeChanged) queueFirst({"mode_" + mode, "mode"});
    if (tempChanged) {
      const String absolute = "temp_" + String(temp);
      if (signalExists(absolute)) {
        queueSignal(absolute);
      } else {
        const int delta = temp - (int)lroundf(from.degrees);
        const String step = delta > 0 ? "temp_up" : "temp_down";
        if (signalExists(step)) {
          for (int i = 0; i < min(abs(delta), 15); i++) queueSignal(step);
        } else {
          missingKeys.push_back(step);
        }
      }
    }
  }

  if (from.fanspeed != to.fanspeed) queueFirst({"fan_" + String(acFanName(to.fanspeed)), "fan"});

  if (from.swingv != to.swingv) {
    if (to.swingv == stdAc::swingv_t::kOff) queueFirst({"swing_off", "swing"});
    else queueFirst({"swing_" + String(acSwingVName(to.swingv)), "swing_on", "swing"});
  }
  if (from.swingh != to.swingh) {
    if (to.swingh == stdAc::swingh_t::kOff) queueFirst({"swingh_off", "swingh"});
    else queueFirst({"swingh_" + String(acSwingHName(to.swingh)), "swingh_on", "swingh"});
  }

  struct Toggle {
    const char *key;
    bool before, after;
  };
  const Toggle toggles[] = {
      {"turbo", from.turbo, to.turbo}, {"econo", from.econo, to.econo},    {"quiet", from.quiet, to.quiet},
      {"light", from.light, to.light}, {"clean", from.clean, to.clean},    {"filter", from.filter, to.filter},
      {"beep", from.beep, to.beep},    {"sleep", from.sleep >= 0, to.sleep >= 0},
  };
  for (const Toggle &t : toggles) {
    if (t.before == t.after) continue;
    queueFirst({String(t.key) + (t.after ? "_on" : "_off"), String(t.key)});
  }
  reportMissing();
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

  // Protocolo de ar suportado: habilita o controle completo automaticamente
  const bool acCapable = sig.hasCode && IRac::isProtocolSupported(type);
  bool autoSet = false;
  if (acCapable && ac.protocol == decode_type_t::UNKNOWN) {
    stdAc::state_t decoded;
    if (IRAcUtils::decodeToState(&results, &decoded, &ac)) ac = decoded;
    ac.protocol = type;
    autoSet = true;
    markDirty();
  }

  Serial.printf("Sinal \"%s\" gravado: %s, %d bits, %d tempos\n", sig.key.c_str(), protoName(sig.protocol).c_str(),
                sig.bits, (int)sig.raw.size());
  JsonDocument d;
  d["t"] = "learned";
  d["key"] = sig.key;
  d["proto"] = sig.hasCode ? protoName(sig.protocol) : String("RAW");
  d["protoId"] = sig.protocol;
  d["bits"] = sig.bits;
  d["n"] = sig.raw.size();
  d["acCapable"] = acCapable;
  d["autoSet"] = autoSet;
  sendJson(d);
  ledFlash(COLOR_OK, 600);
  sendSignals();
  sendStatus();
}

// Controle original usado fora da gravação: acompanha o estado do ar.
static void syncFromRemote() {
  if (learnedMode || !protocolReady() || results.decode_type != ac.protocol) return;
  if (pending.active || millis() - lastJobDone < 1500) return;  // é o nosso próprio emissor
  stdAc::state_t decoded;
  if (!IRAcUtils::decodeToState(&results, &decoded, &ac)) return;
  if (ac.model >= 0 && decoded.model < 0) decoded.model = ac.model;
  ac = decoded;
  markDirty();
  JsonDocument d;
  d["t"] = "remote";
  sendJson(d);
  sendStatus();
}

static void handleIr() {
  if (!irrecv.decode(&results)) return;  // com save_buffer a captura continua sozinha
  if (learning.active) finishLearning();
  else syncFromRemote();
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
    return;
  }
  if (c == "list") return sendSignals();
  if (c == "protocols") return sendProtocols();

  if (c == "ac") {
    stdAc::state_t next = ac;
    acMergeJson(doc["ac"].as<JsonObjectConst>(), next);
    if (learnedMode) {
      queueLearnedChange(ac, next);
    } else if (protocolReady()) {
      queueAc(next);
    } else {
      notifyError("Defina o protocolo do ar (CONFIG) ou use o modo APRENDIDO");
    }
    ac = next;
    markDirty();
    return sendStatus();
  }

  if (c == "resend") {
    if (!learnedMode) {
      if (protocolReady()) queueAc(ac);
      else notifyError("Protocolo do ar nao definido");
    } else {
      const String combo = String(acModeName(ac.mode)) + "_" + String(lroundf(ac.degrees));
      if (!ac.power) queueSignal(signalExists("power_off") ? "power_off" : "power");
      else if (signalExists(combo)) queueSignal(combo);
      else notifyError("Nenhum sinal \"" + combo + "\" gravado");
    }
    return;
  }

  if (c == "learn") return startLearning(doc["key"] | QUICK_SLOT);
  if (c == "learnCancel") return stopLearning();

  if (c == "send") {
    const String key = signalSanitizeKey(doc["key"] | "");
    if (signalExists(key)) queueSignal(key);
    else notifyError("Sinal nao encontrado: " + key);
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

  if (c == "setProto") {
    const int id = doc["id"] | -1;
    if (id >= 0 && !IRac::isProtocolSupported((decode_type_t)id)) {
      notifyError("Protocolo nao suportado");
      return;
    }
    ac.protocol = (decode_type_t)id;
    ac.model = doc["model"] | -1;
    if (id >= 0) learnedMode = false;
    markDirty();
    return sendStatus();
  }

  // Assistente de protocolo: envia um estado de teste sem alterar a configuração
  if (c == "tryProto") {
    const int id = doc["id"] | -1;
    if (id < 0 || !IRac::isProtocolSupported((decode_type_t)id)) {
      notifyError("Protocolo nao suportado");
      return;
    }
    stdAc::state_t test = ac;
    test.protocol = (decode_type_t)id;
    test.model = doc["model"] | -1;
    acMergeJson(doc["ac"].as<JsonObjectConst>(), test);
    queueAc(test);
    return;
  }

  if (c == "setMode") {
    learnedMode = doc["learned"] | false;
    markDirty();
    return sendStatus();
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
  Serial.println("\n== Receptor ArCondicionado " FW_VERSION " ==");

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

  prefs.begin("ac", false);
  loadState();
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

  if (stateDirty && millis() - stateDirtySince > 3000) saveState();
  ledUpdate();
}
