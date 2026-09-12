#pragma once
// ============================================================================
//  ALEXO - Gehirn (Claude / Anthropic Messages API)
// ============================================================================
#include <Arduino.h>

// Schickt den Text des Nutzers samt Gesprächsverlauf an Claude und liefert die
// Antwort. Leere Zeichenkette im Fehlerfall (seriell protokolliert).
// Ist 'musicReq' != nullptr, steht Claude das Werkzeug "riproduci_musica" zur
// Verfügung: Will der Nutzer Musik hören, ruft Claude statt einer gesprochenen
// Antwort das Werkzeug auf, und das gewählte GENRE landet in *musicReq (die
// Textantwort bleibt dann leer). Leer = kein Musikwunsch.
String llmAsk(const String &userText, String *musicReq = nullptr);

// Leert den Gesprächsverlauf (es geht bei null weiter).
void llmReset();
