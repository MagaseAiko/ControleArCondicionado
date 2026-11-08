#include "signals.h"

#include <LittleFS.h>

#define SIG_DIR "/s"
#define SIG_MAGIC 0x31474953  // "SIG1"
#define SIG_KEY_MAX 32

enum : uint8_t { SIG_F_CODE = 1, SIG_F_STATE = 2 };

struct __attribute__((packed)) SigFileHeader {
  uint32_t magic;
  int16_t protocol;
  uint16_t bits;
  uint8_t flags;
  uint8_t nbytes;
  uint16_t rawLen;
};

static String pathFor(const String &key) { return String(SIG_DIR "/") + key; }

static bool readHeader(File &f, Signal &out) {
  SigFileHeader h;
  if (f.read((uint8_t *)&h, sizeof(h)) != sizeof(h) || h.magic != SIG_MAGIC || h.nbytes > LINK_MAX_STATE) return false;
  out.protocol = h.protocol;
  out.bits = h.bits;
  out.hasCode = h.flags & SIG_F_CODE;
  out.isState = h.flags & SIG_F_STATE;
  out.nbytes = h.nbytes;
  out.rawLen = h.rawLen;
  return f.read(out.data, h.nbytes) == h.nbytes;
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
  if (!sig.key.length()) return false;
  File f = LittleFS.open(pathFor(sig.key), "w");
  if (!f) return false;
  SigFileHeader h;
  h.magic = SIG_MAGIC;
  h.protocol = sig.protocol;
  h.bits = sig.bits;
  h.flags = (sig.hasCode ? SIG_F_CODE : 0) | (sig.isState ? SIG_F_STATE : 0);
  h.nbytes = sig.hasCode ? sig.nbytes : 0;
  h.rawLen = sig.raw.size();
  bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h);
  ok = ok && f.write(sig.data, h.nbytes) == h.nbytes;
  const size_t rawBytes = sig.raw.size() * sizeof(uint16_t);
  ok = ok && (rawBytes == 0 || f.write((const uint8_t *)sig.raw.data(), rawBytes) == rawBytes);
  f.close();
  if (!ok) LittleFS.remove(pathFor(sig.key));
  return ok;
}

bool signalLoad(const String &key, Signal &out, bool withRaw) {
  File f = LittleFS.open(pathFor(key), "r");
  if (!f) return false;
  out.key = key;
  bool ok = readHeader(f, out);
  if (ok && withRaw) {
    out.raw.resize(out.rawLen);
    const size_t rawBytes = out.rawLen * sizeof(uint16_t);
    ok = rawBytes == 0 || f.read((uint8_t *)out.raw.data(), rawBytes) == rawBytes;
  }
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
    if (readHeader(f, sig)) cb(sig);
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
