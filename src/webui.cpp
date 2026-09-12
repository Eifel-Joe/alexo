// ============================================================================
//  ALEXO - Einstellungs-Panel im Browser (siehe webui.h).
//  Synchroner Webserver (der mitgelieferte WebServer) auf Port 80. Die Seite
//  liegt in LittleFS (data/index.html). Die JSON-Schnittstelle:
//    GET  /api/settings  -> alle aktuellen Werte samt Lautstärke
//    POST /api/settings  -> ändert die im Rumpf enthaltenen Werte und speichert
//                           sie im NVS
//    GET  /api/live      -> laufende Werte des Mikrofons (Pegel, Grundpegel,
//                           Schwelle) samt Zustand
//    POST /api/reset     -> stellt die Werkseinstellung wieder her
//    POST /api/music/start-> schaltet das Radio ein (erster Sender der Liste)
//    POST /api/music/seek-> wechselt den Sender (+1 vor / -1 zurück)
//    POST /api/local/test-> prüft die KI-Dienste zu Hause: wer antwortet und mit
//                           welchem Modell
//  Der Server arbeitet synchron: während eines Wortwechsels (Aufnahme oder Netz)
//  ist der Loop blockiert und die Seite antwortet einige Sekunden lang nicht.
//  Das ist normal, das Panel benutzt man bei Ruhe.
// ============================================================================
#include "webui.h"
#include "config.h"
#include "settings.h"
#include "localai.h"
#include "volume.h"
#include "mic.h"
#include "wakeword.h"
#include "music.h"
#include "tts.h"      // ttsUsesLocal(): geht die Stimme wirklich dort hindurch?
#include "gobbo.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static WebServer server(80);

// --- gSettings samt Lautstärke in ein JSON-Objekt schreiben -----------------
static void fillSettingsJson(JsonDocument &doc) {
  doc["recSilenceMs"]     = gSettings.recSilenceMs;
  doc["recSilenceMargin"] = gSettings.recSilenceMargin;
  doc["recSilenceFloor"]  = gSettings.recSilenceFloor;
  doc["micLvlMargin"]     = gSettings.micLvlMargin;
  doc["micLvlFloor"]      = gSettings.micLvlFloor;
  doc["micLvlAttack"]     = gSettings.micLvlAttack;
  doc["micLvlRelease"]    = gSettings.micLvlRelease;
  doc["idleReactive"]     = gSettings.idleReactive;
  doc["chatContinua"]     = gSettings.chatContinua;
  doc["wakeGain"]         = gSettings.wakeGain;
  doc["wakeProbCutoff"]   = gSettings.wakeProbCutoff;
  doc["wakeWindow"]       = gSettings.wakeWindow;
  doc["voiceId"]          = gSettings.voiceId;
  doc["voiceIdAlt"]       = gSettings.voiceIdAlt;
  doc["voiceTrigger"]     = gSettings.voiceTrigger;
  doc["llmModel"]         = gSettings.llmModel;
  doc["systemPrompt"]     = gSettings.systemPrompt;
  doc["hallucTerms"]      = gSettings.hallucTerms;
  doc["musicStations"]    = gSettings.musicStations;
  doc["replyTrigger"]     = gSettings.replyTrigger;
  doc["replyText"]        = gSettings.replyText;
  doc["localLlmUrl"]      = gSettings.localLlmUrl;
  doc["localLlmModel"]    = gSettings.localLlmModel;
  doc["localLlmTemp"]     = gSettings.localLlmTemp;
  doc["localSttUrl"]      = gSettings.localSttUrl;
  doc["localSttModel"]    = gSettings.localSttModel;
  doc["localTtsUrl"]      = gSettings.localTtsUrl;
  doc["localTtsModel"]    = gSettings.localTtsModel;
  doc["localTtsVoice"]    = gSettings.localTtsVoice;
  doc["localOnly"]        = gSettings.localOnly;
  doc["ttsLocalOnly"]     = gSettings.ttsLocalOnly;
  doc["volume"]           = volumeGet();
}

