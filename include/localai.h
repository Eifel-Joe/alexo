#pragma once
// ============================================================================
//  ALEXO - Servizi AI IN CASA (LM Studio & co.)
//  Aiutanti condivisi dai tre moduli della catena vocale (stt / llm / tts): a
//  loro serve sapere se il PC di casa risponde e quale modello ha caricato, il
//  resto lo fanno da soli. Regola unica per tutti e tre:
//    indirizzo VUOTO  -> servizio locale spento, si va in cloud come sempre
//    indirizzo pieno  -> se il PC risponde si usa quello, altrimenti cloud
//  ECCEZIONE, gSettings.localOnly ("solo casa", dal pannello): il cloud non si
//  usa MAI per questi tre. Se il servizio di casa non c'e' o sbaglia, Alexo lo
//  dice in chat e si ferma. Non tocca la radio (la chiedi tu) ne' l'orologio NTP.
//  Gli indirizzi e i nomi modello li legge da gSettings (pannello web), quindi
//  chi chiama deve dire solo DI QUALE servizio si parla.
//  I servizi in casa parlano http:// in chiaro (niente TLS): sono sulla LAN.
// ============================================================================
#include <Arduino.h>

// L'ordine e' quello in cui i pallini compaiono sul display.
enum LocalSvc { LOC_STT = 0, LOC_LLM = 1, LOC_TTS = 2, LOC_COUNT = 3 };

// Questo pezzo si puo' davvero fare in casa? Prova ad aprire la connessione con
// un'attesa cortissima, cosi' col PC spento non si blocca la catena vocale.
// Attenzione: non basta che il server risponda. Per il CERVELLO serve anche un
// modello caricato, altrimenti la domanda fallirebbe e si finirebbe in cloud
// dopo aver perso tempo. L'esito resta in cache per qualche secondo, quindi si
// puo' chiedere a ogni domanda senza pagare ogni volta.
bool localReachable(LocalSvc svc);

// Solo "il server risponde", senza chiedersi se e' pronto a lavorare. Serve al
// pannello per distinguere "spento" da "acceso ma senza modello caricato".
bool localConnected(LocalSvc svc);

// Ultimo esito NOTO di localReachable, senza provare niente e senza toccare la
// rete: e' quello che guarda il display (gira sul core 0, non puo' aspettare).
bool localOn(LocalSvc svc);

// Riprova UN servizio, a turno, se e' passato abbastanza tempo. Va chiamata dal
// loop quando Alexo e' a riposo: serve a tenere onesti i pallini del display
// anche se non stai facendo domande. Torna true se ha davvero provato (chi
// chiama ne approfitta per buttare l'audio letto nel frattempo).
bool localRefreshTick();

// Nome del modello da usare per questo servizio. Se nel pannello e' scritto,
// torna quello; se e' VUOTO chiede al server quale ha caricato adesso (GET
// <base>/models, il primo della lista) - cosi' si cambia modello dal PC senza
// toccare il pannello. Torna "" se non riesce a saperlo.
String localModelName(LocalSvc svc);

// Indirizzo base del servizio (dal pannello), "" se spento.
String localBaseUrl(LocalSvc svc);

// Dimentica gli esiti in cache (raggiungibilita' e nome modello): la prossima
// prova richiede tutto da capo. Serve al pulsante "Prova" del pannello e dopo
// aver cambiato gli indirizzi.
void localForget();

// --- Dirlo in faccia --------------------------------------------------------
//  Il ripiego sul cloud e' comodo ma silenzioso: con le spie verdi si crede di
//  essere in casa mentre voce, domanda o risposta sono appena uscite su internet.
//  Queste due lo scrivono in ROSSO nella chat (TFT e pannello web).

// "Questo pezzo e' appena uscito su internet". Scrive SOLO se il servizio in casa
// e' configurato: a chi lavora in cloud di proposito non serve dirlo ogni volta.
void localSayCloud(LocalSvc svc);

// "Solo casa acceso e il servizio di casa non c'e': mi fermo qui". Scrive sempre.
void localSayBlocked(LocalSvc svc);

// Toglie il "pensiero" che parecchi modelli locali stampano prima della risposta
// vera (blocco <think>...</think>). Va fatto comunque, anche chiedendo di non
// ragionare: non tutti i modelli ubbidiscono.
String localStripThink(const String &s);
