#pragma once
// ============================================================================
//  ALEXO - WiFi
// ============================================================================
#include <Arduino.h>

// Verbindet mit dem WLAN (Zugangsdaten in secrets.h). Liefert true, wenn die
// Verbindung innerhalb von timeoutMs steht. Zeigt den Fortschritt seriell an.
bool wifiBegin(uint32_t timeoutMs = 15000);

// true, wenn gerade verbunden.
bool wifiOk();

// Startet den Abgleich der Uhr via NTP mit DEUTSCHER Zeitzone (Sommerzeit
// automatisch). Einmal nach dem Verbinden aufrufen. Blockiert nicht: die
// Uhrzeit trifft erst nach einigen Sekunden ein.
void timeBegin();

// Liefert Datum und Uhrzeit auf Deutsch samt Zeitzone und UTC, fertig für
// Claude, etwa "Dienstag, 1. Juli 2026, 21:35 Uhr Ortszeit (Europe/Berlin)".
// LEERE Zeichenkette, solange die Uhr nicht abgeglichen ist (NTP hat noch
// nicht geantwortet).
String nowContextString();
