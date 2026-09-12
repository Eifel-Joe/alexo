#pragma once
// ============================================================================
//  ALEXO - Lichtanzeige: Animationen des NeoPixel-Rings, gesteuert über einen
//  Zustand. Läuft in einer eigenen Aufgabe auf KERN 0, damit die Animationen
//  flüssig bleiben, auch während Kern 1 (der Hauptloop) bei Spracherkennung,
//  Claude oder Sprachausgabe wartet.
// ============================================================================
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

enum AlexoState {
  ST_IDLE,       // Ruhe: Regenbogen, der atmet
  ST_LISTENING,  // Zuhören: Aussteuerungsanzeige vom Mikrofon
  ST_THINKING,   // Verarbeitung: umlaufender Komet
  ST_SPEAKING,   // Sprechen: Pulsieren
  ST_ERROR,      // Fehler: rotes Blinken
  ST_OTA,        // Aktualisierung über Funk: grüner Komet
  ST_MUSIC,      // Musik: bunter Regenbogen, reagiert auf den Pegel
  ST_FOLLOWUP    // Fortlaufender Chat: "du bist dran", bernsteinfarbenes Atmen,
                 // bis gesprochen wird
};
// Letzter Wert der Aufzählung: wer Tabellen nach Zustand durchgeht (gobbo),
// hört hier auf.
#define ST_LAST ST_FOLLOWUP

// Startet die Animationsaufgabe (Kern 0). Der Ring muss bereits eingerichtet sein.
void uiBegin(Adafruit_NeoPixel *ring);

// Wechselt den animierten Zustand (threadsicher, blockiert nicht).
void uiSetState(AlexoState s);

// Pegel 0..255 für die Aussteuerungsanzeige beim Zuhören.
void uiSetLevel(uint8_t level);
