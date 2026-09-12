// ============================================================================
//  ALEXO - Lichtanzeige (eigene Aufgabe auf Kern 0)
// ============================================================================
#include "ui.h"
#include "config.h"
#include <math.h>

static Adafruit_NeoPixel *R = nullptr;
static volatile AlexoState g_state = ST_IDLE;
static volatile uint8_t    g_level = 0;

// Alle Animationen rechnen mit der ECHTEN ZEIT (millis): so bleiben sie flüssig,
// auch wenn die Aufgabe einige Bilder lang übersprungen wird, etwa weil das WLAN
// auf Kern 0 dazwischenkommt. 't' sind die Millisekunden seit dem Start.
static inline void showAll() { R->show(); }

// --- Animationen ------------------------------------------------------------
// REAGIERT AUF TON: bunter Regenbogen, gesteuert vom Pegel (g_level). Die Farben
// wandern im HSV-Raum; je lauter der Ton, desto schneller die Drehung und desto
// heller das Licht. Bei Pegel 0 sind die LED AUS: Stille bedeutet Dunkelheit,
// ohne festen Grundschimmer. Benutzt wird das sowohl bei Ruhe als auch im
// Zustand MUSIK.
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
  animReactive(t);   // bei Ruhe tanzt er zum Mikrofon
#else
  // BEI RUHE: der Ring bleibt ganz AUS. Licht gibt es nur in den aktiven
  // Zuständen (Zuhören, Denken, Sprechen, Weckwort). Um wieder auf Geräusche zu
  // reagieren, IDLE_REACTIVE in config.h auf 1 setzen.
  (void)t;
  for (int i = 0; i < LED_RING_COUNT; i++) R->setPixelColor(i, 0, 0, 0);
#endif
}

static void animListening(uint32_t) {
  int lit = (g_level * LED_RING_COUNT + 127) / 255;
  for (int i = 0; i < LED_RING_COUNT; i++) {
    if (i < lit) {
      uint8_t r = (uint8_t)(i * 170 / (LED_RING_COUNT - 1));   // von Grün nach Rot, weich
      R->setPixelColor(i, r, 170 - r, 0);
    } else {
      R->setPixelColor(i, 0, 0, 0);
    }
  }
}

static void animThinking(uint32_t t) {
  // Komet in Violett und Cyan: der Kopf rückt alle 70 ms eine LED weiter
  int head = (int)((t / 70) % LED_RING_COUNT);
  for (int i = 0; i < LED_RING_COUNT; i++) {
    int d = (head - i + LED_RING_COUNT) % LED_RING_COUNT;
    int b = 150 - d * 42;
    if (b < 0) b = 0;
    R->setPixelColor(i, (uint8_t)(b * 0.5f), (uint8_t)(b * 0.3f), (uint8_t)b);
  }
}

static void animSpeaking(uint32_t t) {
  // REAGIERT auf die Stimme: g_level kommt während der Sprachausgabe vom
  // Mikrofon. Ein CYANFARBENES Pulsieren, das in den Pausen abfällt. Ein kleiner
  // Grundwert verhindert, dass es zwischen zwei Wörtern ganz ausgeht.
  (void)t;
  uint8_t b = (uint8_t)((uint16_t)g_level * 155 / 255);   // 0..155: bei Stille aus
  for (int i = 0; i < LED_RING_COUNT; i++) {
    R->setPixelColor(i, 0, b, b);   // Cyan (Grün und Blau gleich)
  }
}

// FORTLAUFENDER CHAT, "du bist dran": zwei gegenüberliegende BERNSTEINFARBENE
// Punkte, die langsam umlaufen.
// g_level wird hier mit Absicht NICHT benutzt: die Aussteuerungsanzeige ist das
// Zeichen, dass Alexo bereits aufnimmt, hier wartet er aber noch darauf, dass man
// anfängt. Es bleibt so, bis gesprochen wird (dann folgt ST_LISTENING) oder der
// Chat sich schließt. Die warme Farbe unterscheidet den Zustand von allen
// anderen.
// Die HELLIGKEIT IST ABSICHTLICH KONSTANT: die erste Fassung liess den ganzen
// Ring atmen, und weil er dabei fast bis zur Dunkelheit abfiel, sah es aus, als
// schlösse und öffnete sich der Chat in jedem Zyklus, also zwei- bis dreimal in
// den drei Sekunden Wartezeit. Etwas, das sich bewegt, OHNE je auszugehen, kann
// man nicht für "geschlossen" halten.
static void animFollowUp(uint32_t t) {
  int head = (int)((t / 110) % LED_RING_COUNT);          // etwa 1,3 s je Umlauf
  int opp  = (head + LED_RING_COUNT / 2) % LED_RING_COUNT;
  for (int i = 0; i < LED_RING_COUNT; i++) {
    bool acceso = (i == head || i == opp);
    // schwacher bernsteinfarbener Grundwert: der Ring ist während des Wartens NIE aus
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
  // umlaufender GRÜNER Komet (Aktualisierung über Funk läuft)
  int head = (int)((t / 70) % LED_RING_COUNT);
  for (int i = 0; i < LED_RING_COUNT; i++) {
    int d = (head - i + LED_RING_COUNT) % LED_RING_COUNT;
    int b = 160 - d * 42;
    if (b < 0) b = 0;
    R->setPixelColor(i, 0, (uint8_t)b, (uint8_t)(b * 0.15f));   // grün
  }
}

// --- Aufgabe (Kern 0) -------------------------------------------------------
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
      case ST_MUSIC:     animReactive(t);  break;   // Regenbogen nach dem Musikpegel
      case ST_FOLLOWUP:  animFollowUp(t);  break;   // "du bist dran" (fortlaufender Chat)
    }
    showAll();
    vTaskDelay(pdMS_TO_TICKS(25));   // etwa 40 Bilder je Sekunde
  }
}

void uiBegin(Adafruit_NeoPixel *ring) {
  R = ring;
  xTaskCreatePinnedToCore(uiTask, "ui", 4096, nullptr, 1, nullptr, 0 /* core 0 */);
}

void uiSetState(AlexoState s) { g_state = s; }
void uiSetLevel(uint8_t level) { g_level = level; }
