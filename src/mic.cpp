// ============================================================================
//  ALEXO - Aufnahme vom Mikrofon. ZWEI Wege, in config.h auswählbar:
//    MIC_USE_I2S = 0  -> MAX4466 analog an ADC1 (getaktetes Abtasten)
//    MIC_USE_I2S = 1  -> digitales I2S-Mikrofon ICS-43434/INMP441 (DMA in Hardware)
//  Die öffentliche Schnittstelle (mic.h) ist in beiden Fällen dieselbe, die
//  übrige Firmware ändert sich also nicht. Zum Wechseln genügt es, MIC_USE_I2S
//  zu ändern und neu zu übersetzen.
// ============================================================================
#include "mic.h"
#include "config.h"
#include "settings.h"
#include "netlog.h"
#include <math.h>

// Gibt eine Zeile sowohl seriell als auch über das Netz (Telnet) aus, damit sich
// die Messwerte auch lesen lassen, wenn das Gerät im Gehäuse steckt und die
// USB-Buchse nicht erreichbar ist.
static void micLogln(const char *s) {
  Serial.println(s);
  netlogPrintln(s);
}

#if MIC_USE_I2S
  #include "driver/i2s.h"
#else
  #include "driver/adc.h"
  #include "esp_adc_cal.h"
#endif

// --- Gemeinsame Werte -------------------------------------------------------
#define MIC_MAX_SECONDS 20   // Puffer im PSRAM: Höchstdauer der Aufnahme (siehe REC_MAX_MS)
static const size_t MIC_MAX_SAMPLES = (size_t)MIC_SAMPLE_RATE * MIC_MAX_SECONDS;

// --- Gemeinsame Puffer (PSRAM) ----------------------------------------------
static int16_t *g_pcm   = nullptr;   // PCM, 16 Bit Mono
static uint8_t *g_wav   = nullptr;   // WAV-Kopf und eine Kopie des PCM
static size_t   g_count = 0;         // Abtastwerte der letzten Aufnahme
static int      g_peak  = 0;         // absoluter Spitzenwert der letzten Aufnahme
static bool     g_heard = false;     // echte Sprache erkannt (über der Schwelle) in der letzten Aufnahme
static volatile bool g_heardNow = false;  // dasselbe, aber WÄHREND der Aufnahme lesbar
static uint32_t g_noVoiceMs = 0;     // Wartezeit "niemand spricht" für die nächste Aufnahme (0 = normal)

void micSetNoVoiceMs(uint32_t ms) { g_noVoiceMs = ms; }
bool micVoiceStarted()            { return g_heardNow; }

// ============================================================================
//                    WEG ÜBER I2S (ICS-43434 / INMP441)
// ============================================================================
#if MIC_USE_I2S

// Bei Ruhe auf Geräusche reagieren, mit SELBSTTÄTIGER Nachführung und einer
// HÜLLKURVE gegen einzelne Impulse und Aufblitzen.
// Das Grundrauschen des I2S-Mikrofons ist STOSSWEISE: bei Ruhe liegt der Wert
// niedrig, hat aber vereinzelt sehr hohe Spitzen, auch nach dem Hochpass.
// Das Vorgehen ist zweiteilig: erstens führt ein Grundpegel die Basis von selbst
// nach; zweitens folgt die Helligkeit statt einem harten Schwellwert (der
// entweder die Empfindlichkeit zunichte machte oder ein Dauerflackern durchließ)
// einer HÜLLKURVE mit LANGSAMEM ANSTIEG. Ein einzelner Impuls über einen Block
// (etwa 20 ms) schafft es nicht hinauf, es blitzt also nichts; ein echtes
// Geräusch, das mehrere Blöcke dauert, leuchtet voll aus.
//  MIC_LVL_MARGIN und MIC_LVL_FLOOR sind jetzt zur LAUFZEIT änderbar
//  (gSettings.micLvlMargin und micLvlFloor über das Web-Panel); die
//  Werkseinstellungen stehen in config.h.
#define MIC_LVL_GAIN    1.0f   // Verstärkung bei Ruhe (Helligkeit im Verhältnis zum Ton)
//  Anstieg und Abklingen sind jetzt zur Laufzeit änderbar
//  (gSettings.micLvlAttack und micLvlRelease über das Web-Panel); die
//  Werkseinstellungen stehen in config.h.
#define MIC_DEBUG       0

