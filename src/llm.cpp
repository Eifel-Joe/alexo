// ============================================================================
//  ALEXO - Cervello. Due strade, stessa memoria della conversazione:
//    CLOUD  Anthropic Messages API (Claude) + ricerca web server-side
//    CASA   server compatibile OpenAI sulla LAN (LM Studio & co.), niente
//           ricerca web e niente chiave: vedi localai.h
//  Si va in casa solo se il pannello ha un indirizzo E il PC risponde; in ogni
//  altro caso (indirizzo vuoto, PC spento, errore del server locale) si torna
//  al cloud - dicendolo in rosso nella chat, e senza portarsi dietro la memoria
//  fatta in casa. Col "solo casa" acceso il ritorno al cloud non avviene affatto.
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

//  Modello e personalita' (system prompt) sono ora RUNTIME (gSettings.llmModel /
//  systemPrompt, modificabili dal pannello web); i default stanno in config.h.
#define LLM_MAX_TOKENS 1024            // headroom per ragionamento + ricerca
#define WEB_MAX_USES   3               // max ricerche per richiesta (limita i costi)
//  Il modello locale puo' dover essere CARICATO al volo dal server alla prima
//  domanda (decine di secondi): l'attesa qui e' piu' larga che sul cloud.
#define LOCAL_TIMEOUT  40000

// --- Allocatore ArduinoJson su PSRAM ----------------------------------------
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return ps_malloc(n); }
  void  deallocate(void *p) override { free(p); }
  void *reallocate(void *p, size_t n) override { return ps_realloc(p, n); }
};
static PsramAllocator psramAlloc;

// --- Memoria della conversazione (ultimi N messaggi) ------------------------
#define MAX_HISTORY 8   // messaggi totali = 4 scambi user/assistant
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

// Descrizione del tool musica: identica sulle due strade, cambia solo il modo di
// dichiararlo (Anthropic o OpenAI).
static String musicToolDesc() {
  return String(
      "Avvia la riproduzione di musica/radio quando l'utente vuole ASCOLTARE "
      "musica, anche con richieste vaghe o per umore (es. \"metti qualcosa di "
      "rilassante\", \"musica allegra\", \"un po' di jazz\"). NON usarlo per "
      "domande informative o conversazione normale. Scegli il 'genere' PIU' "
      "adatto tra questi: ") + musicCatalogList() + ".";
}

