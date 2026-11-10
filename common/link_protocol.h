#pragma once
// Protocolo ESP-NOW entre o Receptor (ESP32) e o Emissor (ESP-01).
// Compartilhado pelos dois projetos via build_flags = -I../common

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LINK_CHANNEL 1        // canal Wi-Fi usado pelo ESP-NOW (igual nos dois lados)
#define LINK_MAGIC 0xA1C0
#define LINK_VERSION 3       // 3: sinais com vários quadros (LINK_RAW_SEQ)
#define LINK_MAX_STATE 64     // maior código de protocolo em bytes (ex.: controles de ar)
#define LINK_RAW_PER_CHUNK 115
#define LINK_MAX_RAW 2048     // maior sinal bruto (quantidade de tempos, somando todos os quadros)
// Em LINK_RAW_SEQ os quadros vêm em sequência, separados por LINK_SEQ_MARK
// seguido do silêncio em ms antes do próximo quadro: q1..., 0, gap, q2...
#define LINK_SEQ_MARK 0

enum LinkMsgType : uint8_t {
  LINK_HELLO = 1,      // Emissor -> broadcast: "estou aqui"
  LINK_HELLO_ACK = 2,  // Receptor -> Emissor: "te conheço"
  // 3 era LINK_AC (estado do ar via IRac), removido na versão 2
  LINK_CODE = 4,       // Receptor -> Emissor: código de protocolo conhecido
  LINK_RAW = 5,        // Receptor -> Emissor: pedaço de sinal bruto
  LINK_RESULT = 6,     // Emissor -> Receptor: resultado de um envio
  LINK_PING = 7,       // Receptor -> Emissor: teste de comunicação
  LINK_RAW_SEQ = 8,    // Receptor -> Emissor: pedaço de vários quadros brutos seguidos
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
