#include "ac_json.h"

#define AC_TEMP_MIN 16
#define AC_TEMP_MAX 30

template <typename E>
struct Named {
  E value;
  const char *name;
};

// O primeiro item de cada tabela é o padrão para valores desconhecidos.
static const Named<stdAc::opmode_t> MODES[] = {
    {stdAc::opmode_t::kAuto, "auto"}, {stdAc::opmode_t::kCool, "cool"}, {stdAc::opmode_t::kHeat, "heat"},
    {stdAc::opmode_t::kDry, "dry"},   {stdAc::opmode_t::kFan, "fan"},
};
static const Named<stdAc::fanspeed_t> FANS[] = {
    {stdAc::fanspeed_t::kAuto, "auto"},     {stdAc::fanspeed_t::kMin, "min"},   {stdAc::fanspeed_t::kLow, "low"},
    {stdAc::fanspeed_t::kMedium, "medium"}, {stdAc::fanspeed_t::kHigh, "high"}, {stdAc::fanspeed_t::kMax, "max"},
};
static const Named<stdAc::swingv_t> SWINGV[] = {
    {stdAc::swingv_t::kOff, "off"},       {stdAc::swingv_t::kAuto, "auto"},  {stdAc::swingv_t::kHighest, "highest"},
    {stdAc::swingv_t::kHigh, "high"},     {stdAc::swingv_t::kMiddle, "middle"}, {stdAc::swingv_t::kLow, "low"},
    {stdAc::swingv_t::kLowest, "lowest"},
};
static const Named<stdAc::swingh_t> SWINGH[] = {
    {stdAc::swingh_t::kOff, "off"},       {stdAc::swingh_t::kAuto, "auto"},   {stdAc::swingh_t::kLeftMax, "leftmax"},
    {stdAc::swingh_t::kLeft, "left"},     {stdAc::swingh_t::kMiddle, "middle"}, {stdAc::swingh_t::kRight, "right"},
    {stdAc::swingh_t::kRightMax, "rightmax"}, {stdAc::swingh_t::kWide, "wide"},
};

template <typename E, size_t N>
static const char *nameOf(const Named<E> (&table)[N], E value) {
  for (const auto &item : table)
    if (item.value == value) return item.name;
  return table[0].name;
}

template <typename E, size_t N>
static void readEnum(JsonObjectConst o, const char *field, const Named<E> (&table)[N], E &out) {
  const char *s = o[field];
  if (!s) return;
  for (const auto &item : table)
    if (!strcmp(item.name, s)) out = item.value;
}

static void readBool(JsonObjectConst o, const char *field, bool &out) {
  if (o[field].is<bool>()) out = o[field].as<bool>();
}

const char *acModeName(stdAc::opmode_t mode) { return nameOf(MODES, mode); }
const char *acFanName(stdAc::fanspeed_t fan) { return nameOf(FANS, fan); }
const char *acSwingVName(stdAc::swingv_t swing) { return nameOf(SWINGV, swing); }
const char *acSwingHName(stdAc::swingh_t swing) { return nameOf(SWINGH, swing); }

void acToJson(const stdAc::state_t &s, JsonObject o) {
  o["power"] = s.power;
  o["mode"] = acModeName(s.mode);
  o["temp"] = lroundf(s.degrees);
  o["fan"] = acFanName(s.fanspeed);
  o["swingv"] = acSwingVName(s.swingv);
  o["swingh"] = acSwingHName(s.swingh);
  o["turbo"] = s.turbo;
  o["econo"] = s.econo;
  o["quiet"] = s.quiet;
  o["light"] = s.light;
  o["clean"] = s.clean;
  o["filter"] = s.filter;
  o["beep"] = s.beep;
  o["sleep"] = s.sleep >= 0;
}

void acMergeJson(JsonObjectConst o, stdAc::state_t &s) {
  if (o.isNull()) return;
  readBool(o, "power", s.power);
  readEnum(o, "mode", MODES, s.mode);
  if (o["temp"].is<float>()) s.degrees = constrain(roundf(o["temp"].as<float>()), AC_TEMP_MIN, AC_TEMP_MAX);
  readEnum(o, "fan", FANS, s.fanspeed);
  readEnum(o, "swingv", SWINGV, s.swingv);
  readEnum(o, "swingh", SWINGH, s.swingh);
  readBool(o, "turbo", s.turbo);
  readBool(o, "econo", s.econo);
  readBool(o, "quiet", s.quiet);
  readBool(o, "light", s.light);
  readBool(o, "clean", s.clean);
  readBool(o, "filter", s.filter);
  readBool(o, "beep", s.beep);
  if (o["sleep"].is<bool>()) s.sleep = o["sleep"].as<bool>() ? 0 : -1;
  s.celsius = true;
}
