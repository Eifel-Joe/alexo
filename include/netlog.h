#pragma once
// ============================================================================
//  ALEXO - Protokoll über das Netz (Telnet, Port 23). Damit lässt sich die
//  Ausgabe lesen, wenn die USB-Buchse nicht zugänglich ist (Gerät im Gehäuse
//  verbaut): man verbindet sich mit `telnet alexo.local` (oder PuTTY im Modus
//  Raw/Telnet auf alexo.local:23) und sieht die Zeilen, die mit
//  netlogPrintln() gesendet wurden.
//
//  Es ist so gebaut, dass es NICHT blockiert und mit ArduinoOTA ZUSAMMENARBEITET:
//  netlogHandle() gehört häufig in den Loop, neben ArduinoOTA.handle(). Die
//  Aktualisierung über Funk hat immer Vorrang und bleibt funktionsfähig.
// ============================================================================
#include <Arduino.h>

// Startet den Telnet-Server auf dem angegebenen Port (Voreinstellung 23).
// Mehrfacher Aufruf schadet nicht.
void netlogBegin(uint16_t port = 23);

// Nimmt den Client an und hält ihn (blockiert nicht). Gehört in den Loop.
void netlogHandle();

// Sendet eine Zeile an den Telnet-Client, sofern einer verbunden ist (sonst
// ohne Wirkung).
void netlogPrintln(const char *s);

// true, wenn ein Telnet-Client verbunden ist.
bool netlogConnected();
