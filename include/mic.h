#pragma once
// ============================================================================
//  ALEXO - Mikrofon. Ab Werk das ICS-43434 über I2S (MIC_USE_I2S=1), als
//  Alternative das analoge MAX4466 über ADC1 (MIC_USE_I2S=0).
//  Nimmt PCM mit 16 Bit Mono bei MIC_SAMPLE_RATE in einen Puffer im PSRAM auf
//  und erzeugt daraus eine WAV-Datei, die an die Spracherkennung gehen kann.
// ============================================================================
#include <Arduino.h>

// Richtet die Aufnahme ein und reserviert die Puffer im PSRAM. Liefert false,
// wenn kein PSRAM vorhanden ist oder die Reservierung scheitert.
bool micBegin();

// Nimmt auf, solange keepGoing() true liefert, höchstens aber maxMs lang.
// onLevel(0..255) wird, sofern übergeben, etwa alle 20 ms mit dem aktuellen
// Pegel aufgerufen (nützlich für eine Aussteuerungsanzeige auf LED oder
// Display).
// Liefert die Zahl der aufgenommenen Abtastwerte.
// silenceMs > 0: bricht nach so langer ununterbrochener Stille selbsttätig ab,
// nachdem zuvor Sprache zu hören war. 0 = abgeschaltet, dann endet die Aufnahme
// nur bei keepGoing()==false oder nach maxMs.
size_t micRecord(uint32_t maxMs, bool (*keepGoing)(), void (*onLevel)(uint8_t) = nullptr, uint32_t silenceMs = 0);

// Höchste Wartezeit in ms, bevor aufgegeben wird, wenn KEINE Stimme zu hören
// ist. Gilt nur für die NÄCHSTE Aufnahme, danach gilt wieder die normale
// Wartezeit (REC_MIN_MS + silenceMs + 1 s). Der fortlaufende Chat braucht das:
// nach einer Antwort öffnet das Mikrofon erneut, und wenn binnen weniger
// Sekunden niemand spricht, schliesst es wieder, ohne die Wartezeit zu
// verlängern, wenn der Chat von Hand gestartet wurde.
void micSetNoVoiceMs(uint32_t ms);

// true, wenn die LAUFENDE Aufnahme bereits echte Sprache gehört hat (dieselbe
// mitlaufende Schwelle wie micHeardVoice, aber während der Aufnahme lesbar).
// Damit lässt sich erkennen, dass die Frage begonnen hat, ohne das Ende
// abzuwarten.
bool micVoiceStarted();

// Leert den DMA-Puffer des Mikrofons (verwirft den angesammelten Ton). Nach
// einem Wortwechsel aufzurufen, bevor wieder auf das Weckwort gehört wird, sonst
// löst es fälschlich aus.
void micFlush();

// Liest bis zu 'maxn' rohe PCM-Abtastwerte mit 16 Bit vom Mikrofon (ein oder
// mehrere i2s_read, blockierend etwa maxn/16 ms). "Roh" heisst nur verschoben,
// OHNE Hochpass: das braucht das Weckwort, dessen Merkmalsberechnung eigene
// Filter mitbringt (Filterbank ab 125 Hz).
// Liefert die tatsächlich gelesenen Abtastwerte, 0 wenn nichts verfügbar ist.
size_t micReadChunk(int16_t *out, size_t maxn);

// Lautstärkepegel (0..255) für den Ring, berechnet aus einem bereits
// vorliegenden PCM-Block mit 16 Bit (Hochpass, selbsttätig nachgeführter
// Grundpegel, Hüllkurve). Der Loop des Weckworts steuert damit den Ring aus
// DEMSELBEN Block, den auch das Modell bekommt.
uint8_t micLevelFromChunk(const int16_t *s, size_t n);

// Untersuchung des Grundrauschens am I2S-Mikrofon (Schritt 0 des Weckworts).
// EINMALIG: misst etwa 600 ms, schreibt eine Zeile auf die serielle
// Schnittstelle und kehrt zurück. Im Loop im Wechsel mit ArduinoOTA.handle()
// aufzurufen, damit die Aktualisierung über Funk erreichbar bleibt.
// Ausgegeben werden Wechselanteil und Spitzenwert im 24-Bit-Bereich sowie die
// Hochrechnung auf verschiedene Werte von I2S_SHIFT. Aussagekräftig nur mit dem
// I2S-Mikrofon; mit dem MAX4466 erscheint ein Hinweis.
void micDiag();

// Schnelle Abfrage des Umgebungspegels (0..255), für Animationen bei Ruhe, die
// auf Geräusche reagieren. Tastet ein sehr kurzes Fenster ab (etwa 2 ms) und
// liefert den skalierten Spitze-Spitze-Wert. Stört die Aufnahme nicht.
uint8_t micPeekLevel();

// Die jüngsten Werte des LED-Wegs (micLevelFromChunk) für das Web-Panel: Pegel
// 0..255, der geschätzte Grundpegel und die aktuelle Einschaltschwelle. Werden
// bei jedem Block aktualisiert, der bei Ruhe gelesen wird. Nützlich, um die
// Werte für LED und Stille im Browser in Echtzeit abzustimmen.
void micGetLive(uint8_t *level, float *noiseFloor, float *thresh);

// Zugriff auf die Daten der letzten Aufnahme.
const int16_t *micPcm();         // PCM-Abtastwerte, 16 Bit Mono
size_t         micSampleCount(); // wie viele Abtastwerte
uint32_t       micSampleRate();  // Abtastrate (Hz)
int            micLastPeak();    // absoluter Spitzenwert der letzten Aufnahme (0..32767)
bool           micHeardVoice();  // true, wenn echte Sprache erkannt wurde (über der mitlaufenden Schwelle)

// Baut im PSRAM eine vollständige WAV-Datei (44 Byte Kopf und PCM) aus der
// letzten Aufnahme. Liefert den Zeiger und schreibt die Gesamtlänge nach *len.
// Der Puffer gilt bis zur nächsten Aufnahme.
const uint8_t *micWav(size_t *len);
