#pragma once
// ============================================================================
//  ALEXO - Text-to-Speech (ElevenLabs) mit Wiedergabe über den VS1053
// ============================================================================
#include <Arduino.h>
#include <VS1053.h>

// Lässt "text" von ElevenLabs sprechen und gibt es als Strom über den VS1053
// aus. Liefert true, wenn Ton abgespielt wurde. Der Player muss bereits
// eingerichtet sein.
// voiceId: die zu verwendende ElevenLabs-Stimme. Bleibt sie leer (""), gilt die
// voreingestellte Stimme.
bool ttsSpeak(VS1053 &player, const String &text, const String &voiceId = "");

// true = die nächste Antwort spricht der Server zu Hause. Das entscheiden die
// Schalter "Stimme immer zu Hause" und "nur zu Hause", nicht die
// Erreichbarkeit.
bool ttsUsesLocal();
