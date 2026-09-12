// ============================================================================
//  ALEXO - Lautstärkeregelung des VS1053 (siehe volume.h).
// ============================================================================
#include "volume.h"
#include "config.h"
#include <Preferences.h>

// pendingDelta schreibt Kern 0 (die Drehgeber-Aufgabe), Kern 1 liest und setzt
// es zurück. Es ist ein ausgerichteter int32, auf dem Xtensa also unteilbar zu
// lesen und zu schreiben. Verloren gehen könnte höchstens eine einzelne Rastung
// bei einem genauen Zusammentreffen, was bei einem Lautstärkeknopf nicht ins
// Gewicht fällt.
static volatile int32_t pendingDelta = 0;
static volatile int32_t pendingAbs   = -1;   // absoluter Wert aus dem Web-Panel (-1 = keiner)
static uint8_t          curVol = VOLUME_DEFAULT;
static Preferences      prefs;

// Nutzerlautstärke (0..100) -> Wert für den VS1053. Der VS1053 arbeitet
// logarithmisch: sein unterer Bereich ist fast stumm, deshalb wird 1..100 auf
// den hörbaren Bereich VOLUME_VS_MIN..100 umgerechnet, damit der ganze Weg des
// Reglers etwas bewirkt. 0 = stumm.
uint8_t volumeVsValue() {
  if (curVol == 0) return 0;
  return (uint8_t)map(curVol, 1, 100, VOLUME_VS_MIN, 100);
}
bool volumeIsMuted() { return curVol == 0; }

void volumeBegin(VS1053 &player) {
  prefs.begin("alexo", false);                 // Namensraum im NVS
  curVol = prefs.getUChar("vol", VOLUME_DEFAULT);
  if (curVol > 100)        curVol = 100;
  if (curVol < VOLUME_MIN) curVol = VOLUME_MIN;
  player.setVolume(volumeVsValue());
  Serial.printf("[vol] Lautstärke beim Start: %u%% (VS1053 %u)\n", curVol, volumeVsValue());
}

void volumeRequest(int32_t detents) {
  pendingDelta += detents;                     // Kern 0 sammelt nur
}

void volumeSet(int percent) {
  if (percent < VOLUME_MIN) percent = VOLUME_MIN;
  if (percent > 100)        percent = 100;
  pendingAbs = percent;                        // Kern 1 wendet es an (siehe unten)
}

bool volumeApplyPending(VS1053 &player) {
  // Zuerst ein etwaiger ABSOLUTER Wert aus dem Web-Panel.
  int32_t abs = pendingAbs;
  if (abs >= 0) {
    pendingAbs = -1;
    if ((uint8_t)abs != curVol) {
      curVol = (uint8_t)abs;
      player.setVolume(volumeVsValue());
      prefs.putUChar("vol", curVol);
      Serial.printf("[vol] Lautstärke (Panel): %u%% (VS1053 %u)\n", curVol, volumeVsValue());
      return true;
    }
    return false;
  }

  int32_t d = pendingDelta;
  if (d == 0) return false;
  pendingDelta -= d;                           // genau so viel abziehen wie gelesen

  int32_t v = (int32_t)curVol + d * VOLUME_STEP;
  if (v < VOLUME_MIN) v = VOLUME_MIN;
  if (v > 100)        v = 100;
  if ((uint8_t)v == curVol) return false;

  curVol = (uint8_t)v;
  player.setVolume(volumeVsValue());
  prefs.putUChar("vol", curVol);               // speichern (das NVS verteilt die Schreibzugriffe)
  Serial.printf("[vol] Lautstärke: %u%% (VS1053 %u)\n", curVol, volumeVsValue());
  return true;
}

uint8_t volumeGet() { return curVol; }