static const i2s_port_t I2S_PORT = I2S_NUM_0;

// Laufende Werte für das Web-Panel (gesetzt von micLevelFromChunk).
static volatile uint8_t g_liveLevel  = 0;
static volatile float   g_liveFloor  = 0;
static volatile float   g_liveThresh = 0;

bool micBegin() {
  if (!psramFound()) { Serial.println("[mic] kein PSRAM vorhanden!"); return false; }
  if (!g_pcm) g_pcm = (int16_t *)ps_malloc(MIC_MAX_SAMPLES * sizeof(int16_t));
  if (!g_wav) g_wav = (uint8_t *)ps_malloc(44 + MIC_MAX_SAMPLES * sizeof(int16_t));
  if (!g_pcm || !g_wav) { Serial.println("[mic] Reservieren im PSRAM fehlgeschlagen!"); return false; }

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = MIC_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;   // INMP441: 24 Bit in 32
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;    // L/R auf Masse -> linker Kanal
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = I2S_SCK_PIN;
  pins.ws_io_num    = I2S_WS_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = I2S_SD_PIN;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
    Serial.println("[mic] i2s_driver_install fehlgeschlagen!");
    return false;
  }
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
  Serial.printf("[mic] ICS-43434 über I2S bereit (BCLK=%d WS=%d SD=%d), %d Hz\n",
                I2S_SCK_PIN, I2S_WS_PIN, I2S_SD_PIN, MIC_SAMPLE_RATE);
  return true;
}

