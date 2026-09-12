#pragma once
// ============================================================================
//  ALEXO - Musikwiedergabe (MP3-Webradio -> VS1053).
//  Die Sender sind MP3-Ströme über einfaches HTTP (181.fm): der VS1053/VS1003
//  decodiert MP3 in Hardware, deshalb wird dieselbe stückweise Zuführung wie
//  bei der Sprachausgabe (tts.cpp) wiederverwendet, nur ist die Quelle hier ein
//  ENDLOSER Strom.
//  Sprachbefehl: "spiel <Genre>". Die Senderliste lässt sich im Web-Panel
//  bearbeiten (gSettings.musicStations), und Claude wählt über das Werkzeug
//  riproduci_musica aus dem internen Katalog.
//  Hinweis: Die Schlüsselwörter der Senderliste sind noch italienisch, siehe
//  den offenen Punkt in docs/specs/2026-09-12-uebersetzung-deutsch.md.
// ============================================================================
#include <Arduino.h>
#include <VS1053.h>

// Ein Sender: die URL des MP3-Stroms und die Bezeichnung für das Display.
struct MusicStation { const char *url; const char *nome; };

// Erkennt einen Musikbefehl im Text (der BEREITS klein geschrieben ist), indem
// er mit der im Panel bearbeitbaren Liste verglichen wird
// (gSettings.musicStations). Liefert den zu spielenden Sender oder nullptr,
// wenn nichts passt.
const MusicStation *musicMatch(const String &testoLower);

// Rückfallweg: bildet ein von Claude gewähltes GENRE (Werkzeug
// riproduci_musica) auf einen Sender des geprüften internen Katalogs ab.
// nullptr, wenn das Genre nicht im Katalog steht.
const MusicStation *musicFromGenre(const String &genere);
// Liste der Genres im Katalog, für die Beschreibung des Werkzeugs für Claude.
String musicCatalogList();

// Spielt den MP3-Strom über den VS1053, bis stopRequested() true liefert (Klick
// auf den Drehgeber), musicRequestStop() gerufen wird (Schaltfläche im
// Web-Panel) oder der Strom abreisst. Liefert seekRequested() einen Unterschied
// != 0 (gedrückt und gedreht während der Musik), bricht die Wiedergabe ab und
// GIBT diesen Unterschied ZURÜCK, damit der Aufrufer zu einem anderen Sender
// wechselt; 0, wenn die Wiedergabe zu Ende ist oder angehalten wurde. Blockiert
// Kern 1 für die gesamte Dauer, hält aber die Aktualisierung über Funk, Telnet
// und das Web-Panel AM LEBEN (neu flashen geht nur über Funk).
int musicPlay(VS1053 &player, const char *url, bool (*stopRequested)(), int (*seekRequested)());

// --- Bewegen in der Senderliste des Panels (für gedrückt und gedreht) -------
// Zahl der gültigen Sender in gSettings.musicStations.
int  musicStationCount();
// Füllt url und nome mit dem Sender an der Stelle idx (ab 0). false, wenn die
// Stelle außerhalb der Liste liegt.
bool musicStationGet(int idx, String &url, String &nome);
// Stelle des Senders mit dieser URL in der Liste, oder -1, wenn er fehlt.
int  musicStationIndexOf(const char *url);

// Fordert das Anhalten der Wiedergabe an (Aufruf aus dem Web-Panel,
// threadsicher).
void musicRequestStop();
// Fordert das EINSCHALTEN des Radios aus dem Web-Panel an (Schaltfläche "Radio
// einschalten"): es startet der erste Sender der Liste. Die HTTP-Bearbeitung
// kann das nicht selbst starten (musicPlay blockiert Kern 1, solange das Radio
// läuft), sie hinterlässt also nur den Wunsch, und die Wiedergabe beginnt im
// Loop von main.cpp.
void musicRequestStart();
// Holt den Startwunsch ab und setzt ihn zurück (wird vom Loop gelesen).
// true = das Radio ist einzuschalten.
bool musicTakeStartRequest();
// Fordert den SENDERWECHSEL aus dem Web-Panel an (Schaltflächen vor und
// zurück): derselbe Unterschied wie bei gedrückt und gedreht am Drehgeber
// (+1 = nächster, -1 = vorheriger). Gilt nur, während ein Sender läuft; bei
// stummer Musik startet dadurch nichts.
void musicRequestSeek(int delta);
// true, solange ein Strom läuft (für die Anzeige im Web-Panel).
bool musicIsPlaying();
// Roher MAD-Wert und Grundlinie des letzten Musikblocks (zum Abstimmen der
// Empfindlichkeit des Rings: Felder musMad und musBase in /api/live). 0, wenn
// nichts läuft.
float musicLastMad();
float musicLastBase();

// ICY-Metadaten des laufenden Stroms: Name des Senders (icy-name) und aktueller
// Titel ("Interpret - Titel" aus StreamTitle). Leere Zeichenkette, wenn sie
// fehlen oder gerade keine Musik läuft.
String musicStation();
String musicNowPlaying();
