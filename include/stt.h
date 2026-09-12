#pragma once
// ============================================================================
//  ALEXO - Speech-to-Text (OpenAI Whisper)
// ============================================================================
#include <Arduino.h>

// Schickt ein WAV (Kopf und PCM) an den Whisper-Endpunkt von OpenAI und
// liefert den erkannten Text. Leere Zeichenkette im Fehlerfall (seriell
// protokolliert). lang = ISO-Code ("de", "en", ...), verbessert die Erkennung.
String sttTranscribe(const uint8_t *wav, size_t wavLen, const char *lang = "de");