size_t micRecord(uint32_t maxMs, bool (*keepGoing)(), void (*onLevel)(uint8_t), uint32_t silenceMs) {
  if (!g_pcm) return 0;
  const size_t maxSamples =
      min(MIC_MAX_SAMPLES, (size_t)((uint64_t)MIC_SAMPLE_RATE * maxMs / 1000));

  static int32_t buf[256];
  size_t   n = 0;
  int      peak = 0, chunkPeak = 0;
  double   chunkSumSq = 0;    // Summe der Quadrate im Block -> mittlere Energie (RMS)
  long     chunkCount = 0;    // Abtastwerte im Block (für den Mittelwert)
  float    silLevel   = 0;    // geglätteter Pegel für die Stille (Hüllkurve gegen Böen)
  float    noiseFloor = -1;   // selbsttätig nachgeführter Grundpegel (Lüfter, Wind)
  uint32_t levelTimer = millis();
  // Selbsttätiger Abbruch bei Stille: nachdem Sprache zu hören war (heard),
  // endet die Aufnahme, sobald silenceMs lang ununterbrochen Stille herrscht.
  // Ist gar nichts zu hören, endet sie nach einer angemessenen Wartezeit
  // (Schonfrist plus Stille plus eine Sekunde).
  const uint32_t recStart = millis();
  uint32_t lastSound = recStart;
  bool     heard = false;
  g_heardNow = false;
  // Wartezeit für "niemand spricht": die angemessene, sofern der vorherige
  // Aufruf sie nicht mit micSetNoVoiceMs verkürzt hat (fortlaufender Chat).
  // Sie wird hier verbraucht und gilt nur für diese eine Aufnahme.
  const uint32_t noVoiceMs = g_noVoiceMs ? g_noVoiceMs
                                         : (uint32_t)(REC_MIN_MS + silenceMs + 1000);
  g_noVoiceMs = 0;

  // Hochpass erster Ordnung (etwa 120 Hz) im 24-Bit-Bereich: entfernt den
  // Gleichanteil und das tieffrequente Driften, die in der Messung (Schritt 0)
  // den größten Teil des Grundrauschens des ICS-43434 ausmachten. Sprache
  // oberhalb von 120 Hz läuft unversehrt durch. Der Zustand wird bei jeder
  // Aufnahme zurückgesetzt, der kurze Einschwingvorgang am Anfang fällt nicht ins
  // Gewicht.
  //   y[n] = x[n] - x[n-1] + R*y[n-1]
  double hpX = 0, hpY = 0;
  const double hpR = 0.95;
  const int    shift24 = I2S_SHIFT - 8;          // Verschiebung vom 24-Bit- in den 16-Bit-Bereich

  while (n < maxSamples) {
    if (keepGoing && !keepGoing()) break;
    size_t br = 0;
    i2s_read(I2S_PORT, buf, sizeof(buf), &br, portMAX_DELAY);
    int got = br / 4;
    for (int i = 0; i < got && n < maxSamples; i++) {
      double v = (double)(buf[i] >> 8);           // Abtastwert mit 24 Bit
      double y = v - hpX + hpR * hpY;             // Hochpass (nimmt Gleichanteil und Brummen)
      hpX = v; hpY = y;
      int32_t s = (int32_t)y >> shift24;          // 24 -> 16 Bit mit der gewählten Verstärkung
      if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
      g_pcm[n++] = (int16_t)s;
      int a = s < 0 ? -s : s;
      if (a > peak) peak = a;
      if (a > chunkPeak) chunkPeak = a;
      chunkSumSq += (double)s * s;   // Energie -> RMS für den Abbruch bei Stille
      chunkCount++;
    }
    if ((millis() - levelTimer) >= 20) {
      int l = (int)((long)chunkPeak * 255 / 4000);   // SPITZENwert -> LED (0..255)
      if (l > 255) l = 255;
      if (onLevel) onLevel((uint8_t)l);
      // --- Abbruch bei Stille mit SELBSTTÄTIG NACHGEFÜHRTER SCHWELLE --------
      //  Statt eines festen Pegels misst sich "Sprache" am Grundrauschen.
      //  Der Grundpegel (Lüfter, Wind) wird laufend von noiseFloor geschätzt.
      //  Dieser fällt schnell (er hängt sich an Stille und gleichmäßiges
      //  Rauschen) und steigt nur sehr langsam, damit Sprache ihn nicht
      //  mitzieht: er bleibt das Rauschen und wird nicht zur Sprache.
      //  Benutzt wird die MITTLERE ENERGIE (RMS) des Blocks, nicht der
      //  Spitzenwert: Wind am Mikrofon erzeugt sehr hohe, aber einzelne Spitzen.
      //  Der Spitzenwert sieht sie, der Mittelwert nicht, deshalb trennt der
      //  Mittelwert "Wind" von "Sprache" viel besser. Die Schwelle ist:
      //      Schwelle = noiseFloor * REC_SILENCE_MARGIN + REC_SILENCE_FLOOR
      //  So passt sie sich von selbst an, wenn sich das Rauschen ändert, ohne
      //  dass jemand von Hand nachstellt.
      if (silenceMs) {
        float rms = (chunkCount > 0) ? sqrtf((float)(chunkSumSq / chunkCount)) : 0.0f;
        if (noiseFloor < 0) noiseFloor = rms;                 // Startwert aus dem ersten Block
        float k = (rms < noiseFloor) ? 0.30f : 0.02f;         // schnell hinunter, langsam hinauf
        noiseFloor += (rms - noiseFloor) * k;
        silLevel += (rms - silLevel) * 0.40f;                 // Hüllkurve gegen Böen
        float soglia = noiseFloor * REC_SILENCE_MARGIN + REC_SILENCE_FLOOR;
        uint32_t now = millis();
        if (silLevel >= soglia) { lastSound = now; heard = true; g_heardNow = true; }
        bool stopSilenzio  = heard && (now - lastSound) >= silenceMs;
        bool stopNienteVoce = !heard && (now - recStart) >= noVoiceMs;
        if (stopSilenzio || stopNienteVoce) { chunkPeak = 0; break; }
      }
      levelTimer = millis();
      chunkPeak = 0;
      chunkSumSq = 0; chunkCount = 0;
    }
  }
  g_count = n;
  g_peak  = peak;
  // Ist der Abbruch bei Stille eingeschaltet, sagt 'heard', ob echte Sprache zu
  // hören war (über der nachgeführten Schwelle). Ist er aus (silenceMs==0),
  // lässt sich das nicht wissen, und wir nehmen "gehört" an, um brauchbare
  // Aufnahmen nicht zu verwerfen.
  g_heard = silenceMs ? heard : true;
  return n;
}

