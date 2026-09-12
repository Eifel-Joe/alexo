#pragma once
// ============================================================================
//  ALEXO - Lautstärke des VS1053 (Skala 0..100, in NVS gespeichert).
//
//  Zwei Kerne: der Drehgeber läuft in der AUFGABE der Anzeige (Kern 0) und
//  sammelt die Wünsche mit volumeRequest(); der VS1053 hängt am SPI-Bus von
//  Kern 1, deshalb wendet IMMER Kern 1 sie mit volumeApplyPending() an (im Loop
//  bei Ruhe und während der Sprachausgabe, solange Alexo spricht). So streiten
//  sich die beiden nicht um den Bus.
// ============================================================================
#include <Arduino.h>
#include <VS1053.h>

// Lädt die gespeicherte Lautstärke (oder VOLUME_DEFAULT) und übergibt sie dem
// Player.
void volumeBegin(VS1053 &player);

// Aufruf aus der Drehgeber-Aufgabe (Kern 0): sammelt Rastungen (>0 / <0). Rührt
// die Hardware nicht an, merkt sich nur den Wunsch.
void volumeRequest(int32_t detents);

// Setzt einen ABSOLUTEN Wert 0..100 (aus dem Web-Panel, Kern 0 oder eine andere
// Aufgabe): rührt die Hardware nicht an, das Anwenden übernimmt Kern 1 mit
// volumeApplyPending.
void volumeSet(int percent);

// Aufruf aus Kern 1: liegt ein Wunsch vor, wird die Lautstärke geändert, an den
// VS1053 geschrieben und gespeichert. Liefert true, wenn sie sich geändert hat.
bool volumeApplyPending(VS1053 &player);

// Aktuelle Lautstärke aus Sicht des Nutzers (0..100), für Display und Panel.
uint8_t volumeGet();

// Wert für player.setVolume(): die Nutzerlautstärke 1..100, umgerechnet auf den
// HÖRBAREN Bereich des VS1053 (VOLUME_VS_MIN..100), 0 bei stumm. In ALLEN
// Aufrufen von setVolume (Sprachausgabe, Musik, Lautstärke) statt volumeGet()
// zu verwenden.
uint8_t volumeVsValue();

// true, wenn die Nutzerlautstärke 0 ist (wirklich stumm: Stille, und der
// Verstärker ist abzuschalten).
bool volumeIsMuted();
