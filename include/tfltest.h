#pragma once
// ============================================================================
//  ALEXO - Selbsttest für TFLite Micro (Schritt 2 des Weckworts, siehe
//  WAKEWORD.md). Prüft, ob die TFLM-Laufzeitumgebung übersetzt UND auf dem
//  ESP32-S3 läuft, anhand des Testmodells "hello_world" (es lernt sin(x)).
//  Nur aktiv mit TFL_SELFTEST=1.
// ============================================================================
#include <Arduino.h>

// Führt einen Durchgang des Selbsttests aus (Aufbau einmalig, dann einige
// Durchläufe) und gibt Ergebnisse und Rechenzeit über die serielle
// Schnittstelle und Telnet aus. Kann im Loop aufgerufen werden. Ohne Wirkung
// bei TFL_SELFTEST=0.
void tflSelfTest();
