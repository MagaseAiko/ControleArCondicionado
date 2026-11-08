#pragma once
// Perfis (um por controle remoto), guardados no LittleFS como JSON.
// O receptor não interpreta o conteúdo: quem monta e lê o perfil é a página.
// Os sinais de um perfil usam a chave "<id>-<comando>" (ver signals.h).

#include <Arduino.h>
#include <functional>

String profileSanitizeId(const String &id);
bool profileSave(const String &id, const String &json);
bool profileRemove(const String &id);
int profileCount();
void profileList(const std::function<void(const String &id, const String &json)> &cb);
