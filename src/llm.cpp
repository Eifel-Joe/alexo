// ============================================================================
//  ALEXO - Gehirn. Zwei Wege, dasselbe Gedaechtnis des Gespraechs:
//    CLOUD  Anthropic Messages API (Claude) samt Websuche auf deren Servern
//    ZU HAUSE  OpenAI-kompatibler Server im eigenen Netz (LM Studio & Co.),
//           ohne Websuche und ohne Schluessel: siehe localai.h
//  Zu Hause wird nur gerechnet, wenn im Panel eine Adresse steht UND der PC
//  antwortet. In jedem anderen Fall (Adresse leer, PC aus, Fehler des lokalen
//  Servers) geht es zurueck in die Cloud - sichtbar in Rot im Chat, und ohne
//  das zu Hause entstandene Gedaechtnis mitzunehmen. Bei eingeschaltetem
//  "nur zu Hause" findet dieser Rueckweg gar nicht statt.
// ============================================================================
#include "llm.h"
#include "secrets.h"
#include "settings.h"
#include "localai.h"
#include "net.h"
#include "music.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

//  Modell und Persoenlichkeit (System-Prompt) sind zur LAUFZEIT aenderbar
//  (gSettings.llmModel / systemPrompt, ueber das Web-Panel); die Werkseinstellung
//  steht in config.h.
#define LLM_MAX_TOKENS 1024            // Luft fuer Denken und Suche
#define WEB_MAX_USES   3               // max. Suchen pro Anfrage (begrenzt die Kosten)
//  Das Modell zu Hause muss bei der ersten Frage womoeglich erst GELADEN werden
//  (Dutzende Sekunden): die Wartezeit ist hier groesser als in der Cloud.
#define LOCAL_TIMEOUT  40000

// --- Speicherverwaltung fuer ArduinoJson im PSRAM ---------------------------
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return ps_malloc(n); }
  void  deallocate(void *p) override { free(p); }
  void *reallocate(void *p, size_t n) override { return ps_realloc(p, n); }
};
static PsramAllocator psramAlloc;

// --- Gedaechtnis des Gespraechs (letzte N Nachrichten) ----------------------
#define MAX_HISTORY 8   // Nachrichten gesamt = 4 Wechsel Nutzer/Assistent
static String histRole[MAX_HISTORY];
static String histText[MAX_HISTORY];
static int    histN = 0;

static void histPush(const char *role, const String &text) {
  if (histN >= MAX_HISTORY) {
    for (int i = 2; i < histN; i++) {
      histRole[i - 2] = histRole[i];
      histText[i - 2] = histText[i];
    }
    histN -= 2;
  }
  histRole[histN] = role;
  histText[histN] = text;
  histN++;
}

void llmReset() { histN = 0; }

// Beschreibung des Musik-Werkzeugs: auf beiden Wegen dieselbe, nur die Art der
// Deklaration unterscheidet sich (Anthropic oder OpenAI).
static String musicToolDesc() {
  return String(
      "Startet Musik oder Radio, wenn der Nutzer Musik HOEREN moechte, auch bei "
      "vagen Wuenschen oder solchen nach Stimmung (etwa \"spiel was Entspanntes\", "
      "\"froehliche Musik\", \"ein bisschen Jazz\"). NICHT verwenden bei Wissensfragen "
      "oder normaler Unterhaltung. Waehle das PASSENDSTE 'genere' aus dieser "
      "Liste: ") + musicCatalogList() + ".";
}