static void handleGetSettings() {
  JsonDocument doc;
  fillSettingsJson(doc);
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handlePostSettings() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "JSON ungueltig"); return;
  }
  // Ändert nur die enthaltenen Felder, das Panel darf auch eine Auswahl senden.
  if (!doc["recSilenceMs"].isNull())     gSettings.recSilenceMs     = doc["recSilenceMs"].as<uint32_t>();
  if (!doc["recSilenceMargin"].isNull()) gSettings.recSilenceMargin = doc["recSilenceMargin"].as<float>();
  if (!doc["recSilenceFloor"].isNull())  gSettings.recSilenceFloor  = doc["recSilenceFloor"].as<int>();
  if (!doc["micLvlMargin"].isNull())     gSettings.micLvlMargin     = doc["micLvlMargin"].as<float>();
  if (!doc["micLvlFloor"].isNull())      gSettings.micLvlFloor      = doc["micLvlFloor"].as<float>();
  if (!doc["micLvlAttack"].isNull())     gSettings.micLvlAttack     = doc["micLvlAttack"].as<float>();
  if (!doc["micLvlRelease"].isNull())    gSettings.micLvlRelease    = doc["micLvlRelease"].as<float>();
  if (!doc["idleReactive"].isNull())     gSettings.idleReactive     = doc["idleReactive"].as<bool>();
  if (!doc["chatContinua"].isNull())     gSettings.chatContinua     = doc["chatContinua"].as<bool>();
  if (!doc["wakeGain"].isNull())         gSettings.wakeGain         = doc["wakeGain"].as<int>();
  if (!doc["wakeProbCutoff"].isNull())   gSettings.wakeProbCutoff   = doc["wakeProbCutoff"].as<int>();
  if (!doc["wakeWindow"].isNull())       gSettings.wakeWindow       = doc["wakeWindow"].as<int>();
  if (!doc["voiceId"].isNull())          gSettings.voiceId          = doc["voiceId"].as<String>();
  if (!doc["voiceIdAlt"].isNull())       gSettings.voiceIdAlt       = doc["voiceIdAlt"].as<String>();
  if (!doc["voiceTrigger"].isNull())     gSettings.voiceTrigger     = doc["voiceTrigger"].as<String>();
  if (!doc["llmModel"].isNull())         gSettings.llmModel         = doc["llmModel"].as<String>();
  if (!doc["systemPrompt"].isNull())     gSettings.systemPrompt     = doc["systemPrompt"].as<String>();
  if (!doc["hallucTerms"].isNull())      gSettings.hallucTerms      = doc["hallucTerms"].as<String>();
  if (!doc["musicStations"].isNull())    gSettings.musicStations    = doc["musicStations"].as<String>();
  if (!doc["replyTrigger"].isNull())     gSettings.replyTrigger     = doc["replyTrigger"].as<String>();
  if (!doc["replyText"].isNull())        gSettings.replyText        = doc["replyText"].as<String>();
  if (!doc["localLlmUrl"].isNull())      gSettings.localLlmUrl      = doc["localLlmUrl"].as<String>();
  if (!doc["localLlmModel"].isNull())    gSettings.localLlmModel    = doc["localLlmModel"].as<String>();
  if (!doc["localLlmTemp"].isNull())     gSettings.localLlmTemp     = doc["localLlmTemp"].as<float>();
  if (!doc["localSttUrl"].isNull())      gSettings.localSttUrl      = doc["localSttUrl"].as<String>();
  if (!doc["localSttModel"].isNull())    gSettings.localSttModel    = doc["localSttModel"].as<String>();
  if (!doc["localTtsUrl"].isNull())      gSettings.localTtsUrl      = doc["localTtsUrl"].as<String>();
  if (!doc["localTtsModel"].isNull())    gSettings.localTtsModel    = doc["localTtsModel"].as<String>();
  if (!doc["localTtsVoice"].isNull())    gSettings.localTtsVoice    = doc["localTtsVoice"].as<String>();
  if (!doc["localOnly"].isNull())        gSettings.localOnly        = doc["localOnly"].as<bool>();
  if (!doc["ttsLocalOnly"].isNull())     gSettings.ttsLocalOnly     = doc["ttsLocalOnly"].as<bool>();
  if (!doc["volume"].isNull())           volumeSet(doc["volume"].as<int>());

  settingsSave();   // begrenzt die Werte und schreibt sie ins NVS

  // Mit dem neuen Zustand antworten, damit das Panel die begrenzten Werte sieht.
  JsonDocument out; fillSettingsJson(out);
  String s; serializeJson(out, s);
  server.send(200, "application/json", s);
}

