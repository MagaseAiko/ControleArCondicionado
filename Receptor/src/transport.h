#pragma once
// Canal com o celular. Mensagens são JSON, uma por linha.
// TRANSPORT_BLE  -> Web Bluetooth (transport_ble.cpp)
// TRANSPORT_WIFI -> rede Wi-Fi própria + página servida pelo ESP (transport_wifi.cpp)

#include <Arduino.h>

#if !defined(TRANSPORT_BLE) && !defined(TRANSPORT_WIFI)
#error "Defina TRANSPORT_BLE ou TRANSPORT_WIFI no platformio.ini"
#endif

typedef void (*MessageHandler)(const String &line);

void transportBegin(MessageHandler handler);
void transportLoop();
void transportSend(const String &json);
bool transportConnected();
const char *transportName();
