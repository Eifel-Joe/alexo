// ============================================================================
//  ALEXO - Weckwort "Hey Jarvis", erkannt im Geraet selbst (microWakeWord).
//  Siehe wakeword.h und WAKEWORD.md.
//
//  Die Kette: PCM mit 16 kHz -> Merkmalsberechnung (40 Mel-Merkmale je 10 ms)
//  -> Umwandlung nach int8 -> INT8-Modell im Strombetrieb (Eingang [1,stride,40],
//  es sammelt 'stride' Frames und ruft dann Invoke) -> Wahrscheinlichkeit als
//  uint8 -> gleitender Mittelwert ueber WAKE_WINDOW groesser als
//  WAKE_PROB_CUTOFF -> erkannt. Ablauf und Konstanten sind micro_wake_word aus
//  ESPHome nachgebildet.
//
//  Wird nur uebersetzt, wenn WAKE_ENABLE oder WAKE_TEST gesetzt ist, sonst
//  bleiben nur wirkungslose Rumpffunktionen.
// ============================================================================
#include "wakeword.h"
#include "config.h"

#if WAKE_ENABLE || WAKE_TEST

#include "netlog.h"
#include "mic.h"
#include "settings.h"
#include <math.h>
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "wake_model.h"   // alignas(16) const unsigned char g_wake_model[]

#define WAKE_FEATURE_SIZE 40
#define WAKE_ARENA_BYTES  (40 * 1024)   // Manifest nennt 22860; Reserve im PSRAM

namespace {
  struct FrontendState   fe;
  tflite::MicroInterpreter *interp = nullptr;
  uint8_t *arena = nullptr;
  int      model_stride  = 1;     // input->dims[1]: Frames je Invoke
  int      current_step  = 0;
  uint8_t  recent[16];            // gleitendes Fenster der Wahrscheinlichkeiten
  int      recent_n = 0, recent_idx = 0;
  uint8_t  last_prob = 0;
  bool     s_ready = false;
}

static void wlogln(const char *s) { Serial.println(s); netlogPrintln(s); }

bool wakeBegin() {
  if (!psramFound()) { wlogln("[wake] kein PSRAM vorhanden"); return false; }

  // --- Merkmalsberechnung (Werte aus preprocessor_settings.h von ESPHome) ---
  struct FrontendConfig cfg;
  FrontendFillConfigWithDefaults(&cfg);
  cfg.window.size_ms = 30;  cfg.window.step_size_ms = 10;
  cfg.filterbank.num_channels = WAKE_FEATURE_SIZE;
  cfg.filterbank.lower_band_limit = 125.0f;
  cfg.filterbank.upper_band_limit = 7500.0f;
  cfg.noise_reduction.smoothing_bits = 10;
  cfg.noise_reduction.even_smoothing = 0.025f;
  cfg.noise_reduction.odd_smoothing = 0.06f;
  cfg.noise_reduction.min_signal_remaining = 0.05f;
  cfg.pcan_gain_control.enable_pcan = 1;
  cfg.pcan_gain_control.strength = 0.95f;
  cfg.pcan_gain_control.offset = 80.0f;
  cfg.pcan_gain_control.gain_bits = 21;
  cfg.log_scale.enable_log = 1;
  cfg.log_scale.scale_shift = 6;
  if (!FrontendPopulateState(&cfg, &fe, 16000)) { wlogln("[wake] Merkmalsberechnung liess sich nicht einrichten"); return false; }

  // --- Modell ---
  const tflite::Model *model = tflite::GetModel(g_wake_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) { wlogln("[wake] TFLite-Schema passt nicht"); return false; }
  arena = (uint8_t *)ps_malloc(WAKE_ARENA_BYTES);
  if (!arena) { wlogln("[wake] Speicher im PSRAM liess sich nicht reservieren"); return false; }
  // Das Modell im Strombetrieb nutzt RESOURCE VARIABLES (den Operator VAR_HANDLE)
  // fuer den Zustand zwischen zwei Auswertungen. Dafuer braucht es einen kleinen
  // eigenen Speicherbereich und MicroResourceVariables, die dem Interpreter
  // uebergeben werden, so wie ESPHome es macht. Ohne das schlaegt AllocateTensors
  // fehl.
  static alignas(16) uint8_t var_arena[1024];
  tflite::MicroAllocator *ma = tflite::MicroAllocator::Create(var_arena, sizeof(var_arena));
  tflite::MicroResourceVariables *mrv = tflite::MicroResourceVariables::Create(ma, 20);
  static tflite::AllOpsResolver resolver;
  static tflite::MicroInterpreter si(model, resolver, arena, WAKE_ARENA_BYTES, mrv);
  interp = &si;
  if (interp->AllocateTensors() != kTfLiteOk) { wlogln("[wake] AllocateTensors fehlgeschlagen"); return false; }

  TfLiteTensor *in  = interp->input(0);
  TfLiteTensor *out = interp->output(0);
  // erwarteter Eingang [1, stride, 40] als int8; Ausgang [1,1] als uint8
  if (in->dims->size != 3 || in->dims->data[2] != WAKE_FEATURE_SIZE || in->type != kTfLiteInt8) {
    char l[120]; snprintf(l, sizeof(l), "[wake] unerwarteter Eingang: dims=%d d2=%d type=%d", in->dims->size, in->dims->size>=3?in->dims->data[2]:-1, in->type); wlogln(l); return false;
  }
  model_stride = in->dims->data[1];
  char l[140];
  snprintf(l, sizeof(l), "[wake] OK: arena=%u/%d, stride=%d, out.type=%d (cutoff=%d win=%d)",
           (unsigned)interp->arena_used_bytes(), WAKE_ARENA_BYTES, model_stride, out->type, WAKE_PROB_CUTOFF, WAKE_WINDOW);
  wlogln(l);
  s_ready = true;
  return true;
}

