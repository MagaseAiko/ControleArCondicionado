#include "profiles.h"

#include <LittleFS.h>
#include "config.h"

#define PROFILE_DIR "/p"
#define PROFILE_ID_MAX 8

static String pathFor(const String &id) { return String(PROFILE_DIR "/") + id + ".json"; }

static void ensureDir() {
  if (!LittleFS.exists(PROFILE_DIR)) LittleFS.mkdir(PROFILE_DIR);
}

String profileSanitizeId(const String &id) {
  String out;
  for (unsigned int i = 0; i < id.length() && out.length() < PROFILE_ID_MAX; i++) {
    char c = id[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
  }
  return out;
}

bool profileSave(const String &id, const String &json) {
  if (!id.length() || json.length() > PROFILE_MAX_BYTES) return false;
  ensureDir();
  // Grava em um arquivo temporário e troca no fim: não perde o perfil se faltar energia
  const String tmp = String(PROFILE_DIR "/") + id + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const bool ok = f.print(json) == json.length();
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(pathFor(id));
  return LittleFS.rename(tmp, pathFor(id));
}

bool profileRemove(const String &id) { return id.length() && LittleFS.remove(pathFor(id)); }

int profileCount() {
  int n = 0;
  profileList([&](const String &, const String &) { n++; });
  return n;
}

void profileList(const std::function<void(const String &id, const String &json)> &cb) {
  ensureDir();
  File dir = LittleFS.open(PROFILE_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.endsWith(".json") && f.size() <= PROFILE_MAX_BYTES) {
      const String id = name.substring(0, name.length() - 5);
      const String json = f.readString();
      f.close();
      cb(id, json);
    } else {
      f.close();
    }
  }
}