// ============================================================================
//  WEG 1 - CLOUD (Anthropic)
// ============================================================================
static String askAnthropic(const String &sys, String *musicReq, bool *ok) {
  *ok = false;

  JsonDocument req(&psramAlloc);
  req["model"]      = gSettings.llmModel;
  req["max_tokens"] = LLM_MAX_TOKENS;
  req["system"]     = sys;

  // Werkzeug Websuche (laeuft bei Anthropic, Grundvariante fuer Haiku 4.5)
  JsonObject tool = req["tools"].add<JsonObject>();
  tool["type"]     = "web_search_20250305";
  tool["name"]     = "web_search";
  tool["max_uses"] = WEB_MAX_USES;

  // Werkzeug MUSIK (laeuft bei uns): ist es angefordert (musicReq != nullptr),
  // kann Claude Musik starten statt zu antworten. Es waehlt ein Genre aus dem
  // internen Katalog (music.cpp); die URL setzen wir, damit keine erfunden wird.
  if (musicReq) {
    JsonObject mt = req["tools"].add<JsonObject>();
    mt["name"] = "riproduci_musica";
    mt["description"] = musicToolDesc();
    JsonObject sch = mt["input_schema"].to<JsonObject>();
    sch["type"] = "object";
    JsonObject props = sch["properties"].to<JsonObject>();
    JsonObject gp = props["genere"].to<JsonObject>();
    gp["type"] = "string";
    gp["description"] = "das gewaehlte Genre oder die Stimmung aus der Liste";
    JsonArray rq = sch["required"].to<JsonArray>();
    rq.add("genere");
  }

  JsonArray msgs = req["messages"].to<JsonArray>();
  for (int i = 0; i < histN; i++) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"]    = histRole[i];
    m["content"] = histText[i];
  }

  String body;
  serializeJson(req, body);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(25000);

  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, "https://api.anthropic.com/v1/messages");
  http.addHeader("content-type", "application/json");
  http.addHeader("x-api-key", ANTHROPIC_API_KEY);
  http.addHeader("anthropic-version", "2023-06-01");

  Serial.printf("[llm] frage Claude (%s, +Web)...\n", gSettings.llmModel.c_str());
  uint32_t t0 = millis();
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("[llm] Antwort HTTP %d nach %lu ms\n", code, (unsigned long)(millis() - t0));

  if (code != 200) {
    Serial.printf("[llm] Fehler: %s\n", resp.c_str());
    return "";
  }

  // --- Auswertung mit FILTER: nur Textbloecke und stop_reason werden gelesen,
  //     die umfangreichen Suchergebnisse nicht -> spart Speicher ---
  JsonDocument filter;
  filter["stop_reason"]          = true;
  filter["content"][0]["type"]   = true;
  filter["content"][0]["text"]   = true;
  filter["content"][0]["name"]   = true;   // tool_use: Name des Werkzeugs
  filter["content"][0]["input"]  = true;   // tool_use: Argumente (genere)

  JsonDocument doc(&psramAlloc);
  DeserializationError e =
      deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  if (e) {
    Serial.printf("[llm] JSON ungueltig: %s\n", e.c_str());
    return "";
  }

  const char *stop = doc["stop_reason"] | "";
  String out;
  for (JsonObject block : doc["content"].as<JsonArray>()) {
    const char *bt = block["type"] | "";
    if (strcmp(bt, "text") == 0) {
      out += (const char *)(block["text"] | "");
    } else if (musicReq && strcmp(bt, "tool_use") == 0 &&
               strcmp(block["name"] | "", "riproduci_musica") == 0) {
      *musicReq = (const char *)(block["input"]["genere"] | "");
    }
  }
  out.trim();
  Serial.printf("[llm] stop_reason=%s\n", stop);
  *ok = true;
  return out;
}

