#pragma once
// Conversão entre stdAc::state_t (IRremoteESP8266) e LinkAc (ESP-NOW).

#include <math.h>
#include <IRac.h>
#include "link_protocol.h"

inline void linkPackAc(const stdAc::state_t &s, LinkAc &m) {
  m.protocol = (int16_t)s.protocol;
  m.model = s.model;
  m.mode = (int8_t)s.mode;
  m.fan = (int8_t)s.fanspeed;
  m.swingv = (int8_t)s.swingv;
  m.swingh = (int8_t)s.swingh;
  m.degrees10 = (int16_t)lroundf(s.degrees * 10);
  m.sleep = s.sleep;
  m.flags = 0;
  if (s.power) m.flags |= AC_F_POWER;
  if (s.celsius) m.flags |= AC_F_CELSIUS;
  if (s.quiet) m.flags |= AC_F_QUIET;
  if (s.turbo) m.flags |= AC_F_TURBO;
  if (s.econo) m.flags |= AC_F_ECONO;
  if (s.light) m.flags |= AC_F_LIGHT;
  if (s.filter) m.flags |= AC_F_FILTER;
  if (s.clean) m.flags |= AC_F_CLEAN;
  if (s.beep) m.flags |= AC_F_BEEP;
}

// Descobre qual "botão" o usuário apertou comparando o estado anterior com o novo.
inline LinkAcCmd linkAcCommand(const stdAc::state_t &prev, const stdAc::state_t &next) {
  if (prev.power != next.power) return AC_CMD_POWER;
  if (prev.mode != next.mode) return AC_CMD_MODE;
  if (lroundf(prev.degrees * 10) != lroundf(next.degrees * 10)) return AC_CMD_TEMP;
  if (prev.fanspeed != next.fanspeed) return AC_CMD_FAN;
  if (prev.swingv != next.swingv) return AC_CMD_SWING;
  if (prev.turbo != next.turbo) return AC_CMD_TURBO;
  if ((prev.sleep >= 0) != (next.sleep >= 0)) return AC_CMD_SLEEP;
  if (prev.light != next.light) return AC_CMD_LIGHT;
  return AC_CMD_NONE;
}

inline void linkUnpackAc(const LinkAc &m, stdAc::state_t &s) {
  IRac::initState(&s);
  s.protocol = (decode_type_t)m.protocol;
  s.model = m.model;
  s.mode = (stdAc::opmode_t)m.mode;
  s.fanspeed = (stdAc::fanspeed_t)m.fan;
  s.swingv = (stdAc::swingv_t)m.swingv;
  s.swingh = (stdAc::swingh_t)m.swingh;
  s.degrees = m.degrees10 / 10.0f;
  s.sleep = m.sleep;
  s.clock = -1;
  s.power = m.flags & AC_F_POWER;
  s.celsius = m.flags & AC_F_CELSIUS;
  s.quiet = m.flags & AC_F_QUIET;
  s.turbo = m.flags & AC_F_TURBO;
  s.econo = m.flags & AC_F_ECONO;
  s.light = m.flags & AC_F_LIGHT;
  s.filter = m.flags & AC_F_FILTER;
  s.clean = m.flags & AC_F_CLEAN;
  s.beep = m.flags & AC_F_BEEP;
}