// Untersuchung des Grundrauschens (Schritt 0 des Weckworts). EINMALIG: misst
// etwa 600 ms, gibt EINE Zeile aus und kehrt zurück. Wiederholt aus dem Loop im
// Setup aufzurufen, im Wechsel mit ArduinoOTA.handle(), damit die Aktualisierung
// über Funk erreichbar bleibt und sich der Messbetrieb durch erneutes Flashen
// wieder abschalten lässt. KEINE Endlosschleife, die den Funkweg zumauern würde.
// Gemessen werden die VOLLEN 24 Bit der Abtastwerte (buf>>8, denn die Daten des
// ICS-43434 stehen linksbündig in 32 Bit), und es wird hochgerechnet, was jede
// Einstellung von I2S_SHIFT ergäbe (out16 = v24 >> (SHIFT-8)). Bei Ruhe soll AC16
// niedrig sein (etwa 10 bis 30), beim Sprechen soll PEAK16 gesund aussehen,
// einige Tausend, ohne bei 32767 anzustoßen.
void micDiag() {
  static bool intro = false;
  if (!intro) {
    micLogln("[micDiag] === Messung am I2S-Mikrofon (Schritt 0 des Weckworts) ===");
    micLogln("[micDiag] AC=Rauschen roh  AC_HP=nach Hochpass ~120Hz  PEAK=Spitze  DC=Versatz (24 Bit)");
    micLogln("[micDiag] AC_HP viel kleiner als AC -> tieffrequentes Rauschen, in Software behebbar. Bleibt es hoch -> Einstreuung oder Masse, also Hardware.");
    micLogln("[micDiag] PEAK16/AC16 bei Verschiebung 15. 1) STILLE 2) etwas sagen. Funk und Telnet bleiben aktiv.");
    intro = true;
  }
  // Hochpass erster Ordnung gegen Gleichanteil und Brummen, DURCHGEHEND über die
  // Messfenster hinweg:
  //   y[n] = x[n] - x[n-1] + R*y[n-1]   mit R=0.95 -> Grenze etwa 120 Hz bei 16 kHz.
  // Sprache (Formanten zwischen 300 und 3000 Hz) läuft durch, das Driften
  // unterhalb von etwa 120 Hz wird entfernt.
  static double hpPrevX = 0, hpPrevY = 0;
  const double R = 0.95;

  static int32_t buf[256];
  double sum = 0, sumSq = 0, sumHp2 = 0;
  int32_t peak = 0, peakHp = 0;
  long n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 600) {
    size_t br = 0;
    i2s_read(I2S_PORT, buf, sizeof(buf), &br, portMAX_DELAY);
    int got = br / 4;
    for (int i = 0; i < got; i++) {
      double v = (double)(buf[i] >> 8);        // Abtastwert mit 24 Bit und Vorzeichen
      sum += v; sumSq += v * v;
      double a = v < 0 ? -v : v;
      if (a > peak) peak = (int32_t)a;
      double y = v - hpPrevX + R * hpPrevY;    // Ausgang des Hochpasses
      hpPrevX = v; hpPrevY = y;
      sumHp2 += y * y;
      double ay = y < 0 ? -y : y;
      if (ay > peakHp) peakHp = (int32_t)ay;
      n++;
    }
  }
  if (n < 1) return;
  double mean = sum / n;
  double var  = sumSq / n - mean * mean; if (var < 0) var = 0;
  double sd   = sqrt(var);                    // rohes Rauschen (im 24-Bit-Bereich)
  double sdHp = sqrt(sumHp2 / n);             // Rauschen nach dem Hochpass (Mittelwert etwa 0)
  const double f15 = (double)(1L << (15 - 8));// Hochrechnung auf Verschiebung 15
  long pk16   = (long)(peak   / f15); if (pk16   > 32767) pk16   = 32767;
  long pkHp16 = (long)(peakHp / f15); if (pkHp16 > 32767) pkHp16 = 32767;
  char line[256];
  snprintf(line, sizeof(line),
           "[micDiag] AC=%.0f AC_HP=%.0f PEAK=%ld DC=%.0f | @sh15: AC16=%.1f AC_HP16=%.1f PEAK16=%ld PEAK_HP16=%ld",
           sd, sdHp, (long)peak, mean, sd / f15, sdHp / f15, pk16, pkHp16);
  micLogln(line);
}