bool wakeReady() { return s_ready; }
uint8_t wakeLastProb() { return last_prob; }

void wakeReset() {
  // Setzt den Erkennungszustand nach einem Wortwechsel zurueck: das Fenster der
  // Wahrscheinlichkeiten, die gesammelten Frames und den Zustand der
  // Merkmalsberechnung (Rauschunterdrueckung und PCAN). So loest der alte Ton
  // nach einer Antwort das Weckwort nicht faelschlich aus.
  recent_n = 0; recent_idx = 0; current_step = 0; last_prob = 0;
  if (s_ready) FrontendReset(&fe);
}

bool wakeFeed(const int16_t *samples, size_t n) {
  if (!s_ready || !samples) return false;
  bool detected = false;
  static int16_t g[2048];
  size_t pos = 0;
  while (pos < n) {
    size_t chunk = n - pos; if (chunk > 2048) chunk = 2048;
    // Verstaerkung des Weckwort-Wegs anwenden (mit Begrenzung): hebt den Pegel
    // normal gesprochener Sprache an.
    for (size_t j = 0; j < chunk; j++) {
      int32_t v = (int32_t)samples[pos + j] * gSettings.wakeGain;
      g[j] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
    pos += chunk;
    size_t off = 0;
    while (off < chunk) {
    size_t processed = 0;
    struct FrontendOutput fo = FrontendProcessSamples(&fe, g + off, chunk - off, &processed);
    off += processed;
    if (processed == 0) break;
    if (fo.size == 0) continue;   // Fenster noch nicht voll

    // uint16 der Merkmalsberechnung -> int8 (Formel aus ESPHome: mal 256,
    // geteilt durch 666, minus 128)
    TfLiteTensor *in = interp->input(0);
    int8_t *indata = tflite::GetTensorData<int8_t>(in);
    int8_t *slot = indata + WAKE_FEATURE_SIZE * current_step;
    for (int i = 0; i < WAKE_FEATURE_SIZE; i++) {
      int32_t v = ((int32_t)fo.values[i] * 256 + 333) / 666;
      v += -128;
      slot[i] = (int8_t)(v < -128 ? -128 : (v > 127 ? 127 : v));
    }
    if (++current_step < model_stride) continue;   // erst 'stride' Frames sammeln
    current_step = 0;

    if (interp->Invoke() != kTfLiteOk) { wlogln("[wake] Invoke fehlgeschlagen"); continue; }
    last_prob = interp->output(0)->data.uint8[0];

    // gleitendes Fenster, Mittelwert ueber der Schwelle (Werte zur Laufzeit aus
    // dem Web-Panel)
    const int win = gSettings.wakeWindow;   // in settings bereits auf 1..16 begrenzt
    recent[recent_idx] = last_prob;
    recent_idx = (recent_idx + 1) % win;
    if (recent_n < win) recent_n++;
    if (recent_n >= win) {
      int sum = 0;
      for (int i = 0; i < win; i++) sum += recent[i];
      if (sum > gSettings.wakeProbCutoff * win) {
        detected = true;
        recent_n = 0; recent_idx = 0;   // Sperrzeit: Fenster leeren, damit es nicht nachfeuert
      }
    }
    }   // while (off < chunk)
  }     // while (pos < n)
  return detected;
}

// --- Test der Kette ohne Mikrofon -------------------------------------------
#if WAKE_TEST
void wakeSelfTest() {
  static bool tried = false;
  if (!tried) { tried = true; if (!wakeBegin()) wlogln("[wake] Einrichten fuer den Test fehlgeschlagen"); }
  if (!s_ready) return;

  // DAUERHAFTES Zuhoeren: liest einen kurzen Block (etwa 20 ms) vom Mikrofon und
  // gibt ihn sofort an die Kette. Der Aufruf gehoert in eine ENGE Schleife ohne
  // Verzoegerung, damit das Modell im Strombetrieb einen durchgehenden Fluss
  // bekommt und das Weckwort nicht verpasst. Gibt jede Sekunde die hoechste
  // Wahrscheinlichkeit aus und meldet jede Erkennung.
  static int16_t buf[320];          // 20 ms bei 16 kHz
  static uint8_t maxp = 0;
  static uint32_t lastPrint = 0;
  size_t got = micReadChunk(buf, 320);
  if (got) {
    if (wakeFeed(buf, got)) {
      char l[100];
      snprintf(l, sizeof(l), "[wakeTest] *** WECKWORT ERKANNT! *** (Spitze=%u)", maxp);
      wlogln(l);
      maxp = 0;
    }
    if (last_prob > maxp) maxp = last_prob;
  }
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    char l[80];
    snprintf(l, sizeof(l), "[wakeTest] (1 s) hoechste Wahrscheinlichkeit=%u/255", maxp);
    wlogln(l);
    maxp = 0;
  }
}
#else
void wakeSelfTest() {}
#endif

#else  // !(WAKE_ENABLE || WAKE_TEST) -- wirkungslose Rumpffunktionen

bool    wakeBegin()    { return false; }
bool    wakeReady()    { return false; }
uint8_t wakeLastProb() { return 0; }
bool    wakeFeed(const int16_t *, size_t) { return false; }
void    wakeReset()    {}
void    wakeSelfTest() {}

#endif  // WAKE_ENABLE || WAKE_TEST