static void handleLive() {
  uint8_t level = 0; float floorV = 0, thresh = 0;
  micGetLive(&level, &floorV, &thresh);
  JsonDocument doc;
  doc["level"]    = level;                 // aktueller LED-Pegel 0..255
  doc["floor"]    = floorV;                // geschätztes Grundrauschen (LED)
  doc["thresh"]   = thresh;                // Einschaltschwelle der LED
  doc["wakeProb"] = wakeLastProb();        // letzte Wahrscheinlichkeit des Weckworts 0..255
  doc["heap"]     = ESP.getFreeHeap();
  doc["rssi"]     = WiFi.RSSI();
  doc["music"]    = musicIsPlaying();      // true, wenn ein Sender läuft
  doc["musMad"]   = musicLastMad();        // roher MAD-Wert der Musik (zum Abstimmen des Rings)
  doc["musBase"]  = musicLastBase();       // gleitende Grundlinie der Musik
  doc["station"]  = musicStation();        // Name des Senders (ICY-Metadaten)
  doc["nowPlaying"] = musicNowPlaying();   // laufender Titel "Interpret - Titel"
  doc["chatRev"]  = gobboChatRev();        // ändert sich bei jeder Nachricht -> das Panel lädt den Chat neu
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// GET /api/chat -> die letzten Nachrichten des Chats (desselben, durch den man
// auf dem TFT blättert) als JSON.
// Ausgeliefert wird STÜCKWEISE, eine Nachricht nach der anderen. So muss im RAM
// keine Zeichenkette entstehen, die so gross ist wie der ganze Chat (bis zu
// 40 mal 2 KB). Das JSON hat die Form [{"r":Rolle,"t":"Text"}].
static void handleChat() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  int n = gobboChatCount();
  String item;
  for (int i = 0; i < n; i++) {
    uint8_t role; const char *t;
    if (!gobboChatItem(i, &role, &t)) break;
    item = i ? "," : "";
    item += "{\"r\":"; item += (int)role; item += ",\"t\":\"";
    for (const char *p = t; *p; p++) {                 // JSON-Maskierung (UTF-8 bleibt erhalten)
      unsigned char c = (unsigned char)*p;
      if (c == '"' || c == '\\') { item += '\\'; item += (char)c; }
      else if (c == '\n')        { item += "\\n"; }
      else if (c >= 0x20)        { item += (char)c; }
    }
    item += "\"}";
    server.sendContent(item);
  }
  server.sendContent("]");
  server.sendContent("");   // beendet die stückweise Übertragung
}