size_t micReadChunk(int16_t *out, size_t maxn) {
  static int32_t buf[256];
  size_t total = 0;
  while (total < maxn) {
    size_t br = 0;
    i2s_read(I2S_PORT, buf, sizeof(buf), &br, portMAX_DELAY);
    int got = br / 4;
    if (got <= 0) break;
    for (int i = 0; i < got && total < maxn; i++) {
      int32_t s = buf[i] >> I2S_SHIFT;                 // roh, ohne Hochpass
      if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
      out[total++] = (int16_t)s;
    }
  }
  return total;
}

// Berechnet den Pegel (0..255) für den Ring aus einem BEREITS vorliegenden
// PCM-Block mit 16 Bit. Verwendet werden ein Hochpass bei etwa 120 Hz (er nimmt
// Gleichanteil und Brummen, damit nichts flackert), ein selbsttätig
// nachgeführter Grundpegel und eine Hüllkurve. Benutzt wird die Funktion sowohl
// von micPeekLevel als auch vom Loop des Weckworts, der ihr DENSELBEN Block gibt
// wie dem Modell, damit der I2S-Bus nicht zweimal gelesen wird. Der Zustand ist
// statisch, es darf also immer nur ein Aufrufer aktiv sein.
void micFlush() {
  // Verwirft den gesamten Ton, der sich im DMA-Puffer des I2S angesammelt hat.
  // Nach einem Wortwechsel aufzurufen: Aufnahme und Antwort dauern einige
  // Sekunden, in denen sich der Puffer füllt. Ohne das würde das Weckwort diesen
  // alten Ton in einem Schwung verschlucken und gleich nach dem Verstummen der
  // Stimme fälschlich auslösen.
  static int32_t tmp[256];
  size_t br;
  do { br = 0; i2s_read(I2S_PORT, tmp, sizeof(tmp), &br, 0); } while (br > 0);
}

uint8_t micLevelFromChunk(const int16_t *s, size_t n) {
  if (n == 0) return 0;
  static double hpX = 0, hpY = 0;
  const double hpR = 0.95;
  float acc = 0;
  for (size_t i = 0; i < n; i++) {
    double v = (double)s[i];
    double y = v - hpX + hpR * hpY;                // Hochpass (nimmt Gleichanteil und Brummen)
    hpX = v; hpY = y;
    acc += fabsf((float)y);
  }
  float mad = acc / n;

  // Selbsttätig nachgeführter Grundpegel: er fällt schnell und hängt sich damit
  // an die Stille, steigt aber nur sehr langsam, damit Sprache ihn nicht
  // mitzieht. So bleibt die Grundlinie beim Rauschen.
  static float noiseFloor = -1.0f;
  if (noiseFloor < 0.0f) noiseFloor = mad;
  float k = (mad < noiseFloor) ? 0.05f : 0.0005f;
  noiseFloor += (mad - noiseFloor) * k;

  float thresh = noiseFloor * gSettings.micLvlMargin + gSettings.micLvlFloor;
  float target = (mad - thresh) * MIC_LVL_GAIN;
  if (target < 0) target = 0; else if (target > 255) target = 255;

  // Hüllkurve: langsamer Anstieg, damit ein einzelner Impuls es nicht hinauf
  // schafft, und schnelleres Abklingen. So flackert nichts, und auf echte
  // Geräusche reagiert es weiterhin.
  static float env = 0;
  float ek = (target > env) ? gSettings.micLvlAttack : gSettings.micLvlRelease;
  env += (target - env) * ek;
  int lvl = (int)(env + 0.5f);
  if (lvl < 0) lvl = 0; else if (lvl > 255) lvl = 255;
  // Stellt die aktuellen Werte für das Web-Panel bereit.
  g_liveLevel  = (uint8_t)lvl;
  g_liveFloor  = noiseFloor;
  g_liveThresh = thresh;
  return (uint8_t)lvl;
}

