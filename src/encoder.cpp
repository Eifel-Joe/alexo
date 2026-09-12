// ============================================================================
//  ALEXO - Drehgeber (siehe encoder.h).
//  Darunter liegt die Bibliothek Versatile_RotaryEncoder (ruiseixasm): eine
//  robuste Auswertung durch regelmäßiges Abfragen samt vollständiger
//  Behandlung der Tastenereignisse. Die öffentliche Schnittstelle bleibt
//  unverändert (encoderTake / encoderButtonHeld / ...), gobbo.cpp und main.cpp
//  müssen also nicht angefasst werden.
//
//  Die Bibliothek FRAGT AB: sie will häufig mit ReadEncoder() aufgerufen werden.
//  Damit keine Schritte verloren gehen, während der Hauptloop auf Kern 1 im
//  Netzwerk hängt, geschieht das in einer eigenen Aufgabe mit hoher Frequenz
//  (etwa jede Millisekunde). Die Rückrufe laufen in dieser Aufgabe und sammeln
//  in flüchtigen Variablen, die die Schnittstelle ausliest.
// ============================================================================
#include "encoder.h"
#include "config.h"
#include <Versatile_RotaryEncoder.h>

static Versatile_RotaryEncoder *enc = nullptr;

static volatile int32_t encDelta   = 0;   // gesammelte Rastungen (+1/-1 je Schritt)
static volatile bool    btnHeld    = false; // true, solange die Taste gedrückt ist
static volatile bool    clickEvent = false; // bestätigter EINFACHER KLICK (verzögert)
static volatile bool    dblEvent   = false; // DOPPELKLICK

// Einfach oder doppelt unterscheiden: beim Loslassen eines schlichten Drucks
// (ohne Drehung) wird der einfache Klick NICHT sofort gemeldet, sondern eine Zeit
// gestartet. Kommt innerhalb von DOUBLE_WINDOW_MS ein zweiter Klick, ruft die
// Bibliothek onDoublePress auf, der offene einfache Klick entfällt und ein
// doppelter wird gemeldet. Läuft das Fenster ab, gilt der einfache.
#define DOUBLE_WINDOW_MS 350
static volatile bool     rotated       = false;   // beim Drücken gedreht? dann ist es kein Klick
static volatile bool     inDouble      = false;   // dieser Druck gehört zu einem Doppelklick
static volatile bool     pendingSingle = false;
static volatile uint32_t pressDownAt   = 0;       // Zeitpunkt des LETZTEN Drucks (Bezug für das Fenster)

// --- Rückrufe der Bibliothek (laufen in der abfragenden Aufgabe) ------------
static void onRotate(int8_t r)      { encDelta += r; }                 // freies Drehen -> blättern
static void onPressRotate(int8_t r) { encDelta += r; rotated = true; } // gedrückt und gedreht -> Lautstärke
static void onHeldRotate(int8_t r)  { encDelta += r; rotated = true; }

// Das Fenster für den Doppelklick misst ab dem DRÜCKEN, so wie es die Bibliothek
// tut. Bei einem "sauberen" Loslassen (ohne Drehung und nicht als zweiter eines
// Doppelklicks) wird ein einfacher Klick VORGEMERKT, den die Aufgabe nur
// bestätigt, wenn das Fenster ohne zweiten Klick abläuft. Kommt dagegen der
// Doppelklick (onDoublePress), verfällt der vorgemerkte und inDouble wird
// gesetzt, damit das folgende Loslassen NICHT erneut einen einfachen vormerkt.
// Genau das war der Fehler: dabei startete versehentlich der Chat.
static void onPress()               { btnHeld = true; rotated = false; inDouble = false; pressDownAt = millis(); }
static void onPressRelease()        { btnHeld = false; if (!rotated && !inDouble) pendingSingle = true; rotated = false; inDouble = false; }
static void onLongPressRelease()    { btnHeld = false; if (!rotated && !inDouble) pendingSingle = true; rotated = false; inDouble = false; }
static void onPressRotateRelease()  { btnHeld = false; rotated = false; }               // gedreht -> kein Klick
static void onHeldRotateRelease()   { btnHeld = false; rotated = false; }
static void onDoublePress()         { btnHeld = true; pendingSingle = false; dblEvent = true; inDouble = true; }

// --- Abfragende Aufgabe (ruft die Bibliothek etwa jede Millisekunde) --------
static void encoderTask(void *) {
  for (;;) {
    enc->ReadEncoder();
    // Einfachen Klick bestätigen, wenn das Fenster für den Doppelklick um ist
    if (pendingSingle && (millis() - pressDownAt) >= DOUBLE_WINDOW_MS) {
      pendingSingle = false;
      clickEvent = true;
    }
    vTaskDelay(1);   // 1 Tick = 1 ms (FreeRTOS mit 1 kHz); ReadEncoder bremst sich selbst auf 1 ms
  }
}

void encoderBegin() {
  // clk = A (CLK), dt = B (DT), sw = Taste. Die internen Pull-ups setzt die Bibliothek.
  enc = new Versatile_RotaryEncoder(ENC_A_PIN, ENC_B_PIN, ENC_SW_PIN);

  enc->setHandleRotate(onRotate);
  enc->setHandlePressRotate(onPressRotate);
  enc->setHandleHeldRotate(onHeldRotate);
  enc->setHandlePress(onPress);
  enc->setHandleDoublePress(onDoublePress);
  enc->setHandlePressRelease(onPressRelease);
  enc->setHandleLongPressRelease(onLongPressRelease);
  enc->setHandlePressRotateRelease(onPressRotateRelease);
  enc->setHandleHeldRotateRelease(onHeldRotateRelease);
  enc->setDoublePressDuration(DOUBLE_WINDOW_MS);   // Fenster für den Doppelklick

  encDelta = 0; btnHeld = false; clickEvent = false; dblEvent = false; pendingSingle = false; inDouble = false;

  // Schlanke Aufgabe auf Kern 0 (wie Anzeige und Teleprompter); der Loop mit dem
  // Netzwerk liegt auf Kern 1.
  xTaskCreatePinnedToCore(encoderTask, "enc", 2048, nullptr, 2, nullptr, 0);
}

int32_t encoderTake() {
  int32_t d = encDelta;
  encDelta -= d;        // abziehen statt nullsetzen: so gehen zwischenzeitliche Schritte nicht verloren
  return d;
}

bool encoderButtonPressed() {
  if (clickEvent) { clickEvent = false; return true; }
  return false;
}

bool encoderDoublePressed() {
  if (dblEvent) { dblEvent = false; return true; }
  return false;
}

bool encoderButtonHeld() {
  return btnHeld;
}
