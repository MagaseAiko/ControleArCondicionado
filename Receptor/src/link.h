#pragma once
// Comunicação ESP-NOW com o Emissor.

#include <Arduino.h>
#include <IRac.h>
#include "link_protocol.h"

typedef void (*LinkResultCallback)(uint16_t seq, uint8_t code);

void linkBegin();
void linkLoop();
void linkOnResult(LinkResultCallback cb);

// Retornam o número de sequência do envio, ou 0 se o emissor não recebeu.
// prev: estado anterior do ar (para protocolos com "alternar" e com "botão")
// powerAbs: Whirlpool com bit de energia absoluto (ver AC_F_POWER_ABS)
uint16_t linkSendAc(const stdAc::state_t &state, const stdAc::state_t &prev, bool powerAbs);
uint16_t linkSendCode(int16_t protocol, uint16_t bits, bool isState, const uint8_t *data, uint8_t nbytes);
uint16_t linkSendRaw(const uint16_t *timings, uint16_t count, uint8_t khz);
uint16_t linkPing();

bool linkPaired();
bool linkEmitterOnline();
String linkEmitterMac();
String linkEmitterFw();
void linkUnpair();
const char *linkErrorText(uint8_t code);
