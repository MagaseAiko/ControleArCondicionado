#pragma once
// Protocolo ESP-NOW entre o Receptor (ESP32) e o Emissor (ESP-01).
// Compartilhado pelos dois projetos via build_flags = -I../common

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LINK_CHANNEL 1        // canal Wi-Fi usado pelo ESP-NOW (igual nos dois lados)
#define LINK_MAGIC 0xA1C0
#define LINK_VERSION 1
#define LINK_MAX_STATE 64     // maior estado de protocolo de ar (bytes)
#define LINK_RAW_PER_CHUNK 115
#define LINK_MAX_RAW 1024     // maior sinal bruto (quantidade de tempos)

enum LinkMsgType : uint8_t {
  LINK_HELLO = 1,      // Emissor -> broadcast: "estou aqui"
  LINK_HELLO_ACK = 2,  // Receptor -> Emissor: "te conheço"
  LINK_AC = 3,         // Receptor -> Emissor: estado completo do ar (IRac)
  LINK_CODE = 4,       // Receptor -> Emissor: código de protocolo conhecido
  LINK_RAW = 5,        // Receptor -> Emissor: pedaço de sinal bruto
  LINK_RESULT = 6,     // Emissor -> Receptor: resultado de um envio
  LINK_PING = 7,       // Receptor -> Emissor: teste de comunicação
};

enum LinkResultCode : uint8_t {
  LINK_OK = 0,
  LINK_ERR_UNSUPPORTED = 1,
  LINK_ERR_BAD_PACKET = 2,
  LINK_ERR_RAW_SEQUENCE = 3,
  LINK_ERR_TOO_LONG = 4,
};

struct __attribute__((packed)) LinkHeader {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t seq;
};

struct __attribute__((packed)) LinkHello {
  LinkHeader h;
  uint8_t paired;
  char fw[10];
};

// Bits de LinkAc::flags
#define AC_F_POWER (1 << 0)
#define AC_F_CELSIUS (1 << 1)
#define AC_F_QUIET (1 << 2)
#define AC_F_TURBO (1 << 3)
#define AC_F_ECONO (1 << 4)
#define AC_F_LIGHT (1 << 5)
#define AC_F_FILTER (1 << 6)
#define AC_F_CLEAN (1 << 7)
#define AC_F_BEEP (1 << 8)

struct __attribute__((packed)) LinkAc {
  LinkHeader h;
  int16_t protocol;  // decode_type_t
  int16_t model;
  int8_t mode;       // stdAc::opmode_t
  int8_t fan;        // stdAc::fanspeed_t
  int8_t swingv;     // stdAc::swingv_t
  int8_t swingh;     // stdAc::swingh_t
  int16_t degrees10; // temperatura x10
  int16_t sleep;     // -1 = desligado
  uint16_t flags;
};

struct __attribute__((packed)) LinkCode {
  LinkHeader h;
  int16_t protocol;
  uint16_t bits;
  uint8_t isState;   // 1 = data é um estado (ar); 0 = data é um uint64 little-endian
  uint8_t nbytes;
  uint8_t data[LINK_MAX_STATE];
};

struct __attribute__((packed)) LinkRaw {
  LinkHeader h;
  uint16_t total;    // total de tempos do sinal
  uint16_t offset;   // posição deste pedaço
  uint8_t count;     // tempos neste pedaço
  uint8_t khz;       // frequência da portadora
  uint16_t t[LINK_RAW_PER_CHUNK];
};

struct __attribute__((packed)) LinkResult {
  LinkHeader h;
  uint16_t ackSeq;
  uint8_t code;
};

static_assert(sizeof(LinkRaw) <= 250, "LinkRaw excede o limite do ESP-NOW");
static_assert(sizeof(LinkCode) <= 250, "LinkCode excede o limite do ESP-NOW");

inline void linkInitHeader(LinkHeader &h, uint8_t type, uint16_t seq) {
  h.magic = LINK_MAGIC;
  h.version = LINK_VERSION;
  h.type = type;
  h.seq = seq;
}

inline bool linkValid(const uint8_t *data, int len) {
  if (len < (int)sizeof(LinkHeader)) return false;
  LinkHeader h;
  memcpy(&h, data, sizeof(h));
  return h.magic == LINK_MAGIC && h.version == LINK_VERSION;
}
