#pragma once
// ============================================================================
//  ALEXO - Einstellungen, die zur LAUFZEIT über das Web-Panel änderbar sind
//  (webui.cpp).
//  Die Parameter, die früher feste #define waren (in config.h oder in den
//  .cpp-Dateien), leben nun in dieser Struktur. Sie wird beim Start aus dem NVS
//  geladen (Werkseinstellung = config.h) und gespeichert, sobald der Nutzer sie
//  im Panel ändert. So lässt sich alles abstimmen, ohne neu zu übersetzen. Die
//  Lautstärke verwaltet weiterhin volume.cpp (ebenfalls im NVS).
// ============================================================================
#include <Arduino.h>

struct AlexoSettings {
  // --- Mikrofon, Abbruch bei Stille, LED ---
  uint32_t recSilenceMs;      // ununterbrochene Stille vor dem Abbruch der Aufnahme
  float    recSilenceMargin;  // Sprachschwelle = Grundpegel * margin + floor
  int      recSilenceFloor;   // kleinster absoluter Abstand (RMS)
  float    micLvlMargin;      // reagierende LED: wie weit über dem Grundpegel zum Einschalten
  float    micLvlFloor;       // reagierende LED: kleinster absoluter Abstand
  float    micLvlAttack;      // reagierende LED: Anstiegsgeschwindigkeit 0..1
  float    micLvlRelease;     // reagierende LED: Abklinggeschwindigkeit 0..1
  bool     idleReactive;      // LED "tanzen" bei Ruhe zum Ton (ein/aus)
  bool     chatContinua;      // nach einer Antwort öffnet das Mikrofon erneut (kein Weckwort jedes Mal)

  // --- Weckwort "Hey Jarvis" ---
  int      wakeGain;          // digitale Verstärkung des Weckwort-Wegs
  int      wakeProbCutoff;    // Wahrscheinlichkeitsschwelle 0..255
  int      wakeWindow;        // Breite des gleitenden Fensters (1..16)

  // --- Ton ---
  String   voiceId;           // voreingestellte ElevenLabs-Stimme
  String   voiceIdAlt;        // zweite Stimme (mit dem Auslösewort)
  String   voiceTrigger;      // Anfangswort, das die zweite Stimme aktiviert (leer = aus)

  // --- Gehirn (Claude) ---
  String   llmModel;          // etwa claude-haiku-4-5 / claude-sonnet-5 / claude-opus-5
  String   systemPrompt;      // die "Persönlichkeit" von Alexo

  // --- Filter gegen Halluzinationen von Whisper ---
  String   hallucTerms;       // Geisterphrasen zum Verwerfen, durch Komma getrennt (leer = aus)

  // --- Musik (Webradio) ---
  String   musicStations;     // Sender, einer je Zeile: "Schlüssel | Name | URL"

  // --- Dienste ZU HAUSE (LM Studio und ähnliche, siehe localai.h) ---
  //  LEERE Adresse = Dienst zu Hause aus -> es geht wie immer in die Cloud.
  //  LEERER Modellname = das gerade auf dem Server geladene Modell wird benutzt
  //  (es wird dort erfragt).
  String   localLlmUrl;       // Gehirn, etwa http://192.168.1.50:1234/v1
  String   localLlmModel;
  float    localLlmTemp;      // wie viel das Modell zu Hause bei der Wortwahl "wagt"
  String   localSttUrl;       // Spracherkennung, etwa http://192.168.1.50:8001/v1
  String   localSttModel;
  String   localTtsUrl;       // Stimme, etwa http://192.168.1.50:8880/v1
  String   localTtsModel;
  String   localTtsVoice;     // Stimmenname des Servers zu Hause (nicht die ElevenLabs-Kennung)
  bool     localOnly;         // true = nie in die Cloud für Erkennung/Gehirn/Stimme: fehlt der Dienst zu Hause, bleibt es still
  bool     ttsLocalOnly;      // true = Stimme IMMER zu Hause (kein ElevenLabs), betrifft nur die Sprachausgabe

  // --- Eigene Antwort (Auslöser und Text) ---
  String   replyTrigger;      // Wort oder Satzteil in der Frage (leer = aus -> es antwortet die KI)
  String   replyText;         // fester Text, den Alexo sagt, wenn der Auslöser vorkommt
};

extern AlexoSettings gSettings;

void settingsBegin();          // lädt aus dem NVS (oder die Werte aus config.h)
void settingsSave();           // schreibt ALLES ins NVS (dauerhaft)
void settingsResetDefaults();  // stellt die Werkseinstellung wieder her (config.h) und speichert