// ============================================================================
//  WEG 2 - ZU HAUSE (OpenAI-kompatibler Server, etwa LM Studio)
// ============================================================================
static String askLocal(const String &sys, String *musicReq, bool *ok) {
  *ok = false;

  const String base  = localBaseUrl(LOC_LLM);
  const String model = localModelName(LOC_LLM);
  if (model.isEmpty()) return "";     // Server stumm oder ohne Modell: ab in die Cloud

  JsonDocument req(&psramAlloc);
  req["model"]       = model;
  req["max_tokens"]  = LLM_MAX_TOKENS;
  req["temperature"] = gSettings.localLlmTemp;   // im Panel einstellbar
  //  Fast alle Modelle zu Hause "denken" vor der Antwort, und dieses Denken kann
  //  mehrere Sekunden Schweigen kosten. Fuer eine gesprochene Antwort bringt das
  //  nichts und faellt schwer ins Gewicht. Server, die den Parameter nicht kennen,
  //  ignorieren ihn, er kann also immer mitgeschickt werden.
  req["reasoning_effort"] = "none";

  //  Der System-Prompt ist hier eine Nachricht wie jede andere (OpenAI-Format),
  //  und es muss dazugesagt werden, dass kein Internet da ist: die Werksfassung
  //  verspricht eine Websuche, die es zu Hause nicht gibt.
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject sm = msgs.add<JsonObject>();
  sm["role"]    = "system";
  sm["content"] = sys + "\n\nDu hast keinen Zugang zum Internet: wenn du etwas "
                        "nicht weisst, sag es, statt es zu erfinden.";
  for (int i = 0; i < histN; i++) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"]    = histRole[i];
    m["content"] = histText[i];
  }

  if (musicReq) {
    JsonObject t = req["tools"].add<JsonObject>();
    t["type"] = "function";
    JsonObject fn = t["function"].to<JsonObject>();
    fn["name"] = "riproduci_musica";
    fn["description"] = musicToolDesc();
    JsonObject sch = fn["parameters"].to<JsonObject>();
    sch["type"] = "object";
    JsonObject gp = sch["properties"]["genere"].to<JsonObject>();
    gp["type"] = "string";
    gp["description"] = "das gewaehlte Genre oder die Stimmung aus der Liste";
    sch["required"].to<JsonArray>().add("genere");
  }

  String body;
  serializeJson(req, body);

  WiFiClient client;                  // zu Hause ohne TLS
  client.setTimeout(LOCAL_TIMEOUT);

  HTTPClient http;
  http.setTimeout(LOCAL_TIMEOUT);
  http.begin(client, base + "/chat/completions");
  http.addHeader("Content-Type", "application/json");

  Serial.printf("[llm] frage zu Hause (%s)...\n", model.c_str());
  uint32_t t0 = millis();
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("[llm] Antwort von zu Hause HTTP %d nach %lu ms\n",
                code, (unsigned long)(millis() - t0));

  if (code != 200) {
    Serial.printf("[llm] Fehler zu Hause: %s\n", resp.c_str());
    return "";
  }

  JsonDocument filter;
  filter["choices"][0]["message"]["content"]    = true;
  filter["choices"][0]["message"]["tool_calls"] = true;

  JsonDocument doc(&psramAlloc);
  DeserializationError e =
      deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  if (e) {
    Serial.printf("[llm] JSON von zu Hause ungueltig: %s\n", e.c_str());
    return "";
  }

  JsonObject msg = doc["choices"][0]["message"];

  // Musik: im OpenAI-Format kommen die Argumente des Werkzeugs als JSON-TEXT
  // innerhalb der Antwort, sie brauchen deshalb einen zweiten Durchgang.
  if (musicReq) {
    for (JsonObject tc : msg["tool_calls"].as<JsonArray>()) {
      if (strcmp(tc["function"]["name"] | "", "riproduci_musica") != 0) continue;
      JsonDocument args;
      if (!deserializeJson(args, (const char *)(tc["function"]["arguments"] | "{}")))
        *musicReq = (const char *)(args["genere"] | "");
      break;
    }
  }

  String out = localStripThink((const char *)(msg["content"] | ""));
  out.trim();
  *ok = true;
  return out;
}

// Die Modelle streuen Markdown ein, auch wenn der System-Prompt es verbietet
// (die zu Hause besonders): "**Nordrhein-Westfalen**". Aus dem GESPROCHENEN war
// es bereits in tts.cpp entfernt, auf dem Bildschirm blieb es aber stehen, und
// auf dem TFT wie im Panel sieht man jedes Sternchen. Entfernt wird es HIER, an
// der Quelle, mit derselben Zeichenliste wie beim Sprechen: so stimmt das
// Gelesene mit dem Gehoerten ueberein, und der Teleprompter, der mit der Stimme
// mitlaeuft, zeigt nichts an, was nicht gesagt wird. Das Gedaechtnis des
// Gespraechs speichert die gereinigte Fassung.
static String ripuliMarkdown(const String &in) {
  String out;
  out.reserve(in.length());
  for (int i = 0; i < (int)in.length(); i++) {
    char c = in[i];
    if (c == '*' || c == '`' || c == '#' || c == '_') continue;
    out += c;
  }
  return out;
}

