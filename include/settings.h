#pragma once
// ============================================================================
//  ALEXO - Impostazioni RUNTIME modificabili dal pannello web (webui.cpp).
//  I parametri che prima erano #define fissi (config.h / dentro i .cpp) ora
//  vivono in questa struct, caricata dall'NVS all'avvio (default = config.h) e
//  salvata quando l'utente li cambia dal pannello. Cosi' si tarano senza
//  ricompilare. Il volume resta gestito da volume.cpp (gia' in NVS).
// ============================================================================
#include <Arduino.h>

struct AlexoSettings {
  // --- Mic / stop-al-silenzio / LED ---
  uint32_t recSilenceMs;      // silenzio continuo prima dello stop registrazione
  float    recSilenceMargin;  // soglia voce = noiseFloor * margin + floor
  int      recSilenceFloor;   // margine minimo assoluto (RMS)
  float    micLvlMargin;      // LED reattivi: quanto sopra il fondo per accendere
  float    micLvlFloor;       // LED reattivi: margine minimo assoluto
  float    micLvlAttack;      // LED reattivi: velocita' di salita (reattivita') 0..1
  float    micLvlRelease;     // LED reattivi: velocita' di discesa (permanenza) 0..1
  bool     idleReactive;      // LED "ballano" col suono a riposo (on/off)
  bool     chatContinua;      // dopo una risposta riapre il mic (niente wake word ogni volta)

  // --- Wake word "Hey Mycroft" ---
  int      wakeGain;          // guadagno digitale del percorso wake
  int      wakeProbCutoff;    // soglia probabilita' 0..255
  int      wakeWindow;        // ampiezza finestra mobile (1..16)

  // --- Audio ---
  String   voiceId;           // Voice ID ElevenLabs di default
  String   voiceIdAlt;        // Voice ID alternativo (usato col trigger)
  String   voiceTrigger;      // parola iniziale che attiva la voce alternativa (vuoto = off)

  // --- Cervello (Claude) ---
  String   llmModel;          // es. claude-haiku-4-5 / claude-sonnet-5 / claude-opus-5
  String   systemPrompt;      // "personalita'" di Alexo

  // --- Filtro anti-allucinazione Whisper ---
  String   hallucTerms;       // frasi-fantasma da scartare, separate da virgola (vuoto = off)

  // --- Musica (web-radio) ---
  String   musicStations;     // stazioni, una per riga "chiave | nome | url"

  // --- Servizi AI IN CASA (LM Studio & co., vedi localai.h) ---
  //  Indirizzo VUOTO = servizio locale spento -> si va in cloud come sempre.
  //  Nome modello VUOTO = usa quello caricato adesso sul server (glielo chiede).
  String   localLlmUrl;       // cervello, es. http://192.168.1.50:1234/v1
  String   localLlmModel;
  float    localLlmTemp;      // quanto il modello locale "osa" nella scelta delle parole
  String   localSttUrl;       // trascrizione, es. http://192.168.1.50:8001/v1
  String   localSttModel;
  String   localTtsUrl;       // voce, es. http://192.168.1.50:8880/v1
  String   localTtsModel;
  String   localTtsVoice;     // nome voce del server locale (non il Voice ID ElevenLabs)
  bool     localOnly;         // true = mai in cloud per stt/llm/tts: se casa non c'e', tace
  bool     ttsLocalOnly;      // true = voce SEMPRE in casa (niente ElevenLabs), solo il tts

  // --- Risposta personalizzata (trigger + testo) ---
  String   replyTrigger;      // parola/frase nella domanda (vuoto = off -> risponde l'AI)
  String   replyText;         // testo fisso che Alexo dice quando il trigger e' presente
};

extern AlexoSettings gSettings;

void settingsBegin();          // carica da NVS (o default di config.h)
void settingsSave();           // scrive TUTTO in NVS (permanente)
void settingsResetDefaults();  // riporta ai default di fabbrica (config.h) e salva
