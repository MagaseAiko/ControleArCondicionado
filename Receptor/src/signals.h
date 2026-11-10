#pragma once
// Sinais IR gravados, guardados no LittleFS (um arquivo por sinal).
//
// Um sinal pode ter vários quadros: alguns controles mandam dois (ou mais)
// sinais seguidos a cada toque. Cada quadro guarda o código decodificado
// (quando o protocolo é conhecido), os tempos brutos e o silêncio antes dele.

#include <Arduino.h>
#include <functional>
#include <vector>
#include "link_protocol.h"

#define SIGNAL_MAX_FRAMES 6

struct SignalFrame {
  int16_t protocol = -1;  // decode_type_t (-1 = desconhecido / só bruto)
  uint16_t bits = 0;
  bool hasCode = false;   // tem código decodificado
  bool isState = false;   // código é um estado de ar (array de bytes)
  uint8_t nbytes = 0;
  uint8_t data[LINK_MAX_STATE] = {};
  uint16_t gapMs = 0;     // silêncio antes deste quadro (0 no primeiro)
  uint16_t rawLen = 0;
  std::vector<uint16_t> raw;  // tempos em microssegundos
};

struct Signal {
  String key;
  std::vector<SignalFrame> frames;
  uint32_t rawTotal() const {
    uint32_t n = 0;
    for (const SignalFrame &f : frames) n += f.rawLen;
    return n;
  }
};

bool signalsBegin();
String signalSanitizeKey(const String &key);
bool signalExists(const String &key);
bool signalSave(const Signal &sig);
bool signalLoad(const String &key, Signal &out, bool withRaw = true);
bool signalRemove(const String &key);
bool signalRename(const String &from, const String &to);
int signalRemovePrefix(const String &prefix);  // apaga todos os sinais "prefix*"
void signalList(const std::function<void(const Signal &)> &cb);  // sem os tempos brutos
