#pragma once
// ============================================================================
//  ALEXO - Einstellungs-Panel im Browser (http://alexo.local/).
//  Webserver auf Port 80 (mitgelieferter WebServer, synchron): liefert die
//  Seite aus LittleFS (Ordner data/) und bietet eine kleine JSON-Schnittstelle
//  zum Lesen und Schreiben der Parameter (gSettings und Lautstärke) sowie für
//  die Live-Anzeige des Mikrofons.
//  webuiHandle() muss häufig aus dem Loop aufgerufen werden (neben
//  ArduinoOTA.handle()).
// ============================================================================
#include <Arduino.h>

// Bindet LittleFS ein und startet den Webserver. Liefert false, wenn LittleFS
// nicht eingebunden werden kann (dann fehlt die Seite, die JSON-Schnittstelle
// arbeitet aber weiter).
bool webuiBegin();

// Im Loop aufzurufen: bearbeitet eintreffende HTTP-Anfragen.
void webuiHandle();
