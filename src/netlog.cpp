// ============================================================================
//  ALEXO - Protokoll über das Netz (Telnet). Siehe netlog.h.
//  Immer nur ein Client: kommt ein weiterer, löst er den bisherigen ab.
//  Alle Vorgänge blockieren nicht, damit der Loop für die Aktualisierung über
//  Funk ungestört bleibt.
// ============================================================================
#include "netlog.h"
#include <WiFi.h>

static WiFiServer *s_server = nullptr;
static WiFiClient  s_client;

void netlogBegin(uint16_t port) {
  if (s_server) return;                 // läuft bereits
  s_server = new WiFiServer(port);
  s_server->begin();
  s_server->setNoDelay(true);
}

void netlogHandle() {
  if (!s_server) return;
  // Wartet ein neuer Client? Annehmen und den bisherigen ablösen.
  if (s_server->hasClient()) {
    WiFiClient nc = s_server->available();
    if (s_client && s_client.connected()) s_client.stop();
    s_client = nc;
    s_client.setNoDelay(true);
    s_client.println("[netlog] mit Alexo verbunden");
  }
  // Eingaben des Clients verwerfen: wir brauchen sie nicht, aber der Puffer
  // muss geleert werden.
  if (s_client && s_client.connected()) {
    while (s_client.available()) s_client.read();
  }
}

void netlogPrintln(const char *s) {
  if (s_client && s_client.connected()) s_client.println(s);
}

bool netlogConnected() {
  return s_client && s_client.connected();
}
