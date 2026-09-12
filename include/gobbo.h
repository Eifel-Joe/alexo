#pragma once
// ============================================================================
//  ALEXO - "Gobbo", der Teleprompter auf dem farbigen TFT-Display ST7735.
//  Der Text wandert sanft nach oben wie auf einem Teleprompter im Fernsehstudio.
//  Läuft in einer eigenen Aufgabe auf KERN 0 (dem alleinigen Besitzer des
//  HSPI-Busses zum TFT), damit der Bildlauf flüssig bleibt, auch während Kern 1
//  bei Spracherkennung, Claude oder Sprachausgabe wartet. Der Text kommt von
//  Kern 1 über eine Warteschlange (threadsicher). Gezeichnet wird auf eine
//  Fläche im RAM, die als Ganzes auf den Bildschirm kommt, deshalb flimmert
//  nichts.
//  Der Modulname "gobbo" bleibt, damit der Abgleich mit dem Originalprojekt
//  möglich bleibt.
// ============================================================================
#include <Arduino.h>
#include <Adafruit_ST7735.h>
#include "ui.h"   // AlexoState (für die farbige Zustandszeile am Kopf)

// Startet die Aufgabe des Teleprompters (Kern 0). Das Display muss bereits
// eingerichtet sein. Löscht den Bildschirm sofort.
void gobboBegin(Adafruit_ST7735 *disp);

// Aktualisiert die Zustandsanzeige am Kopf des TFT (bereit/höre zu/denke/...).
// Threadsicher (ein Byte wird unteilbar geschrieben). Von jedem Kern aufrufbar.
void gobboSetState(AlexoState s);

// Hängt eine ANTWORT von Alexo an den Chat an ("Alexo: ..."), gefolgt von einer
// Leerzeile. Startet mit der voreingestellten Bildlaufgeschwindigkeit (gut
// lesbar); gleich danach gobboScrollOver() aufrufen, um den Lauf an die Dauer
// der Stimme zu binden.
void gobboPrint(const String &text);

// Hängt eine FRAGE des Nutzers an den Chat an ("Du: ..."). Zeigt den neuesten
// Inhalt (Bildlauf ans Ende). Von jedem Kern aufrufbar.
void gobboPrintUser(const String &text);

// HINWEIS-Zeile (grün auf dem TFT, rot im Web-Panel: Rot ist auf diesem Display
// nicht lesbar), ohne das Präfix "Alexo:". Damit lässt sich etwas unmissverständlich
// sagen, das sonst unbemerkt bliebe. Bisher nur der Hinweis "dieser Teil ist ins
// Internet gegangen" (siehe localai.h). Von jedem Kern aufrufbar.
void gobboPrintWarn(const String &text);

// Legt die Zeit (ms) fest, in der der Bildlauf bis ans Ende gelaufen sein soll.
// Damit lässt er sich auf die Dauer des Gesprochenen abstimmen (wird von
// ttsSpeak aufgerufen, sobald der Ton beginnt). Von jedem Kern aufrufbar.
void gobboScrollOver(uint32_t ms);

// Leert den Teleprompter und löscht den Bildschirm.
void gobboClear();

// Fortschritt (0..100) der laufenden Aktualisierung über Funk. Gezeichnet wird
// er von der eigenen Aktualisierungsanzeige (im Stil einer grünen Anzeigetafel),
// sobald der Zustand ST_OTA ist. Von Kern 1 aufrufbar (Rückruf von ArduinoOTA):
// es wird nur ein Wert gesetzt, gezeichnet wird in der Aufgabe des Teleprompters.
void gobboOtaProgress(uint8_t percent);

// Angaben zum laufenden Titel (ICY-Metadaten des Radios): erscheinen in der
// eigenen Anzeige "AUF SENDUNG", sobald der Zustand ST_MUSIC ist (drei Zeilen in
// Größe 2 als Laufschrift: Sender / Titel / Interpret). Von Kern 1 aufrufbar
// (music.cpp).
void gobboNowPlaying(const char *station, const char *title, const char *artist);

// Art der laufenden Aktualisierung über Funk: true = Dateisystem und Webseite
// ("DATA"), false = Firmware ("FW"). Erscheint als Unterzeile in der
// Aktualisierungsanzeige.
void gobboOtaKind(bool isData);

// --- Eingaben vom Drehgeber (Kern 0) an die Verarbeitung (Kern 1) -----------
//  Der Drehgeber ist die EINZIGE Bedienung: der Klick startet und stoppt den
//  Chat (Umschalter), die Aufgabe des Teleprompters wertet ihn aus und stellt
//  ihn hier dem Hauptloop zur Verfügung.
//
// EINMAL true (danach zurückgesetzt), wenn der Nutzer den Chat STARTEN möchte.
bool gobboTakeTalkRequest();
// true, solange ein Wunsch offen ist, die Aufnahme zu BEENDEN (Klick während des
// Zuhörens). Dient micRecord als Abbruchbedingung.
bool gobboStopRequested();
// Setzt den Abbruchwunsch zurück: unmittelbar vor dem Aufnahmestart aufrufen.
void gobboClearStopRequest();
// Rastungen aus "gedrückt und gedreht", gesammelt WÄHREND der Musik
// (Senderwechsel): liefert den Unterschied vorwärts/rückwärts und setzt ihn
// zurück. 0, wenn kein Wechsel gewünscht ist. Nur in ST_MUSIC aktiv, ausserhalb
// der Musik bleibt "gedrückt und gedreht" die Lautstärke.
int32_t gobboTakeMusicSeek();

// --- Chat für das Web-Panel -------------------------------------------------
// Zähler für Änderungen: springt bei jeder neuen Nachricht im Chat weiter. Das
// Panel liest ihn (in /api/live) und lädt den Chat NUR neu, wenn er sich
// geändert hat.
uint32_t gobboChatRev();
// Zugriff auf die Nachrichten des Chats, um sie auszuliefern (r: 0=Alexo,
// 1=Nutzer, 2=System; Text in UTF-8). gobboChatCount() = wie viele;
// gobboChatItem() füllt role und text (der Zeiger gilt für die Dauer der
// Anfrage). Von Kern 1 aufzurufen (webui).
int  gobboChatCount();
bool gobboChatItem(int i, uint8_t *role, const char **text);
