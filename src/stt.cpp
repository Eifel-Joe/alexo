// ============================================================================
//  ALEXO - Speech-to-Text
//  Costruisce un body multipart/form-data (in PSRAM) con il WAV e lo invia a un
//  endpoint in formato OpenAI /audio/transcriptions, poi estrae il campo "text".
//  Due strade, stesso identico formato:
//    CLOUD  Groq Whisper (gratis)
//    CASA   server Whisper sulla LAN (vedi localai.h), senza chiave
//  Si va in casa solo se il pannello ha un indirizzo E il PC risponde; se il
//  server locale sbaglia si ritenta in cloud, dicendolo in rosso nella chat.
//  Col "solo casa" acceso il ritentativo non c'e': il WAV della voce non esce.
// ============================================================================
#include "stt.h"
#include "secrets.h"
#include "settings.h"
#include "localai.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- Provider STT cloud (Groq, gratis, API compatibile OpenAI) --------------
// Per passare a OpenAI: URL "https://api.openai.com/v1/audio/transcriptions",
// MODEL "whisper-1", KEY OPENAI_API_KEY.
#define STT_URL     "https://api.groq.com/openai/v1/audio/transcriptions"
#define STT_MODEL   "whisper-large-v3-turbo"
#define STT_API_KEY GROQ_API_KEY
//  Nome modello di ripiego per il server in casa, quando il pannello non lo
//  specifica e il server non sa dire cosa ha caricato: la gran parte dei server
//  Whisper locali ignora comunque questo campo.
#define STT_LOCAL_FALLBACK "whisper-1"

// Un tentativo di trascrizione. apiKey vuota = nessun header di autenticazione
// (server in casa). Torna il testo, oppure "" se il tentativo e' fallito.
static String sttPost(const String &url, const String &model, const String &apiKey,
                      bool tls, const uint8_t *wav, size_t wavLen, const char *lang) {
  const String boundary = "----alexoBoundary7MA4YWxkTrZu0gW";

  // Parti del corpo multipart prima e dopo i byte del file
  String pre;
  pre  = "--" + boundary + "\r\n";
  pre += "Content-Disposition: form-data; name=\"model\"\r\n\r\n" + model + "\r\n";
  pre += "--" + boundary + "\r\n";
  pre += "Content-Disposition: form-data; name=\"language\"\r\n\r\n" + String(lang) + "\r\n";
  pre += "--" + boundary + "\r\n";
  pre += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  pre += "Content-Type: audio/wav\r\n\r\n";
  const String post = "\r\n--" + boundary + "--\r\n";

  const size_t bodyLen = pre.length() + wavLen + post.length();
  uint8_t *body = (uint8_t *)ps_malloc(bodyLen);
  if (!body) {
    Serial.println("[stt] allocazione PSRAM del body fallita");
    return "";
  }
  size_t o = 0;
  memcpy(body + o, pre.c_str(), pre.length());  o += pre.length();
  memcpy(body + o, wav, wavLen);                o += wavLen;
  memcpy(body + o, post.c_str(), post.length());

  // Il client cambia con la strada: TLS verso il cloud, in chiaro verso casa.
  WiFiClientSecure secure;
  WiFiClient       plain;
  secure.setInsecure();              // niente verifica certificato (ok per hobby)
  secure.setTimeout(20000);
  plain.setTimeout(20000);

  HTTPClient http;
  http.setTimeout(25000);
  if (tls) http.begin(secure, url);
  else     http.begin(plain,  url);
  if (apiKey.length()) http.addHeader("Authorization", String("Bearer ") + apiKey);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  Serial.printf("[stt] invio %u byte a %s (%s)...\n", (unsigned)bodyLen,
                tls ? "Whisper cloud" : "Whisper in casa", model.c_str());
  uint32_t t0 = millis();
  int code = http.POST(body, bodyLen);
  String resp = http.getString();
  http.end();
  free(body);
  Serial.printf("[stt] risposta HTTP %d in %lu ms\n", code, (unsigned long)(millis() - t0));

  if (code != 200) {
    Serial.printf("[stt] errore: %s\n", resp.c_str());
    return "";
  }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, resp);
  if (e) {
    Serial.printf("[stt] JSON non valido: %s\n", e.c_str());
    return "";
  }
  String text = doc["text"] | "";
  text.trim();
  return text;
}

String sttTranscribe(const uint8_t *wav, size_t wavLen, const char *lang) {
  if (!wav || wavLen == 0) return "";

  bool provataInCasa = false;
  if (localReachable(LOC_STT)) {
    provataInCasa = true;
    String model = localModelName(LOC_STT);
    if (model.isEmpty()) model = STT_LOCAL_FALLBACK;
    String text = sttPost(localBaseUrl(LOC_STT) + "/audio/transcriptions",
                          model, "", false, wav, wavLen, lang);
    if (text.length()) return text;
    Serial.println("[stt] trascrizione in casa non riuscita");
  }

  // "Solo casa": la voce registrata non esce, punto. Tornando vuoto chi chiama
  // dira' "Non ho capito" - meglio di un WAV spedito a Groq senza avvisare.
  if (gSettings.localOnly) {
    if (!provataInCasa) localSayBlocked(LOC_STT);
    return "";
  }

  localSayCloud(LOC_STT);
  return sttPost(STT_URL, STT_MODEL, STT_API_KEY, true, wav, wavLen, lang);
}