// Abbild der laufenden Werte (von micLevelFromChunk bei jedem Block gesetzt,
// solange das Gerät ruht).
void micGetLive(uint8_t *level, float *noiseFloor, float *thresh) {
  if (level)      *level      = g_liveLevel;
  if (noiseFloor) *noiseFloor = g_liveFloor;
  if (thresh)     *thresh     = g_liveThresh;
}

uint8_t micPeekLevel() {
  static int32_t buf[256];
  size_t br = 0;
  i2s_read(I2S_PORT, buf, sizeof(buf), &br, portMAX_DELAY);
  int n = br / 4;
  if (n <= 0) return 0;
  static int16_t pcm[256];
  for (int i = 0; i < n; i++) {
    int32_t v = buf[i] >> I2S_SHIFT;                // roh (der Hochpass steckt weiter innen)
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    pcm[i] = (int16_t)v;
  }
  return micLevelFromChunk(pcm, n);
}

// ============================================================================
//                       ANALOGER WEG (MAX4466)
// ============================================================================
#else  // !MIC_USE_I2S

#define MIC_REC_GAIN  16      // digitale Verstärkung bei der Aufnahme (roh -> int16)
#define MIC_LVL_NOISE 20.0f   // Schwelle für das Reagieren bei Ruhe (MAD)
#define MIC_LVL_GAIN  14.0f   // Verstärkung für das Reagieren bei Ruhe
#define MIC_DEBUG     0

// Beim ESP32-S3 gilt für ADC1: GPIO1->CH0, GPIO2->CH1, ... also Kanal = Anschluss - 1.
static const adc1_channel_t MIC_CH = (adc1_channel_t)(MIC_ADC_PIN - 1);

bool micBegin() {
  if (!psramFound()) { Serial.println("[mic] kein PSRAM vorhanden!"); return false; }
  if (!g_pcm) g_pcm = (int16_t *)ps_malloc(MIC_MAX_SAMPLES * sizeof(int16_t));
  if (!g_wav) g_wav = (uint8_t *)ps_malloc(44 + MIC_MAX_SAMPLES * sizeof(int16_t));
  if (!g_pcm || !g_wav) { Serial.println("[mic] Reservieren im PSRAM fehlgeschlagen!"); return false; }

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(MIC_CH, ADC_ATTEN_DB_12);  // früher DB_11 (veraltet, gleiches Verhalten)
  (void)adc1_get_raw(MIC_CH);   // erster Leerlauf zum Einschwingen
  Serial.printf("[mic] MAX4466 bereit: GPIO%d (ADC1_CH%d), %d Hz, höchstens %d s\n",
                MIC_ADC_PIN, (int)MIC_CH, MIC_SAMPLE_RATE, MIC_MAX_SECONDS);
  return true;
}

size_t micRecord(uint32_t maxMs, bool (*keepGoing)(), void (*onLevel)(uint8_t), uint32_t silenceMs) {
  (void)silenceMs;   // Abbruch bei Stille ist auf dem analogen Weg nicht umgesetzt
  if (!g_pcm) return 0;
  const uint32_t period_us = 1000000UL / MIC_SAMPLE_RATE;
  const size_t   maxSamples =
      min(MIC_MAX_SAMPLES, (size_t)((uint64_t)MIC_SAMPLE_RATE * maxMs / 1000));

  size_t   n = 0;
  uint64_t sum = 0;                  // zum Schätzen des Gleichanteils
  uint32_t levelTimer = millis();
  uint16_t chunkMin = 4095, chunkMax = 0;

  uint32_t tNext = micros();
  while (n < maxSamples) {
    if (keepGoing && !keepGoing()) break;
    while ((int32_t)(micros() - tNext) < 0) { }
    tNext += period_us;

    uint16_t raw = adc1_get_raw(MIC_CH);
    g_pcm[n++] = (int16_t)raw;       // roh, wird später umgerechnet
    sum += raw;
    if (raw < chunkMin) chunkMin = raw;
    if (raw > chunkMax) chunkMax = raw;

    if (onLevel && (millis() - levelTimer) >= 20) {
      uint16_t amp = (chunkMax - chunkMin);
      uint8_t lvl = (uint8_t)min<uint32_t>(255, (uint32_t)amp * 255 / 4095);
      onLevel(lvl);
      levelTimer = millis();
      chunkMin = 4095; chunkMax = 0;
    }
  }

  g_count = n;
  if (n == 0) { g_peak = 0; g_heard = false; return 0; }
  g_heard = true;   // analoger Weg: ohne Spracherkennung nehmen wir "gehört" an

  // Gleichanteil entfernen, auf int16 umrechnen und dabei die Spitze ermitteln
  int32_t dc = (int32_t)(sum / n);
  int peak = 0;
  for (size_t i = 0; i < n; i++) {
    int32_t s = ((int32_t)g_pcm[i] - dc) * MIC_REC_GAIN;
    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
    g_pcm[i] = (int16_t)s;
    int a = s < 0 ? -s : s;
    if (a > peak) peak = a;
  }
  g_peak = peak;
  return n;
}

