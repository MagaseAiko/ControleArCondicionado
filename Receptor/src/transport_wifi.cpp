#ifdef TRANSPORT_WIFI
#include "transport.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <deque>
#include "config.h"
#include "link_protocol.h"
#include "web_index_gz.h"  // gerado por tools/embed_web.py

// Modo Wi-Fi (ESP32-S2): o Receptor cria a rede "ArCondicionado-XXXX" e
// serve a página. O celular faz POST /api com uma mensagem JSON por linha e
// recebe de volta um array com as mensagens pendentes do Receptor.

#define OUTBOX_MAX 60  // "hello" manda um perfil por mensagem
#define CLIENT_TIMEOUT_MS 5000

namespace {

MessageHandler handler = nullptr;
WebServer server(80);
DNSServer dns;
std::deque<String> outbox;
uint32_t lastClient = 0;

void serveIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

void serveApi() {
  lastClient = millis();
  const String body = server.arg("plain");
  int start = 0;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    line.trim();
    if (line.length() && handler) handler(line);
    start = end + 1;
  }

  String out = "[";
  for (size_t i = 0; !outbox.empty(); i++) {
    if (i) out += ',';
    out += outbox.front();
    outbox.pop_front();
  }
  out += ']';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

// Portal cativo: qualquer endereço leva para a página
void redirectToIndex() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void transportBegin(MessageHandler h) {
  handler = h;

  uint8_t mac[6];
  WiFi.mode(WIFI_AP);
  WiFi.softAPmacAddress(mac);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s-%02X%02X", DEVICE_NAME, mac[4], mac[5]);
  // O canal do AP precisa ser o mesmo do ESP-NOW
  WiFi.softAP(ssid, AP_PASSWORD, LINK_CHANNEL);

  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, serveIndex);
  server.on("/index.html", HTTP_GET, serveIndex);
  server.on("/api", HTTP_POST, serveApi);
  server.onNotFound(redirectToIndex);
  server.begin();

  Serial.printf("Wi-Fi: rede \"%s\" senha \"%s\" -> http://%s/\n", ssid, AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
}

void transportLoop() {
  dns.processNextRequest();
  server.handleClient();
}

void transportSend(const String &json) {
  if (outbox.size() >= OUTBOX_MAX) outbox.pop_front();
  outbox.push_back(json);
}

bool transportConnected() { return lastClient && millis() - lastClient < CLIENT_TIMEOUT_MS; }

const char *transportName() { return "wifi"; }

#endif  // TRANSPORT_WIFI