// POST /api/local/test -> prüft die drei Dienste zu Hause und meldet, wer
// antwortet und mit welchem Modell. Nur auf Anforderung und nicht selbsttätig:
// jede Prüfung kostet Wartezeit, und sie ständig auszuführen würde den Loop
// bremsen, das Weckwort eingeschlossen.
static void handleLocalTest() {
  localForget();                     // keine alten Ergebnisse: es wird wirklich neu geprüft
  JsonDocument doc;
  const char *nome[LOC_COUNT] = { "stt", "llm", "tts" };
  for (int i = 0; i < LOC_COUNT; i++) {
    LocalSvc s = (LocalSvc)i;
    JsonObject o = doc[nome[i]].to<JsonObject>();
    if (localBaseUrl(s).isEmpty()) { o["stato"] = "aus"; continue; }
    if (!localConnected(s)) { o["stato"] = "antwortet nicht"; continue; }
    // Er antwortet. Die eigentliche Frage ist jetzt, ob sich damit arbeiten
    // lässt. Ein Gehirn ohne geladenes Modell nützt nichts, und das gehört
    // gesagt, statt ein "antwortet" zu zeigen, das in der Praxis doch in der
    // Cloud endet.
    String m = localModelName(s);
    // Die STIMME antwortet vielleicht, wird aber nicht benutzt: ohne einen der
    // beiden Schalter spricht ElevenLabs trotzdem, und hier "zu Hause" zu
    // schreiben wäre dieselbe Unwahrheit wie der grüne Punkt (siehe localOn).
    if (s == LOC_TTS && !ttsUsesLocal())
      o["stato"] = "antwortet, aber die Stimme geht zu ElevenLabs (Schalter aus)";
    else
      o["stato"] = localReachable(s) ? "zu Hause" : "antwortet, aber kein Modell geladen";
    o["modello"] = m.length() ? m : String("(nicht genannt)");
    // Die tatsächlich benutzte Adresse: bei erratenem Port (Stimme) ist das der
    // einzige Weg zu erfahren, ob Kokoro oder Chatterbox geantwortet hat.
    o["indirizzo"] = localBaseUsed(s);
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// POST /api/music/stop -> hält die laufende Musik an (Schaltfläche im Panel).
static void handleMusicStop() {
  musicRequestStop();
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/music/start -> schaltet das Radio auf den ersten Sender der Liste.
// Antwortet SOFORT: die Wiedergabe startet der Loop in main.cpp, denn musicPlay
// blockiert Kern 1, solange das Radio läuft, und die HTTP-Bearbeitung käme nie
// zum Antworten. 409, wenn bereits etwas läuft oder die Liste leer ist.
static void handleMusicStart() {
  if (musicIsPlaying()) {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"Radio laeuft bereits\"}");
    return;
  }
  if (musicStationCount() <= 0) {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"kein Sender in der Liste\"}");
    return;
  }
  musicRequestStart();
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/music/seek -> nächster oder vorheriger Sender (Schaltflächen im
// Panel). Der Rumpf lautet {"d":1} oder {"d":-1}. Nur während ein Sender läuft:
// es ist ein Senderwechsel, kein Startbefehl. Musik fordert man per Sprache an.
static void handleMusicSeek() {
  int d = 1;
  if (server.hasArg("plain")) {
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain")) && !doc["d"].isNull())
      d = doc["d"].as<int>();
  }
  if (!musicIsPlaying()) {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"Radio laeuft nicht\"}");
    return;
  }
  musicRequestSeek(d >= 0 ? 1 : -1);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleReset() {
  settingsResetDefaults();
  volumeSet(VOLUME_DEFAULT);
  JsonDocument doc; fillSettingsJson(doc);
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

bool webuiBegin() {
  bool fs = LittleFS.begin(true);   // true = formatieren, wenn das Einbinden fehlschlägt
  if (!fs) Serial.println("[web] LittleFS nicht eingebunden (Seite fehlt, Schnittstelle arbeitet)");

  // Schnittstelle
  server.on("/api/settings", HTTP_GET,  handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/live",     HTTP_GET,  handleLive);
  server.on("/api/chat",     HTTP_GET,  handleChat);
  server.on("/api/reset",    HTTP_POST, handleReset);
  server.on("/api/music/start", HTTP_POST, handleMusicStart);
  server.on("/api/music/stop", HTTP_POST, handleMusicStop);
  server.on("/api/music/seek", HTTP_POST, handleMusicSeek);
  server.on("/api/local/test", HTTP_POST, handleLocalTest);

  // Unveränderliche Seite aus LittleFS (data/index.html).
  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/index.html", LittleFS, "/index.html");

  server.onNotFound([]() {
    // Rückfall: fehlt die Seite in LittleFS, wenigstens ein brauchbarer Hinweis.
    if (LittleFS.exists("/index.html")) { server.send(404, "text/plain", "not found"); return; }
    server.send(200, "text/html",
      "<h3>ALEXO</h3><p>Die Seite liegt nicht in LittleFS. Dateisystem uebertragen: "
      "<code>pio run -t uploadfs</code>. Die JSON-Schnittstelle arbeitet unter "
      "/api/settings.</p>");
  });

  server.begin();
  Serial.println("[web] Panel bereit: http://alexo.local/");
  return fs;
}

void webuiHandle() { server.handleClient(); }