void micDiag() {
  Serial.println("[micDiag] Die Messung gibt es nur mit dem I2S-Mikrofon (MIC_USE_I2S=1).");
}

size_t micReadChunk(int16_t *out, size_t maxn) {
  // Analoger Weg: vom Weckwort nicht benutzt, das will das I2S-Mikrofon.
  (void)out; (void)maxn; return 0;
}

uint8_t micLevelFromChunk(const int16_t *s, size_t n) {
  (void)s; (void)n; return 0;   // auf dem analogen Weg nicht benutzt
}

void micGetLive(uint8_t *level, float *noiseFloor, float *thresh) {
  if (level) *level = 0; if (noiseFloor) *noiseFloor = 0; if (thresh) *thresh = 0;
}

void micFlush() { /* auf dem analogen Weg gibt es keinen DMA-Puffer */ }

uint8_t micPeekLevel() {
  const int N = 256;
  const uint32_t period = 1000000UL / MIC_SAMPLE_RATE;
  uint32_t sum = 0;
  static uint16_t raw[N];
  uint32_t tNext = micros();
  for (int i = 0; i < N; i++) {
    while ((int32_t)(micros() - tNext) < 0) { }
    tNext += period;
    uint16_t r = adc1_get_raw(MIC_CH);
    raw[i] = r;
    sum += r;
  }
  float mean = (float)sum / N;
  float acc = 0;
  for (int i = 0; i < N; i++) acc += fabsf((float)raw[i] - mean);   // MAD: unempfindlich gegen Spitzen
  float mad = acc / N;

  int lvl = (int)((mad - MIC_LVL_NOISE) * MIC_LVL_GAIN);
  if (lvl < 0) lvl = 0; if (lvl > 255) lvl = 255;
#if MIC_DEBUG
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg > 500) { Serial.printf("[mic] mad=%.1f lvl=%d\n", mad, lvl); lastDbg = millis(); }
#endif
  return (uint8_t)lvl;
}

#endif  // MIC_USE_I2S

// ============================================================================
//                         GEMEINSAMER TEIL (WAV und Zubehör)
// ============================================================================
const int16_t *micPcm()         { return g_pcm; }
size_t         micSampleCount() { return g_count; }
uint32_t       micSampleRate()  { return MIC_SAMPLE_RATE; }
int            micLastPeak()    { return g_peak; }
bool           micHeardVoice()  { return g_heard; }

static void wr32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void wr16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }

const uint8_t *micWav(size_t *len) {
  if (!g_wav || g_count == 0) { if (len) *len = 0; return nullptr; }
  const uint32_t sr      = MIC_SAMPLE_RATE;
  const uint32_t dataLen = (uint32_t)g_count * sizeof(int16_t);
  uint8_t *h = g_wav;
  memcpy(h + 0,  "RIFF", 4);  wr32(h + 4, 36 + dataLen);
  memcpy(h + 8,  "WAVE", 4);  memcpy(h + 12, "fmt ", 4);
  wr32(h + 16, 16); wr16(h + 20, 1); wr16(h + 22, 1);
  wr32(h + 24, sr); wr32(h + 28, sr * 2); wr16(h + 32, 2); wr16(h + 34, 16);
  memcpy(h + 36, "data", 4); wr32(h + 40, dataLen);
  memcpy(h + 44, g_pcm, dataLen);
  if (len) *len = 44 + dataLen;
  return g_wav;
}