// ============================================================================
//  STRADA 1 - CLOUD (Anthropic)
// ============================================================================
static String askAnthropic(const String &sys, String *musicReq, bool *ok) {
  *ok = false;

  JsonDocument req(&psramAlloc);
  req["model"]      = gSettings.llmModel;
  req["max_tokens"] = LLM_MAX_TOKENS;
  req["system"]     = sys;

  // strumento di ricerca web (server-side, variante base per Haiku 4.5)
  JsonObject tool = req["tools"].add<JsonObject>();
  tool["type"]     = "web_search_20250305";
  tool["name"]     = "web_search";
  tool["max_uses"] = WEB_MAX_USES;

  // tool MUSICA (client-side): se richiesto (musicReq != nullptr) Claude puo'
  // avviare la musica invece di rispondere a voce. Sceglie un genere del catalogo
  // interno (music.cpp); l'URL lo mettiamo noi (niente URL inventati).
  if (musicReq) {
    JsonObject mt = req["tools"].add<JsonObject>();
    mt["name"] = "riproduci_musica";
    mt["description"] = musicToolDesc();
    JsonObject sch = mt["input_schema"].to<JsonObject>();
    sch["type"] = "object";
    JsonObject props = sch["properties"].to<JsonObject>();
    JsonObject gp = props["genere"].to<JsonObject>();
    gp["type"] = "string";
    gp["description"] = "il genere o mood scelto tra quelli elencati";
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

  Serial.printf("[llm] chiedo a Claude (%s, +web)...\n", gSettings.llmModel.c_str());
  uint32_t t0 = millis();
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("[llm] risposta HTTP %d in %lu ms\n", code, (unsigned long)(millis() - t0));

  if (code != 200) {
    Serial.printf("[llm] errore: %s\n", resp.c_str());
    return "";
  }

  // --- parsing con FILTRO: estraggo solo i blocchi di testo e stop_reason,
  //     ignorando i risultati di ricerca (voluminosi) -> poca memoria ---
  JsonDocument filter;
  filter["stop_reason"]          = true;
  filter["content"][0]["type"]   = true;
  filter["content"][0]["text"]   = true;
  filter["content"][0]["name"]   = true;   // tool_use: nome del tool
  filter["content"][0]["input"]  = true;   // tool_use: argomenti (genere)

  JsonDocument doc(&psramAlloc);
  DeserializationError e =
      deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  if (e) {
    Serial.printf("[llm] JSON non valido: %s\n", e.c_str());
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
//  STRADA 2 - IN CASA (server compatibile OpenAI, es. LM Studio)
// ============================================================================
static String askLocal(const String &sys, String *musicReq, bool *ok) {
  *ok = false;

  const String base  = localBaseUrl(LOC_LLM);
  const String model = localModelName(LOC_LLM);
  if (model.isEmpty()) return "";     // server muto o senza modelli: si va in cloud

  JsonDocument req(&psramAlloc);
  req["model"]       = model;
  req["max_tokens"]  = LLM_MAX_TOKENS;
  req["temperature"] = gSettings.localLlmTemp;   // regolabile dal pannello
  //  Quasi tutti i modelli locali "ragionano" prima di rispondere, e il pensiero
  //  puo' valere piu' secondi di attesa a bocca chiusa: per una risposta parlata
  //  non serve e si paga caro. I server che non conoscono questo parametro lo
  //  ignorano, quindi si puo' mandare sempre.
  req["reasoning_effort"] = "none";

  //  Il system prompt qui e' un messaggio come gli altri (formato OpenAI), e va
  //  avvisato che internet non c'e': il prompt di fabbrica promette una ricerca
  //  web che in casa non esiste.
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject sm = msgs.add<JsonObject>();
  sm["role"]    = "system";
  sm["content"] = sys + "\n\nNon hai accesso a internet: se non conosci "
                        "un'informazione, dillo invece di inventarla.";
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
    gp["description"] = "il genere o mood scelto tra quelli elencati";
    sch["required"].to<JsonArray>().add("genere");
  }

  String body;
  serializeJson(req, body);

  WiFiClient client;                  // in casa niente TLS
  client.setTimeout(LOCAL_TIMEOUT);

  HTTPClient http;
  http.setTimeout(LOCAL_TIMEOUT);
  http.begin(client, base + "/chat/completions");
  http.addHeader("Content-Type", "application/json");

  Serial.printf("[llm] chiedo in casa (%s)...\n", model.c_str());
  uint32_t t0 = millis();
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("[llm] risposta locale HTTP %d in %lu ms\n",
                code, (unsigned long)(millis() - t0));

  if (code != 200) {
    Serial.printf("[llm] errore locale: %s\n", resp.c_str());
    return "";
  }

  JsonDocument filter;
  filter["choices"][0]["message"]["content"]    = true;
  filter["choices"][0]["message"]["tool_calls"] = true;

  JsonDocument doc(&psramAlloc);
  DeserializationError e =
      deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  if (e) {
    Serial.printf("[llm] JSON locale non valido: %s\n", e.c_str());
    return "";
  }

  JsonObject msg = doc["choices"][0]["message"];

  // Musica: nel formato OpenAI gli argomenti del tool arrivano come TESTO JSON
  // dentro la risposta, quindi vanno letti con un secondo passaggio.
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

// I modelli infilano markdown anche quando il system prompt dice di non farlo (i
// locali soprattutto): "**Emilia-Romagna**". Dal PARLATO era gia' tolto in
// tts.cpp, ma a video restava, e sul TFT e nel pannello gli asterischi si vedono
// tutti. Si tolgono QUI, alla fonte, con la stessa lista di caratteri del
// parlato: cosi' quello che si legge e quello che si sente coincidono - e il
// teleprompter, che scorre insieme alla voce, non mostra roba che non viene
// detta. La memoria della conversazione salva la versione ripulita.
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

  // Strada di questo turno, decisa PRIMA di toccare la memoria. Se e' cambiata
  // rispetto al turno scorso la memoria si svuota del tutto: senza, la prima
  // domanda fatta al cloud si porterebbe dietro le battute dette in casa (e
  // viceversa). La memoria e' una sola, quindi va azzerata al cambio.
  static bool ultimaInCasa = false;
  const bool inCasa = localReachable(LOC_LLM);
  if (histN && inCasa != ultimaInCasa) {
    Serial.println("[llm] cambio strada -> memoria della conversazione azzerata");
    llmReset();
  }
  ultimaInCasa = inCasa;

  histPush("user", userText);

  // System prompt + data/ora attuali (via NTP): senza, il modello non sa "quando"
  // e' adesso e sbaglia sistematicamente ora e fusi. Se l'orologio non e' ancora
  // sincronizzato, nowContextString() torna vuoto e non aggiungiamo nulla.
  String sys = gSettings.systemPrompt;
  String nowStr = nowContextString();
  if (nowStr.length()) {
    sys += "\n\nData e ora attuali: " + nowStr +
           ". Usa QUESTE per le domande sull'ora corrente; per altre citta' calcola "
           "l'ora dall'UTC qui sopra applicando il loro fuso. Non usare la ricerca web per l'ora.";
  }

  bool ok = false;
  String out;
  if (inCasa) {
    out = askLocal(sys, musicReq, &ok);
    if (!ok) Serial.println("[llm] cervello in casa non ha risposto");
  }
  if (!ok) {
    // "Solo casa": non si esce. Meglio non rispondere che mandare la domanda
    // (e il pezzo di conversazione che si porta dietro) su internet.
    if (gSettings.localOnly) {
      localSayBlocked(LOC_LLM);
      if (histN > 0) histN--;              // domanda senza risposta: fuori dalla memoria
      return "";
    }
    // Ripiego sul cloud. Quello che era stato detto IN CASA non deve uscire con
    // questa chiamata: si riparte dalla sola domanda di adesso.
    if (inCasa) { llmReset(); histPush("user", userText); }
    localSayCloud(LOC_LLM);
    out = askAnthropic(sys, musicReq, &ok);
    ultimaInCasa = false;                  // questo turno e' finito in cloud
  }

  if (!ok) { if (histN > 0) histN--; return ""; }

  // Ha scelto la musica: niente risposta a voce, torna il genere via musicReq.
  // Salvo comunque un turno assistant (per l'alternanza in memoria).
  if (musicReq && musicReq->length()) {
    histPush("assistant", String("(avviata la musica: ") + *musicReq + ")");
    return "";
  }

  if (out.isEmpty()) { if (histN > 0) histN--; return ""; }
  out = ripuliMarkdown(out);
  histPush("assistant", out);
  return out;
}
