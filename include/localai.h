#pragma once
// ============================================================================
//  ALEXO - KI-Dienste ZU HAUSE (LM Studio und ähnliche)
//  Gemeinsame Helfer für die drei Glieder der Sprachkette (stt / llm / tts):
//  Sie müssen wissen, ob der PC zu Hause antwortet und welches Modell er
//  geladen hat, den Rest erledigen sie selbst. Für alle drei gilt dieselbe
//  Regel:
//    LEERE Adresse   -> Dienst zu Hause aus, es geht wie immer in die Cloud
//    Adresse gesetzt -> antwortet der PC, wird er benutzt, sonst die Cloud
//  AUSNAHME, gSettings.localOnly ("nur zu Hause", aus dem Panel): für diese
//  drei wird die Cloud NIE benutzt. Fehlt der Dienst zu Hause oder macht er
//  einen Fehler, sagt Alexo es im Chat und hört auf. Radio (das fordert man
//  selbst an) und die NTP-Uhr bleiben unberührt.
//  Adressen und Modellnamen kommen aus gSettings (Web-Panel), der Aufrufer muss
//  also nur sagen, UM WELCHEN Dienst es geht.
//  Die Dienste zu Hause sprechen unverschlüsseltes http:// (kein TLS): sie
//  stehen im eigenen Netz.
// ============================================================================
#include <Arduino.h>

// Die Reihenfolge ist die, in der die Punkte auf dem Display erscheinen.
enum LocalSvc { LOC_STT = 0, LOC_LLM = 1, LOC_TTS = 2, LOC_COUNT = 3 };

// Lässt sich dieses Glied wirklich zu Hause erledigen? Versucht die Verbindung
// mit sehr kurzer Wartezeit, damit die Sprachkette bei ausgeschaltetem PC nicht
// hängt.
// Achtung: es genügt nicht, dass der Server antwortet. Das GEHIRN braucht auch
// ein geladenes Modell, sonst schlüge die Frage fehl und man landete nach
// verlorener Zeit doch in der Cloud. Das Ergebnis bleibt einige Sekunden
// gespeichert, es lässt sich also bei jeder Frage abrufen, ohne jedes Mal zu
// kosten.
bool localReachable(LocalSvc svc);

// Nur "der Server antwortet", ohne die Frage, ob er arbeitsbereit ist. Das Panel
// braucht es, um "aus" von "an, aber ohne geladenes Modell" zu unterscheiden.
bool localConnected(LocalSvc svc);

// Läuft dieses Glied GERADE JETZT zu Hause? Das ist das letzte BEKANNTE Ergebnis
// von localReachable, ohne einen neuen Versuch und ohne das Netz anzufassen:
// das Display sieht darauf (es läuft auf Kern 0 und kann nicht warten). Für die
// STIMME muss zusätzlich einer der beiden Schalter gesetzt sein: ohne ihn darf
// der Server zu Hause antworten, so viel er will, gesprochen wird von
// ElevenLabs (siehe ttsUsesLocal in tts.h).
bool localOn(LocalSvc svc);

// Versucht es bei EINEM Dienst erneut, reihum, wenn genug Zeit vergangen ist.
// Aus dem Loop aufzurufen, während Alexo ruht: so bleiben die Punkte auf dem
// Display ehrlich, auch wenn gerade nichts gefragt wird. Liefert true, wenn
// wirklich ein Versuch stattfand (der Aufrufer verwirft dann den inzwischen
// gelesenen Ton).
bool localRefreshTick();

// Name des Modells für diesen Dienst. Steht er im Panel, wird er genommen; ist
// er LEER, wird der Server gefragt, welches Modell gerade geladen ist (GET
// <base>/models, das erste der Liste). So lässt sich das Modell am PC wechseln,
// ohne das Panel anzufassen. Liefert "", wenn es sich nicht ermitteln lässt.
String localModelName(LocalSvc svc);

// Basisadresse des Dienstes SO, WIE SIE IM PANEL STEHT, "" wenn er aus ist.
// Rührt das Netz nicht an: sie sagt, ob der Dienst eingerichtet ist, nicht, ob
// er erreichbar ist.
String localBaseUrl(LocalSvc svc);

// Die Adresse, die für Anfragen WIRKLICH benutzt wird. Steht im Panel der Port,
// ist sie mit localBaseUrl identisch und kostet nichts. Fehlt der Port (nur bei
// der STIMME), werden die Ports aus LOCAL_TTS_PORTS_AUTO durchprobiert und der
// antwortende geliefert. So wechselt man zwischen Kokoro und Chatterbox, indem
// man am PC den einen oder anderen startet, ohne das Feld zu ändern. Antwortet
// keiner, kommt der erste zurück, damit die Fehlermeldung eine sinnvolle
// Adresse nennt. "" wenn der Dienst aus ist.
String localBaseUsed(LocalSvc svc);

// Vergisst die gespeicherten Ergebnisse (Erreichbarkeit und Modellname): beim
// nächsten Versuch wird alles neu erfragt. Das braucht die Schaltfläche
// "Testen" im Panel und der Wechsel der Adressen.
void localForget();

// --- Unmissverständlich sagen -----------------------------------------------
//  Der Rückfall auf die Cloud ist bequem, aber leise: bei grünen Anzeigen
//  glaubt man, zu Hause zu sein, während Stimme, Frage oder Antwort soeben ins
//  Internet gegangen sind. Die beiden folgenden schreiben es in ROT in den Chat
//  (auf dem TFT und im Web-Panel).

// "Dieses Glied ist soeben ins Internet gegangen." Schreibt NUR, wenn der Dienst
// zu Hause eingerichtet ist: wer absichtlich in der Cloud arbeitet, braucht den
// Hinweis nicht jedes Mal.
void localSayCloud(LocalSvc svc);

// "Nur zu Hause ist eingeschaltet und der Dienst zu Hause fehlt: ich höre hier
// auf." Schreibt immer.
void localSayBlocked(LocalSvc svc);

// Entfernt das "Nachdenken", das etliche Modelle zu Hause vor der eigentlichen
// Antwort ausgeben (der Block <think>...</think>). Es ist ohnehin nötig, auch
// wenn man darum bittet, nicht nachzudenken: nicht alle Modelle halten sich
// daran.
String localStripThink(const String &s);
