#include "signals.h"

#include <LittleFS.h>

#define SIG_DIR "/s"
#define SIG1_MAGIC 0x31474953  // "SIG1": formato antigo, um quadro só
#define SIG2_MAGIC 0x32474953  // "SIG2": vários quadros
#define SIG_KEY_MAX 32

enum : uint8_t { SIG_F_CODE = 1, SIG_F_STATE = 2 };

// SIG1: magic + Sig1Frame + dados + tempos
struct __attribute__((packed)) Sig1Frame {
  int16_t protocol;
  uint16_t bits;
  uint8_t flags;
  uint8_t nbytes;
  uint16_t rawLen;
};

// SIG2: Sig2Header + (Sig2Frame + dados + tempos) para cada quadro
struct __attribute__((packed)) Sig2Header {
  uint32_t magic;
  uint8_t count;
  uint8_t reserved[3];
};

struct __attribute__((packed)) Sig2Frame {
  int16_t protocol;
  uint16_t bits;
  uint8_t flags;
  uint8_t nbytes;
  uint16_t rawLen;
  uint16_t gapMs;
};

static String pathFor(const String &key) { return String(SIG_DIR "/") + key; }

static bool readFrameBody(File &f, SignalFrame &fr, uint8_t flags, bool withRaw) {
  fr.hasCode = flags & SIG_F_CODE;
  fr.isState = flags & SIG_F_STATE;
  if (fr.nbytes > LINK_MAX_STATE || f.read(fr.data, fr.nbytes) != fr.nbytes) return false;
  const size_t rawBytes = fr.rawLen * sizeof(uint16_t);
  if (!withRaw) return f.seek(rawBytes, SeekCur);
  fr.raw.resize(fr.rawLen);
  return rawBytes == 0 || f.read((uint8_t *)fr.raw.data(), rawBytes) == rawBytes;
}

static bool readSignal(File &f, Signal &out, bool withRaw) {
  uint32_t magic;
  if (f.read((uint8_t *)&magic, sizeof(magic)) != sizeof(magic)) return false;
  out.frames.clear();

  if (magic == SIG1_MAGIC) {
    Sig1Frame h;
    if (f.read((uint8_t *)&h, sizeof(h)) != sizeof(h)) return false;
    SignalFrame fr;
    fr.protocol = h.protocol;
    fr.bits = h.bits;
    fr.nbytes = h.nbytes;
    fr.rawLen = h.rawLen;
    if (!readFrameBody(f, fr, h.flags, withRaw)) return false;
    out.frames.push_back(std::move(fr));
    return true;
  }

  if (magic != SIG2_MAGIC) return false;
  uint8_t rest[sizeof(Sig2Header) - sizeof(uint32_t)];
  if (f.read(rest, sizeof(rest)) != sizeof(rest)) return false;
  const uint8_t count = rest[0];
  if (count == 0 || count > SIGNAL_MAX_FRAMES) return false;
  for (uint8_t i = 0; i < count; i++) {
    Sig2Frame h;
    if (f.read((uint8_t *)&h, sizeof(h)) != sizeof(h)) return false;
    SignalFrame fr;
    fr.protocol = h.protocol;
    fr.bits = h.bits;
    fr.nbytes = h.nbytes;
    fr.rawLen = h.rawLen;
    fr.gapMs = h.gapMs;
    if (!readFrameBody(f, fr, h.flags, withRaw)) return false;
    out.frames.push_back(std::move(fr));
  }
  return true;
}

bool signalsBegin() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(SIG_DIR)) LittleFS.mkdir(SIG_DIR);
  return true;
}

String signalSanitizeKey(const String &key) {
  String out;
  for (unsigned int i = 0; i < key.length() && out.length() < SIG_KEY_MAX; i++) {
    char c = key[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if (c == ' ') c = '_';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out += c;
  }
  return out;
}

bool signalExists(const String &key) { return key.length() && LittleFS.exists(pathFor(key)); }

bool signalSave(const Signal &sig) {
  if (!sig.key.length() || sig.frames.empty() || sig.frames.size() > SIGNAL_MAX_FRAMES) return false;
  File f = LittleFS.open(pathFor(sig.key), "w");
  if (!f) return false;
  Sig2Header hdr = {};
  hdr.magic = SIG2_MAGIC;
  hdr.count = sig.frames.size();
  bool ok = f.write((const uint8_t *)&hdr, sizeof(hdr)) == sizeof(hdr);
  for (const SignalFrame &fr : sig.frames) {
    Sig2Frame h;
    h.protocol = fr.protocol;
    h.bits = fr.bits;
    h.flags = (fr.hasCode ? SIG_F_CODE : 0) | (fr.isState ? SIG_F_STATE : 0);
    h.nbytes = fr.hasCode ? fr.nbytes : 0;
    h.rawLen = fr.raw.size();
    h.gapMs = fr.gapMs;
    ok = ok && f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h);
    ok = ok && f.write(fr.data, h.nbytes) == h.nbytes;
    const size_t rawBytes = fr.raw.size() * sizeof(uint16_t);
    ok = ok && (rawBytes == 0 || f.write((const uint8_t *)fr.raw.data(), rawBytes) == rawBytes);
  }
  f.close();
  if (!ok) LittleFS.remove(pathFor(sig.key));
  return ok;
}

bool signalLoad(const String &key, Signal &out, bool withRaw) {
  File f = LittleFS.open(pathFor(key), "r");
  if (!f) return false;
  out.key = key;
  const bool ok = readSignal(f, out, withRaw);
  f.close();
  return ok;
}

bool signalRemove(const String &key) { return signalExists(key) && LittleFS.remove(pathFor(key)); }

bool signalRename(const String &from, const String &to) {
  if (!signalExists(from) || !to.length() || signalExists(to)) return false;
  return LittleFS.rename(pathFor(from), pathFor(to));
}

void signalList(const std::function<void(const Signal &)> &cb) {
  File dir = LittleFS.open(SIG_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    Signal sig;
    sig.key = name;
    if (readSignal(f, sig, false)) cb(sig);
    f.close();
  }
}

int signalRemovePrefix(const String &prefix) {
  std::vector<String> names;
  signalList([&](const Signal &s) {
    if (s.key.startsWith(prefix)) names.push_back(s.key);
  });
  int n = 0;
  for (const String &k : names) n += signalRemove(k) ? 1 : 0;
  return n;
}