// ============================================================================
String llmAsk(const String &userText, String *musicReq) {
  if (userText.isEmpty()) return "";

  // Der Weg dieser Runde wird BEVOR das Gedaechtnis angefasst wird entschieden.
  // Hat er sich gegenueber der letzten Runde geaendert, wird das Gedaechtnis
  // vollstaendig geleert: sonst naehme die erste Frage an die Cloud die zu Hause
  // gefallenen Saetze mit (und umgekehrt). Es gibt nur ein Gedaechtnis, beim
  // Wechsel muss es also zurueckgesetzt werden.
  static bool ultimaInCasa = false;
  const bool inCasa = localReachable(LOC_LLM);
  if (histN && inCasa != ultimaInCasa) {
    Serial.println("[llm] Wegwechsel -> Gedaechtnis des Gespraechs zurueckgesetzt");
    llmReset();
  }
  ultimaInCasa = inCasa;

  histPush("user", userText);

  // System-Prompt samt aktuellem Datum und Uhrzeit (via NTP): ohne das weiss das
  // Modell nicht, "wann" gerade ist, und liegt bei Uhrzeit und Zeitzonen
  // systematisch daneben. Ist die Uhr noch nicht abgeglichen, liefert
  // nowContextString() eine leere Zeichenkette und wir haengen nichts an.
  String sys = gSettings.systemPrompt;
  String nowStr = nowContextString();
  if (nowStr.length()) {
    sys += "\n\nAktuelles Datum und Uhrzeit: " + nowStr +
           ". Nimm DIESE Angaben fuer Fragen nach der aktuellen Uhrzeit; fuer andere "
           "Staedte rechne die Zeit aus der oben genannten UTC und deren Zeitzone aus. "
           "Nutze fuer die Uhrzeit keine Websuche.";
  }

  bool ok = false;
  String out;
  if (inCasa) {
    out = askLocal(sys, musicReq, &ok);
    if (!ok) Serial.println("[llm] Gehirn zu Hause hat nicht geantwortet");
  }
  if (!ok) {
    // "Nur zu Hause": es geht nichts hinaus. Lieber nicht antworten, als die
    // Frage samt mitgefuehrtem Gespraechsteil ins Internet zu schicken.
    if (gSettings.localOnly) {
      localSayBlocked(LOC_LLM);
      if (histN > 0) histN--;              // Frage ohne Antwort: nicht ins Gedaechtnis
      return "";
    }
    // Rueckfall auf die Cloud. Was ZU HAUSE gesagt wurde, darf mit diesem Aufruf
    // nicht hinausgehen: es geht allein mit der jetzigen Frage weiter.
    if (inCasa) { llmReset(); histPush("user", userText); }
    localSayCloud(LOC_LLM);
    out = askAnthropic(sys, musicReq, &ok);
    ultimaInCasa = false;                  // diese Runde endete in der Cloud
  }

  if (!ok) { if (histN > 0) histN--; return ""; }

  // Es hat Musik gewaehlt: keine gesprochene Antwort, das Genre kommt ueber
  // musicReq zurueck. Eine Assistenten-Runde wird trotzdem gespeichert, damit der
  // Wechsel im Gedaechtnis erhalten bleibt.
  if (musicReq && musicReq->length()) {
    histPush("assistant", String("(Musik gestartet: ") + *musicReq + ")");
    return "";
  }

  if (out.isEmpty()) { if (histN > 0) histN--; return ""; }
  out = ripuliMarkdown(out);
  histPush("assistant", out);
  return out;
}
