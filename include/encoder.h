#pragma once
// ============================================================================
//  ALEXO - Drehgeber zum Blättern im Chat auf dem Display.
//  Quadraturauswertung über Interrupts (zählt auch weiter, während der Loop auf
//  Kern 1 im Netzwerk hängt). Die Anschlüsse stehen in config.h.
// ============================================================================
#include <Arduino.h>

// Richtet Anschlüsse und Interrupts des Drehgebers ein.
void encoderBegin();

// Rastungen seit dem letzten Aufruf: >0 in die eine Richtung, <0 in die andere,
// 0 bei Stillstand.
int32_t encoderTake();

// EINMAL true bei einem bestätigten EINFACHEN KLICK. Achtung: er ist um etwa
// 280 ms "verzögert", um ihn vom Doppelklick zu unterscheiden (siehe
// encoderDoublePressed). Kommt in diesem Fenster ein zweiter Klick, wird KEIN
// einfacher gemeldet, sondern ein doppelter. Klicks mit Drehung
// (gedrückt und gedreht = Lautstärke) zählen nicht als Klick.
bool encoderButtonPressed();

// EINMAL true bei einem DOPPELKLICK (zwei schnelle Betätigungen). Dient als
// Umschalter.
bool encoderDoublePressed();

// true, solange die Taste gedrückt gehalten wird (sofortige Abfrage, Pull-up:
// gedrückt = LOW). Damit lässt sich freies Drehen von "gedrückt und gedreht"
// unterscheiden.
bool encoderButtonHeld();
