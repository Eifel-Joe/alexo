// ============================================================================
//  ALEXO - WiFi
// ============================================================================
#include "net.h"
#include "secrets.h"
#include <WiFi.h>
#include <time.h>

bool wifiBegin(uint32_t timeoutMs) {
  Serial.printf("[wifi] verbinde mit \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(250);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[wifi] OK - IP %s, RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println("\n[wifi] FEHLGESCHLAGEN (SSID/Passwort? 2,4-GHz-Netz?)");
  return false;
}

bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

// --- Uhr via NTP (deutsche Zeitzone mit automatischer Sommerzeit) -----------
static const char *GIORNI[] = { "Sonntag", "Montag", "Dienstag", "Mittwoch",
                                "Donnerstag", "Freitag", "Samstag" };
static const char *MESI[]   = { "Januar", "Februar", "März", "April", "Mai",
                                "Juni", "Juli", "August", "September",
                                "Oktober", "November", "Dezember" };

void timeBegin() {
  // TZ Europe/Berlin: MEZ (UTC+1), MESZ (UTC+2) mit automatischer Umstellung.
  // Die Regel ist dieselbe wie fuer Europe/Rome, nur der Name aendert sich.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3",
               "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  Serial.println("[time] NTP-Abgleich gestartet (Zeitzone Europe/Berlin)");
}

String nowContextString() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";   // Uhr noch nicht abgeglichen (< Nov. 2023)
  struct tm lt, gt;
  localtime_r(&now, &lt);             // Ortszeit (Europe/Berlin, mit Sommerzeit)
  gmtime_r(&now, &gt);               // Zeit in UTC
  char buf[200];
  // Wir geben BEIDES an, Ortszeit und UTC: aus der UTC rechnet Claude jede
  // andere Zeitzone selbst aus.
  snprintf(buf, sizeof(buf),
           "%s, %d. %s %d, %02d:%02d Uhr Ortszeit (Europe/Berlin); im selben Augenblick ist es in UTC %02d:%02d Uhr",
           GIORNI[lt.tm_wday], lt.tm_mday, MESI[lt.tm_mon], lt.tm_year + 1900,
           lt.tm_hour, lt.tm_min, gt.tm_hour, gt.tm_min);
  return String(buf);
}
