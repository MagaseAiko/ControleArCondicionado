#pragma once
// Conversão do estado do ar (stdAc::state_t) para/de JSON da página.

#include <ArduinoJson.h>
#include <IRac.h>

const char *acModeName(stdAc::opmode_t mode);
const char *acFanName(stdAc::fanspeed_t fan);
const char *acSwingVName(stdAc::swingv_t swing);
const char *acSwingHName(stdAc::swingh_t swing);

void acToJson(const stdAc::state_t &s, JsonObject o);
void acMergeJson(JsonObjectConst o, stdAc::state_t &s);  // aplica só os campos presentes
