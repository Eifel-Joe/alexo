#pragma once
// ============================================================================
//  ALEXO - Weckwort "Hey Jarvis", erkannt im Gerät selbst (microWakeWord).
//  Siehe WAKEWORD.md.
//
//  Ablauf: I2S mit 16 kHz im Dauerbetrieb -> Merkmalsberechnung (40 Mel je
//  10 ms) -> INT8-Modell im Strombetrieb (eine Auswertung alle 20 ms) ->
//  Wahrscheinlichkeit -> Schwelle und Entprellung -> Auslösung (derselbe
//  Eingang wie der Klick auf den Drehgeber). Der Klick bleibt als Rückfallweg.
//
//  Die Erkennung ist vollständig umgesetzt und mit WAKE_ENABLE=1 ab Werk aktiv.
//  Mit WAKE_ENABLE=0 bleibt alles wirkungslos und die Firmware unverändert.
// ============================================================================
#include <Arduino.h>

// Richtet das Weckwort ein (reserviert Speicher im PSRAM, lädt das Modell,
// bereitet die Merkmalsberechnung vor). Liefert false, wenn es nicht zur
// Verfügung steht, etwa bei fehlendem PSRAM oder abgeschaltet.
bool wakeBegin();

// Bei Ruhe mit einem Block PCM-Abtastwerten aufzurufen, 16 Bit Mono mit 16 kHz
// (DENSELBEN, die für den Pegel des Rings gelesen werden, damit der I2S-Bus
// nicht zweimal gelesen wird). Sammelt die Schritte, berechnet die Merkmale,
// führt die Auswertung im Strombetrieb aus und wendet Schwelle und Entprellung
// an. Liefert NUR in dem Frame true, in dem "Hey Jarvis" erkannt wurde (ein
// einziges Mal, danach braucht es eine neue Auslösung).
bool wakeFeed(const int16_t *samples, size_t n);

// true, wenn das Weckwort aktiv und bereit ist (Modell geladen).
bool wakeReady();

// Setzt den Erkennungszustand zurück (Fenster der Wahrscheinlichkeiten und
// Merkmalsberechnung). Nach einem Wortwechsel aufzurufen, bevor das Zuhören
// wieder beginnt, damit es sich nicht selbst erneut auslöst.
void wakeReset();

// Letzte Wahrscheinlichkeit (0..255) aus der jüngsten Auswertung, für Fehlersuche
// und Feinabstimmung.
uint8_t wakeLastProb();

// Test der Kette ohne Mikrofon: erzeugt künstlichen Ton, gibt ihn an wakeFeed
// und schreibt Wahrscheinlichkeiten und Zeiten auf die serielle Schnittstelle
// und Telnet. Kann im Loop aufgerufen werden. Ohne Wirkung, wenn weder
// WAKE_ENABLE noch WAKE_TEST gesetzt ist.
void wakeSelfTest();
