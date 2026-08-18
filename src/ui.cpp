// ============================================================================
//  ALEXO - UI luminosa (task sul core 0)
// ============================================================================
#include "ui.h"
#include "config.h"
#include <math.h>

static Adafruit_NeoPixel *R = nullptr;
static volatile AlexoState g_state = ST_IDLE;
static volatile uint8_t    g_level = 0;

// Tutte le animazioni sono basate sul TEMPO REALE (millis): cosi' restano
// fluide anche se il task viene saltato per qualche frame (contesa col WiFi
// sul core 0). 't' = millisecondi dall'avvio.
static inline void showAll() { R->show(); }

// --- Animazioni -------------------------------------------------------------
// REATTIVO AL SUONO: arcobaleno psichedelico pilotato dal livello audio
// (g_level). Colori HSV che ruotano; piu' forte il suono, piu' veloce la
// rotazione e piu' intensa la luce. A livello 0 i LED sono SPENTI (silenzio =
// buio, niente fondo fisso). Lo usano sia l'idle reattivo sia lo stato MUSICA.
static void animReactive(uint32_t t) {
  uint8_t b = (uint8_t)((uint16_t)g_level * 160 / 255);   // 0..160
  uint16_t baseHue = (uint16_t)(t * (6 + g_level / 8));
  for (int i = 0; i < LED_RING_COUNT; i++) {
    uint16_t h = baseHue + (uint16_t)(i * 65536L / LED_RING_COUNT);
    R->setPixelColor(i, R->ColorHSV(h, 255, b));
  }
}

static void animIdle(uint32_t t) {
#if IDLE_REACTIVE
  animReactive(t);   // a riposo balla col microfono
#else
  // A RIPOSO: ring completamente SPENTO. Le luci compaiono solo negli stati
  // attivi (ascolto/penso/parlo/wake). Per riavere l'effetto reattivo al suono
  // metti IDLE_REACTIVE 1 in config.h.
  (void)t;
  for (int i = 0; i < LED_RING_COUNT; i++) R->setPixelColor(i, 0, 0, 0);
#endif
}

static void animListening(uint32_t) {
  int lit = (g_level * LED_RING_COUNT + 127) / 255;
  for (int i = 0; i < LED_RING_COUNT; i++) {
    if (i < lit) {
      uint8_t r = (uint8_t)(i * 170 / (LED_RING_COUNT - 1));   // verde->rosso, soft
      R->setPixelColor(i, r, 170 - r, 0);
    } else {
      R->setPixelColor(i, 0, 0, 0);
    }
  }
}

static void animThinking(uint32_t t) {
  // cometa viola-ciano: la testa avanza di 1 LED ogni 70 ms
  int head = (int)((t / 70) % LED_RING_COUNT);
  for (int i = 0; i < LED_RING_COUNT; i++) {
    int d = (head - i + LED_RING_COUNT) % LED_RING_COUNT;
    int b = 150 - d * 42;
    if (b < 0) b = 0;
    R->setPixelColor(i, (uint8_t)(b * 0.5f), (uint8_t)(b * 0.3f), (uint8_t)b);
  }
}

static void animSpeaking(uint32_t t) {
  // REATTIVA alla voce: g_level arriva dal mic durante il TTS. Pulsazione CIANO che
  // cala nei silenzi. Piccolo fondo per non spegnersi del tutto tra una parola e l'altra.
  (void)t;
  uint8_t b = (uint8_t)((uint16_t)g_level * 155 / 255);   // 0..155: nel silenzio -> spento
  for (int i = 0; i < LED_RING_COUNT; i++) {
    R->setPixelColor(i, 0, b, b);   // ciano (verde+blu uguali)
  }
}

// CHAT CONTINUA, "tocca a te": due punti AMBRA opposti che girano piano.
// NON usa g_level di proposito: il VU-meter e' il segno che Alexo ti sta gia'
// registrando, qui invece sta ancora aspettando che tu cominci. Resta cosi'
// finche' non parti (allora passa a ST_LISTENING) o finche' la chat non si
// chiude - il colore caldo lo distingue da tutti gli altri stati.
// LUMINOSITA' COSTANTE, di proposito: la prima versione era un respiro su tutto
// l'anello, e calando fino quasi al buio sembrava che la chat si chiudesse e
// riaprisse a ogni ciclo (~2-3 volte nei 3 secondi di attesa). Qualcosa che si
// muove SENZA mai spegnersi non si puo' scambiare per "si e' chiuso".
static void animFollowUp(uint32_t t) {
  int head = (int)((t / 110) % LED_RING_COUNT);          // ~1,3 s per giro
  int opp  = (head + LED_RING_COUNT / 2) % LED_RING_COUNT;
  for (int i = 0; i < LED_RING_COUNT; i++) {
    bool acceso = (i == head || i == opp);
    // fondo ambra tenue sempre acceso: l'anello non e' MAI spento durante l'attesa
    if (acceso) R->setPixelColor(i, 150, 68, 0);
    else        R->setPixelColor(i, 14,   6, 0);
  }
}

static void animError(uint32_t t) {
  bool on = ((t / 180) % 2) == 0;
  for (int i = 0; i < LED_RING_COUNT; i++) {
    R->setPixelColor(i, on ? 130 : 0, 0, 0);
  }
}

static void animOta(uint32_t t) {
  // cometa VERDE che gira (aggiornamento OTA in corso)
  int head = (int)((t / 70) % LED_RING_COUNT);
  for (int i = 0; i < LED_RING_COUNT; i++) {
    int d = (head - i + LED_RING_COUNT) % LED_RING_COUNT;
    int b = 160 - d * 42;
    if (b < 0) b = 0;
    R->setPixelColor(i, 0, (uint8_t)b, (uint8_t)(b * 0.15f));   // verde
  }
}

// --- Task (core 0) ----------------------------------------------------------
static void uiTask(void *) {
  for (;;) {
    uint32_t t = millis();
    switch (g_state) {
      case ST_IDLE:      animIdle(t);      break;
      case ST_LISTENING: animListening(t); break;
      case ST_THINKING:  animThinking(t);  break;
      case ST_SPEAKING:  animSpeaking(t);  break;
      case ST_ERROR:     animError(t);     break;
      case ST_OTA:       animOta(t);       break;
      case ST_MUSIC:     animReactive(t);  break;   // arcobaleno sul livello musica
      case ST_FOLLOWUP:  animFollowUp(t);  break;   // "tocca a te" (chat continua)
    }
    showAll();
    vTaskDelay(pdMS_TO_TICKS(25));   // ~40 fps
  }
}

void uiBegin(Adafruit_NeoPixel *ring) {
  R = ring;
  xTaskCreatePinnedToCore(uiTask, "ui", 4096, nullptr, 1, nullptr, 0 /* core 0 */);
}

void uiSetState(AlexoState s) { g_state = s; }
void uiSetLevel(uint8_t level) { g_level = level; }
