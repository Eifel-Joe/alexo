// ============================================================================
//  ALEXO - Rückmeldetöne
//  Erzeugt einen kurzen Ton (Sinus als PCM mit 16 kHz, mit Ein- und Ausblenden
//  gegen Knacken), packt ihn als WAV und gibt ihn an den VS1053. Ohne
//  Tondateien.
// ============================================================================
#include "sound.h"
#include <Arduino.h>
#include <math.h>

static void wr32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void wr16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }

static void playTone(VS1053 &player, uint16_t freq, uint16_t ms, uint8_t vol) {
  const uint32_t sr = 16000;
  const size_t   tone    = (size_t)sr * ms / 1000;   // Abtastwerte des Tons
  const size_t   silence = sr * 70 / 1000;           // etwa 70 ms Stille als Ausklang
  const size_t   n       = tone + silence;
  const size_t   dataLen = n * 2;
  uint8_t *wav = (uint8_t *)ps_malloc(44 + dataLen);   // PSRAM: keine Zersplitterung
  if (!wav) return;

  // WAV-Kopf (Mono, 16 Bit, 16 kHz)
  memcpy(wav + 0, "RIFF", 4);  wr32(wav + 4, 36 + dataLen);
  memcpy(wav + 8, "WAVE", 4);  memcpy(wav + 12, "fmt ", 4);
  wr32(wav + 16, 16); wr16(wav + 20, 1); wr16(wav + 22, 1);
  wr32(wav + 24, sr); wr32(wav + 28, sr * 2); wr16(wav + 32, 2); wr16(wav + 34, 16);
  memcpy(wav + 36, "data", 4); wr32(wav + 40, dataLen);

  int16_t *pcm = (int16_t *)(wav + 44);
  const size_t fade = sr / 200;   // etwa 5 ms Ein- und Ausblenden (gegen Knacken)
  for (size_t i = 0; i < n; i++) {
    if (i >= tone) { pcm[i] = 0; continue; }          // Ausklang in Stille
    float amp = 9000.0f;
    if (i < fade)            amp *= (float)i / fade;
    else if (i > tone - fade) amp *= (float)(tone - i) / fade;
    pcm[i] = (int16_t)(amp * sinf(2.0f * (float)M_PI * freq * i / sr));
  }

  player.setVolume(vol);
  // Wiedergabe wie bei der Sprachausgabe: Daten zuführen und mit Stille
  // ausklingen, OHNE stopSong(). Dieser Aufruf schickt ein "cancel", an dem sich
  // der VS1053 bei kurzen Tönen verschluckt.
  size_t off = 0, tot = 44 + dataLen;
  while (off < tot) {
    size_t c = min((size_t)512, tot - off);
    player.playChunk(wav + off, c);
    off += c;
  }
  uint8_t z[32];
  memset(z, 0, sizeof(z));
  for (int i = 0; i < 64; i++) player.playChunk(z, sizeof(z));  // etwa 2 KB endFillByte
  free(wav);
}

void soundStart(VS1053 &player) { playTone(player, 1200, 110, 90); }
void soundStop(VS1053 &player)  { playTone(player, 700, 110, 90); }
void soundError(VS1053 &player) {
  playTone(player, 400, 130, 90);
  delay(60);
  playTone(player, 320, 160, 90);
}
