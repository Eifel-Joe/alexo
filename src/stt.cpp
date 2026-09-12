// ============================================================================
//  ALEXO - Spracherkennung
//  Baut im PSRAM einen Rumpf im Format multipart/form-data mit der WAV-Datei,
//  schickt ihn an einen Endpunkt im OpenAI-Format /audio/transcriptions und
//  liest daraus das Feld "text".
//  Zwei Wege, genau dasselbe Format:
//    CLOUD  Groq Whisper (kostenlos)
//    ZU HAUSE  ein Whisper-Server im eigenen Netz (siehe localai.h), ohne
//           Schlüssel
//  Zu Hause wird nur erkannt, wenn im Panel eine Adresse steht UND der PC
//  antwortet; macht der lokale Server einen Fehler, geht es mit einem Hinweis in
//  Rot im Chat noch einmal in die Cloud. Bei eingeschaltetem "nur zu Hause"
//  entfällt dieser zweite Versuch: die aufgenommene Stimme verlässt das Netz
//  nicht.
// ============================================================================
#include "stt.h"
#include "secrets.h"
#include "settings.h"
#include "localai.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- Anbieter für die Erkennung in der Cloud (Groq, kostenlos, Schnittstelle
// im OpenAI-Format) ----------------------------------------------------------
// Um auf OpenAI zu wechseln: URL "https://api.openai.com/v1/audio/transcriptions",
// MODEL "whisper-1", KEY OPENAI_API_KEY.
#define STT_URL     "https://api.groq.com/openai/v1/audio/transcriptions"
#define STT_MODEL   "whisper-large-v3-turbo"
#define STT_API_KEY GROQ_API_KEY
//  Ersatzname für das Modell des Servers zu Hause, wenn das Panel keinen nennt
//  und der Server nicht sagen kann, was er geladen hat. Die meisten
//  Whisper-Server zu Hause übergehen dieses Feld ohnehin.
#define STT_LOCAL_FALLBACK "whisper-1"

// Ein Erkennungsversuch. Leerer apiKey = keine Kopfzeile zur Anmeldung (Server
// zu Hause). Liefert den Text oder "", wenn der Versuch scheiterte.
static String sttPost(const String &url, const String &model, const String &apiKey,
                      bool tls, const uint8_t *wav, size_t wavLen, const char *lang) {
  const String boundary = "----alexoBoundary7MA4YWxkTrZu0gW";

  // Die Teile des Rumpfes vor und nach den Bytes der Datei
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
    Serial.println("[stt] Reservieren des Rumpfes im PSRAM fehlgeschlagen");
    return "";
  }
  size_t o = 0;
  memcpy(body + o, pre.c_str(), pre.length());  o += pre.length();
  memcpy(body + o, wav, wavLen);                o += wavLen;
  memcpy(body + o, post.c_str(), post.length());

  // Der Client richtet sich nach dem Weg: TLS in die Cloud, unverschlüsselt
  // nach Hause.
  WiFiClientSecure secure;
  WiFiClient       plain;
  secure.setInsecure();              // keine Zertifikatsprüfung (für ein Hobbyprojekt vertretbar)
  secure.setTimeout(20000);
  plain.setTimeout(20000);

  HTTPClient http;
  http.setTimeout(25000);
  if (tls) http.begin(secure, url);
  else     http.begin(plain,  url);
  if (apiKey.length()) http.addHeader("Authorization", String("Bearer ") + apiKey);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  Serial.printf("[stt] sende %u Byte an %s (%s)...\n", (unsigned)bodyLen,
                tls ? "Whisper in der Cloud" : "Whisper zu Hause", model.c_str());
  uint32_t t0 = millis();
  int code = http.POST(body, bodyLen);
  String resp = http.getString();
  http.end();
  free(body);
  Serial.printf("[stt] Antwort HTTP %d nach %lu ms\n", code, (unsigned long)(millis() - t0));

  if (code != 200) {
    Serial.printf("[stt] Fehler: %s\n", resp.c_str());
    return "";
  }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, resp);
  if (e) {
    Serial.printf("[stt] JSON ungültig: %s\n", e.c_str());
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
    Serial.println("[stt] Erkennung zu Hause fehlgeschlagen");
  }

  // "Nur zu Hause": die aufgenommene Stimme geht nicht hinaus, Ende. Da hier
  // nichts zurückkommt, sagt der Aufrufer "Das habe ich nicht verstanden", was
  // besser ist, als eine WAV-Datei unangekündigt an Groq zu schicken.
  if (gSettings.localOnly) {
    if (!provataInCasa) localSayBlocked(LOC_STT);
    return "";
  }

  localSayCloud(LOC_STT);
  return sttPost(STT_URL, STT_MODEL, STT_API_KEY, true, wav, wavLen, lang);
}
