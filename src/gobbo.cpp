// ============================================================================
//  ALEXO - Teleprompter und blätterbarer Chat auf dem farbigen TFT-Display
//  ST7735. Siehe gobbo.h. Der Modulname "gobbo" bleibt, damit ein Abgleich mit
//  dem Originalprojekt möglich bleibt.
//
//  - Das TFT zeigt den Chat ("Du: ..." / "Alexo: ...") als Verlauf im PSRAM,
//    mehrere tausend Zeilen. Die Aufgabe läuft auf KERN 0 und ist die einzige,
//    die den SPI-Bus des TFT (HSPI) anfasst; der Text kommt von Kern 1 über eine
//    Warteschlange von FreeRTOS.
//  - Gegen das Flimmern (das TFT hat keinen Bildspeicher wie ein OLED) wird
//    alles auf eine Fläche mit 16 Bit im RAM gezeichnet und dann in EINEM Zug
//    als ganzes Bild übertragen.
//  - Farben nach Rolle: "Du:" gelb, "Alexo:" weiss, Systemmeldungen rot.
//  - Zwei Betriebsarten:
//      AUTOMATISCH: folgt der Stimme (die gerade gelesene Zeile bleibt bei
//               READ_ANCHOR) oder steht am Ende, wenn nicht gesprochen wird.
//      VON HAND: der DREHGEBER blättert im Verlauf nach oben und unten. Man
//               kommt durch Drehen hinein, die Taste führt zurück zur
//               automatischen Betriebsart.
//    Der Drehgeber wird HIER in der Aufgabe gelesen (Kern 0) und arbeitet
//    deshalb auch, während Kern 1 in ttsSpeak festhängt.
// ============================================================================
#include "gobbo.h"
#include "encoder.h"
#include "volume.h"
#include "localai.h"
#include "config.h"
#include <Adafruit_GFX.h>

static Adafruit_ST7735 *D  = nullptr;
static GFXcanvas16     *CV = nullptr;   // Bildspeicher im RAM, danach aufs TFT übertragen

#define COLS         21      // Zeichen je Zeile (128 px / 6 px je Zeichen)
#define LINEH         8      // Zeilenhöhe in Pixel (Schriftgrösse 1)
#define VIEWW   TFT_WIDTH    // 128
#define VIEWH   TFT_HEIGHT   // 160
#define HEADER_H     14      // feste Leiste oben: Titel und Zustand
#define CHATH   (VIEWH - HEADER_H)        // blätterbarer Bereich des Chats (Pixel)
#define MAXLINES   2000      // Zeilen des Chatverlaufs (im PSRAM)
#define READ_ANCHOR  ((CHATH * 3) / 5)    // die gerade gelesene Zeile bleibt bei etwa 3/5 des Bereichs
#define MS_PER_LINE_DEFAULT 700          // lesbares Tempo ohne Stimme (ms je Zeile)

// --- Zustand für die Kopfleiste (von jedem Kern geschrieben, von der Aufgabe
// gelesen) -------------------------------------------------------------------
static volatile uint8_t g_state = ST_IDLE;   // ein Byte: Lesen und Schreiben sind unteilbar
static volatile uint8_t g_otaPct = 0;        // Fortschritt der Aktualisierung über Funk in Prozent
static volatile bool    g_otaData = false;   // true = Dateisystem (DATA), false = Firmware (FW)
// Anforderungen vom Drehgeber (Kern 0) an die Verarbeitung (Kern 1): der Klick
// zum Starten und der zum Beenden der Aufnahme. Ausgerichtete bool-Werte, der
// Zugriff ist auf dem Xtensa also unteilbar.
static volatile bool g_talkReq = false;
static volatile bool g_stopReq = false;
static volatile int32_t g_musSeek = 0;   // Rastungen während der Musik -> Senderwechsel
// Beschriftung und Farbe für jeden AlexoState (der Index ist der Wert der
// Aufzählung in ui.h).
static const char *ST_LABEL[] = { "bereit","höre","denke","spreche","Fehler","OTA","Musik",
                                  "du dran" };
static const uint16_t ST_COL[] = { ST77XX_BLUE, ST77XX_GREEN, ST77XX_YELLOW,
                                   ST77XX_CYAN, ST77XX_RED, ST77XX_GREEN, ST77XX_MAGENTA,
                                   ST77XX_ORANGE };

// Die Rolle einer Zeile bestimmt ihre Farbe. Der Verlauf bricht Sätze auf
// mehrere Zeilen um, deshalb trägt jede Zeile ihre Rolle mit sich; die erste
// beginnt mit "Du:" oder "Alexo:".
enum { ROLE_ALEXO = 0, ROLE_USER = 1, ROLE_SYS = 2, ROLE_WARN = 3 };
// ROLE_SYS ist GELB: auf diesem ST7735 lässt sich Rot nicht lesen, auch in einer
// hellen Fassung (255,80,80) bleibt es unleserlich. Deshalb ist ROLE_WARN, der
// Hinweis "es ist etwas ins Internet gegangen", hier GRÜN. Wichtig ist, dass man
// ihn lesen kann, den Sinn trägt der Text. Im Web-Panel bleibt dieselbe Zeile
// rot, dort ist sie gut zu sehen.
static const uint16_t ROLE_COLOR[4] = { ST77XX_WHITE, ST77XX_YELLOW, ST77XX_YELLOW,
                                        ST77XX_GREEN };

// Eine Antwort, die mit "[" beginnt, ist eine Systemmeldung (Fehler,
// Aktualisierung über Funk). Ausgenommen ist "[LOC]", das nur kennzeichnet, WER
// die Stimme erzeugt hat. Der Rest der Zeile ist eine gewöhnliche Antwort von
// Alexo und bekommt deren Farbe.
static bool isSysMsg(const char *t) {
  return t[0] == '[' && strncmp(t, "[LOC]", 5) != 0;
}

// Ringpuffer der Zeilen im PSRAM (Text und Rolle parallel geführt)
static char    *buf      = nullptr;   // MAXLINES mal (COLS+1)
static uint8_t *rolebuf  = nullptr;   // MAXLINES
static int      startIdx = 0;         // Index der ältesten Zeile
static int      nLines   = 0;         // gültige Zeilen
static uint8_t  curRole  = ROLE_ALEXO;// Rolle, die die laufenden pushLine setzen

// --- Schlanke Kopie des Chats für das WEB-PANEL -----------------------------
//  Die letzten WEBCHAT_MAX Nachrichten VOLLSTÄNDIG in UTF-8, nicht die auf 21
//  Spalten umgebrochenen Bruchstücke in CP437 vom TFT. Gefüllt wird sie von
//  gobboPrint und gobboPrintUser, die wie webuiHandle auf KERN 1 laufen, deshalb
//  braucht es keine Sperre. g_chatRev springt bei jeder Nachricht weiter, damit
//  das Panel den Chat NUR bei einer neuen nachlädt.
#define WEBCHAT_MAX 40
#define WEBCHAT_LEN 2048          // wie GobboMsg.text: dieselbe Grösse wie beim TFT, nichts wird abgeschnitten
struct WebMsg { uint8_t role; char text[WEBCHAT_LEN]; };
static WebMsg  *webChat  = nullptr;
static int      webStart = 0, webCount = 0;
static volatile uint32_t g_chatRev = 0;

static void webChatPush(uint8_t role, const char *utf8) {
  if (!webChat) return;
  int idx = (webStart + webCount) % WEBCHAT_MAX;
  if (webCount < WEBCHAT_MAX) webCount++;
  else                        webStart = (webStart + 1) % WEBCHAT_MAX;
  webChat[idx].role = role;
  strncpy(webChat[idx].text, utf8, WEBCHAT_LEN - 1);
  webChat[idx].text[WEBCHAT_LEN - 1] = 0;
  g_chatRev++;
}

static float    posY  = 0;         // aktueller senkrechter Versatz (Pixel)
static uint32_t voiceStart = 0;    // Zeitpunkt, an dem der automatische Lauf begann
static uint32_t voiceDur   = 0;    // Dauer des automatischen Laufs (0 = ans Ende springen)
static int      respStart  = 0;    // erste Zeile der laufenden Antwort
static int      respLines  = 0;    // Anzahl Zeilen der laufenden Antwort

enum { MODE_AUTO, MODE_MANUAL };
static int mode = MODE_AUTO;

static QueueHandle_t q = nullptr;

// Eine Nachricht in der Warteschlange.
struct GobboMsg {
  uint8_t  kind;        // 0=Alexo  1=leeren  2=scrollOver  3=Nutzer
  uint32_t ms;          // bei kind==2: Gesamtdauer des Laufs
  char     text[2048];  // bei kind 0 und 3
};

// --- UTF-8 -> CP437 ---------------------------------------------------------
// Der Text kommt in UTF-8 an (etwa "è" = 0xC3 0xA8), die Schrift des Displays
// arbeitet aber mit CP437 (ein Byte je Zeichen). Umgewandelt werden die
// Umlaute, die italienischen Akzente und die typografischen Satzzeichen, die
// Claude verwendet; alles Übrige wird zu '?'.
static uint8_t cpFromUnicode(uint32_t u) {
  if (u < 0x80) return (uint8_t)u;
  switch (u) {
    case 0x00E0: return 0x85;  // à
    case 0x00E1: return 0xA0;  // á
    case 0x00E8: return 0x8A;  // è
    case 0x00E9: return 0x82;  // é
    case 0x00EC: return 0x8D;  // ì
    case 0x00ED: return 0xA1;  // í
    case 0x00F2: return 0x95;  // ò
    case 0x00F3: return 0xA2;  // ó
    case 0x00F9: return 0x97;  // ù
    case 0x00FA: return 0xA3;  // ú
    case 0x00E7: return 0x87;  // ç
    case 0x00F1: return 0xA4;  // ñ
    // Umlaute und Eszett: CP437 kennt diese Glyphen, die Tabelle bisher
    // nicht - ohne die folgenden Zeilen wird auf dem Display aus jedem
    // deutschen Sonderzeichen ein '?'. Werte gegen den cp437-Codec
    // geprueft, siehe tools/test_cp437.py.
    case 0x00E4: return 0x84;  // ä
    case 0x00F6: return 0x94;  // ö
    case 0x00FC: return 0x81;  // ü
    case 0x00C4: return 0x8E;  // Ä
    case 0x00D6: return 0x99;  // Ö
    case 0x00DC: return 0x9A;  // Ü
    case 0x00DF: return 0xE1;  // ß
    case 0x00C9: return 0x90;  // É
    case 0x00B0: return 0xF8;  // °
    // grosse Akzentbuchstaben, die CP437 nicht kennt -> der Grundbuchstabe
    case 0x00C0: case 0x00C1: return 'A';   // À Á
    case 0x00C8: case 0x00CA: return 'E';   // È Ê
    case 0x00CC: case 0x00CD: return 'I';   // Ì Í
    case 0x00D2: case 0x00D3: return 'O';   // Ò Ó
    case 0x00D9: case 0x00DA: return 'U';   // Ù Ú
    // typografische Satzzeichen
    case 0x2018: case 0x2019: return '\'';  // ' '
    case 0x201C: case 0x201D: return '"';   // " "
    case 0x2013: case 0x2014: return '-';   // - -
    case 0x2026: return '.';                // ...
    default: return '?';
  }
}

// Wandelt die UTF-8-Zeichenkette AN ORT UND STELLE nach CP437 um. Das Ergebnis
// ist nie länger als die Eingabe, das ist also sicher.
static void utf8ToCp437(char *s) {
  uint8_t *r = (uint8_t *)s, *w = (uint8_t *)s;
  while (*r) {
    uint8_t c = *r; uint32_t u;
    if (c < 0x80)                                  { u = c; r += 1; }
    else if ((c & 0xE0) == 0xC0 && r[1])           { u = ((c & 0x1F) << 6) | (r[1] & 0x3F); r += 2; }
    else if ((c & 0xF0) == 0xE0 && r[1] && r[2])   { u = ((uint32_t)(c & 0x0F) << 12) | ((r[1] & 0x3F) << 6) | (r[2] & 0x3F); r += 3; }
    else if ((c & 0xF8) == 0xF0 && r[1] && r[2] && r[3]) { u = 0xFFFD; r += 4; }
    else                                           { u = c; r += 1; }
    *w++ = cpFromUnicode(u);
  }
  *w = 0;
}

// --- Zeilenpuffer (nur in der Aufgabe auf Kern 0) ---------------------------
static inline char    *slot(int i)     { return buf + ((startIdx + i) % MAXLINES) * (COLS + 1); }
static inline uint8_t &roleAt(int i)    { return rolebuf[(startIdx + i) % MAXLINES]; }

static void pushLine(const char *s) {
  if (nLines < MAXLINES) {
    strncpy(slot(nLines), s, COLS); slot(nLines)[COLS] = 0;
    roleAt(nLines) = curRole;
    nLines++;
  } else {
    // voll: überschreibt die älteste und rückt vor, die neue wird zur letzten
    strncpy(slot(0), s, COLS); slot(0)[COLS] = 0;
    roleAt(0) = curRole;
    startIdx = (startIdx + 1) % MAXLINES;
    if (posY >= LINEH) posY -= LINEH;
    if (respStart > 0)  respStart--;
  }
}

// Bricht 'text' an Wortgrenzen auf Zeilen von COLS Zeichen um und hängt sie an.
static void addText(const char *text) {
  char word[COLS + 1]; int wl = 0;
  char line[COLS + 1]; int ll = 0;
  line[0] = 0;

  auto flushWord = [&]() {
    if (wl == 0) return;
    word[wl] = 0;
    if (ll == 0)                         { strcpy(line, word); ll = wl; }
    else if (ll + 1 + wl <= COLS)        { line[ll++] = ' '; memcpy(line + ll, word, wl); ll += wl; line[ll] = 0; }
    else                                 { pushLine(line); strcpy(line, word); ll = wl; line[ll] = 0; }
    wl = 0;
  };

  for (const char *p = text; *p; p++) {
    char c = *p;
    if (c == '\n')      { flushWord(); pushLine(line); line[0] = 0; ll = 0; }
    else if (c == ' ')  { flushWord(); }
    else if (wl < COLS) { word[wl++] = c; }     // Wörter länger als COLS werden abgeschnitten
  }
  flushWord();
  if (ll > 0) pushLine(line);
}

// --- Zeichnen (nur in der Aufgabe) ------------------------------------------
//  Gezeichnet wird auf die Fläche im RAM, danach geht EIN Bild aufs Display, so
//  flimmert nichts. Um Rechenzeit und Bus zu sparen, wird nur neu gezeichnet,
//  wenn sich etwas geändert hat, also posY oder der Inhalt.
// Feste Leiste oben: links "ALEXO", rechts die farbige Zustandsbeschriftung,
// darunter eine Trennlinie in der Farbe des Zustands. Sie wird NACH dem Chat
// gezeichnet und deckt damit Zeilen ab, die unter die Leiste ragen.
//  Zwischen "ALEXO" und der Beschriftung stehen drei Punkte in der Reihenfolge
//  der Sprachkette: Spracherkennung, Gehirn, Stimme. GRÜN heisst, dieses Glied
//  läuft auf dem PC zu Hause, ROT heisst, es geht in die Cloud, weil es im Panel
//  aus ist, weil der PC nicht antwortet oder, allein bei der Stimme, weil kein
//  Schalter sie nach Hause geschickt hat. Dort genügt ein laufender Server
//  nicht, siehe localOn. Angezeigt wird der ZULETZT BEKANNTE Stand: wir sind hier
//  auf Kern 0, der nicht auf das Netz warten kann, darum kümmert sich der Loop,
//  siehe localRefreshTick.
#define DOT_X0     46      // Mitte des ersten Punktes
#define DOT_STEP   12      // Abstand zwischen den Mittelpunkten
#define DOT_R       3
static void drawDots() {
  for (int i = 0; i < LOC_COUNT; i++)
    CV->fillCircle(DOT_X0 + i * DOT_STEP, 6, DOT_R,
                   localOn((LocalSvc)i) ? ST77XX_GREEN : ST77XX_RED);
}

static void drawHeader() {
  uint8_t s = g_state; if (s > ST_LAST) s = ST_IDLE;
  CV->fillRect(0, 0, VIEWW, HEADER_H, ST77XX_BLACK);
  CV->setTextColor(ST77XX_WHITE);
  CV->setCursor(2, 3);
  CV->print("ALEXO");
  drawDots();
  const char *lbl = ST_LABEL[s];
  int x = VIEWW - (int)strlen(lbl) * 6 - 2;   // 6 px je Zeichen (Schriftgrösse 1)
  CV->setTextColor(ST_COL[s]);
  CV->setCursor(x, 3);
  CV->print(lbl);
  CV->drawFastHLine(0, HEADER_H - 1, VIEWW, ST_COL[s]);
}

static void render() {
  if (!CV) return;
  CV->fillScreen(ST77XX_BLACK);
  CV->setTextSize(1);   // die Radioanzeige nutzt Grösse 2, Chat und Leiste hier Grösse 1
  int sy = (int)(posY + 0.5f);
  for (int i = 0; i < nLines; i++) {
    int y = HEADER_H + i * LINEH - sy;       // der Chat liegt unter der Leiste
    if (y >= VIEWH) break;
    if (y <= HEADER_H - LINEH) continue;     // über der Leiste: sie deckt es ohnehin ab
    CV->setTextColor(ROLE_COLOR[roleAt(i)]);
    CV->setCursor(0, y);
    CV->print(slot(i));
  }
  drawHeader();
  D->drawRGBBitmap(0, 0, CV->getBuffer(), VIEWW, VIEWH);
}

// --- Eigene Anzeige für die Aktualisierung über Funk (grün) -----------------
//  Im selben Stil wie das Startbild, nur grün und mit dem Prozentwert GROSS in
//  der Mitte. Gezeichnet auf die Fläche im RAM und in einem Zug übertragen, so
//  flimmert nichts. Aufgerufen wird sie NUR von der Aufgabe des Teleprompters,
//  der einzigen Besitzerin des TFT-Busses, solange g_state == ST_OTA gilt.
static void renderOtaScreen(uint8_t pct) {
  if (!CV) return;
  const int W = VIEWW, H = VIEWH;                 // 128 x 160
  const uint16_t BLK  = ST77XX_BLACK;
  const uint16_t GRN  = D->color565(0, 255, 90); // kräftiges Grün (wie der Ring dabei)
  const uint16_t GRNd = D->color565(0, 70, 32);  // schwaches Grün (das Raster)
  const uint16_t GRNm = D->color565(0, 210, 95); // mittleres Grün (heller)

  CV->fillScreen(BLK);

  // schwaches Raster im Hintergrund
  for (int y = 0; y < H; y += 8) CV->drawFastHLine(0, y, W, GRNd);
  for (int x = 0; x <= W; x += 16) CV->drawFastVLine(x, 0, H, GRNd);

  // Eckwinkel im Stil einer Anzeigetafel
  const int b = 12;
  CV->drawFastHLine(2, 2, b, GRN);            CV->drawFastVLine(2, 2, b, GRN);
  CV->drawFastHLine(W - 2 - b, 2, b, GRN);    CV->drawFastVLine(W - 3, 2, b, GRN);
  CV->drawFastHLine(2, H - 3, b, GRN);        CV->drawFastVLine(2, H - 3 - b, b, GRN);
  CV->drawFastHLine(W - 2 - b, H - 3, b, GRN);CV->drawFastVLine(W - 3, H - 3 - b, b, GRN);

  // Titel auf einer Zeile, kräftig grün und mittig
  CV->setTextColor(GRN); CV->setTextSize(2);
  // Art der Aktualisierung: Firmware ("FW") oder Dateisystem und Webseite
  // ("DATA"). Dieselbe Schrift; die Mitte wird über strlen berechnet, es bleibt
  // also mittig, egal wie der Text lautet.
  const char *sub = g_otaData ? "DATA" : "FW";
  CV->setCursor((W - (4+(int)strlen(sub)) * 12) / 2, 15); CV->print("OTA "); CV->print(sub);

  // Prozentwert GROSS in der Mitte (Schriftgrösse 4 = 24 px je Ziffer)
  if (pct > 100) pct = 100;
  char num[8]; snprintf(num, sizeof(num), "%u%%", (unsigned)pct);
  CV->setTextSize(4);
  int nw = (int)strlen(num) * 24;
  CV->setTextColor(GRN);
  CV->setCursor((W - nw) / 2, 58); CV->print(num);

  // Fortschrittsbalken (höher gesetzt, der Text darunter lief sonst aus dem Bild)
  const int bx = 12, by = 104, bw = W - 24, bh = 14;
  CV->drawRect(bx, by, bw, bh, GRNm);
  int fill = (int)((long)(bw - 4) * pct / 100);
  if (fill < 0) fill = 0; else if (fill > bw - 4) fill = bw - 4;
  CV->fillRect(bx + 2, by + 2, fill, bh - 4, GRN);

  // Warnung, kräftig grün und mit Abstand zum Rand
  CV->setTextColor(GRN); CV->setTextSize(1);
  const char *warn = "NICHT AUSSCHALTEN";
  CV->setCursor((W - (int)strlen(warn) * 6) / 2, by + 24); CV->print(warn);

  D->drawRGBBitmap(0, 0, CV->getBuffer(), VIEWW, VIEWH);
}

// --- Anzeige "RADIO" (laufender Titel) --------------------------------------
//  Ruhiger Hintergrund, oben "RADIO", darunter drei Zeilen in Grösse 2 mit
//  Abstand: Sender, Titel, Interpret. Ist eine Zeile breiter als das Display,
//  läuft sie von rechts nach links durch. Die Zeichenketten, bereits in CP437,
//  setzt die Aufgabe aus einer Nachricht mit kind==4. np_changeMs wird bei jedem
//  Titelwechsel zurückgesetzt, damit der Durchlauf neu beginnt und die Anzeige
//  aufgeräumt wirkt. Gezeichnet wird nur in der Aufgabe, der einzigen am
//  TFT-Bus.
static char     np_station[96] = "";
static char     np_title[96]   = "";
static char     np_artist[96]  = "";
static uint32_t np_changeMs    = 0;

// Geschwindigkeit der Laufschrift in Pixeln je Sekunde, abstimmbar. Sie richtet
// sich danach, WIE WEIT der Text über das Display hinausragt, nicht nach seiner
// absoluten Länge. Ragt er nur wenig hinaus, läuft er mit NP_SPEED_MIN und damit
// sehr langsam; je weiter er hinausragt, desto schneller wird er (NP_SPEED_K
// Pixel je Sekunde für jedes überstehende Pixel), höchstens aber bis
// NP_SPEED_MAX, damit er lesbar bleibt.
#define NP_SPEED_MIN  10.0f    // Pixel je Sekunde, wenn der Text kaum übersteht
#define NP_SPEED_K     0.14f   // Pixel je Sekunde mehr für jedes überstehende Pixel
#define NP_SPEED_MAX  45.0f    // höchste Geschwindigkeit, darüber ist nichts mehr zu lesen

// Eine Zeile in Schriftgrösse 2: mittig, wenn sie hineinpasst, sonst als
// Laufschrift mit einer zweiten Kopie nach einer Lücke, damit der Umlauf
// nahtlos wirkt. Die Geschwindigkeit rechnet die Funktion aus dem Text aus.
static void drawNpLine(int y, const char *s, uint16_t col) {
  CV->setTextSize(2);
  CV->setTextColor(col);
  int textW = (int)strlen(s) * 12;              // 6 px mal Schriftgrösse 2
  if (textW == 0) return;
  if (textW <= VIEWW - 4) {
    CV->setCursor((VIEWW - textW) / 2, y); CV->print(s);   // passt: mittig und stehend
    return;
  }
  // Laufschrift. Die Geschwindigkeit wächst mit dem Überstand (textW minus
  // Displaybreite): fast kein Überstand ergibt NP_SPEED_MIN und damit langsamen
  // Lauf, viel Überstand macht ihn schneller, bis zur Obergrenze.
  const int GAP = 34;
  int loopW = textW + GAP;
  float overflow = (float)(textW - VIEWW);
  if (overflow < 0) overflow = 0;
  float pxPerSec = NP_SPEED_MIN + overflow * NP_SPEED_K;
  if (pxPerSec > NP_SPEED_MAX) pxPerSec = NP_SPEED_MAX;
  int ox = (int)((float)(millis() - np_changeMs) * pxPerSec / 1000.0f) % loopW;
  CV->setCursor(2 - ox, y);           CV->print(s);
  CV->setCursor(2 - ox + loopW, y);   CV->print(s);        // zweite Kopie: Umlauf ohne Lücke
}

static void renderNowPlaying() {
  if (!CV) return;
  const uint16_t CY = D->color565(0, 229, 255);
  const uint16_t CYd = D->color565(0, 70, 85);
  const uint16_t WH = ST77XX_WHITE;
  const uint16_t YE = ST77XX_YELLOW;
  CV->fillScreen(ST77XX_BLACK);

  // Kopfzeile "RADIO"
  CV->setTextSize(3); CV->setTextColor(CY);
  const char *h = "RADIO";
  CV->setCursor((VIEWW - (int)strlen(h) * 18) / 2, 4); CV->print(h);
  CV->drawFastHLine(0, 26, VIEWW, CYd);

  drawNpLine(54,  np_station, YE);   // 1) Sender (die Laufgeschwindigkeit rechnet die Funktion aus)
  drawNpLine(94,  np_title,   WH);   // 2) Titel
  drawNpLine(124, np_artist,  WH);   // 3) Interpret

  /*
  CV->setTextSize(1); CV->setTextColor(CY);
  CV->setCursor(1,40); CV->print((int)strlen(np_station));
  CV->setCursor(45,40); CV->print((int)strlen(np_title));
  CV->setCursor(90,40); CV->print((int)strlen(np_artist));
  */

  D->drawRGBBitmap(0, 0, CV->getBuffer(), VIEWW, VIEWH);
}

// --- Aufgabe (Kern 0) -------------------------------------------------------
static void gobboTask(void *) {
  const uint32_t FRAME_MS = 30;
  char tmp[2048 + 8];
  float   lastPosY  = -1.0f;
  int     lastN     = -1;
  uint8_t lastState = 0xFF;
  uint8_t lastDots  = 0xFF;   // die Punkte für Cloud und zu Hause lösen ebenfalls ein Neuzeichnen aus
  // Stromsparen am Display: die Hintergrundbeleuchtung geht nach
  // DISPLAY_SLEEP_MS ohne Bedienung aus und beim ersten Eingriff wieder an.
  uint32_t lastActivity = millis();
  bool     blOn = true;          // beim Start an (siehe setup() in main.cpp)
  for (;;) {
    // Während einer Aktualisierung über Funk: die eigene grüne Anzeige mit dem
    // Prozentwert in der Mitte. Chat und Drehgeber ruhen, die
    // Hintergrundbeleuchtung bleibt an. Kern 1 setzt nur g_otaPct über
    // gobboOtaProgress(), gezeichnet wird hier.
    if (g_state == (uint8_t)ST_OTA) {
      if (!blOn) { digitalWrite(TFT_BL_PIN, HIGH); blOn = true; }
      renderOtaScreen(g_otaPct);
      lastActivity = millis();
      lastState = 0xFF;          // erzwingt ein vollständiges Neuzeichnen danach
      vTaskDelay(pdMS_TO_TICKS(80));
      continue;
    }

    bool activity = false;       // gab es in diesem Bild irgendeine Bedienung?

    // 1) Nachrichten in der Warteschlange (neue Frage, Antwort oder Zustand
    //    zählen als Bedienung)
    GobboMsg *m;
    while (q && xQueueReceive(q, &m, 0) == pdTRUE) {
      activity = true;
      if (m->kind == 1) {                         // leeren
        nLines = 0; startIdx = 0; posY = 0;
        respStart = 0; respLines = 0; voiceDur = 0; mode = MODE_AUTO;
      } else if (m->kind == 2) {                  // scrollOver: Takt der Stimme
        voiceStart = millis(); voiceDur = m->ms; mode = MODE_AUTO;
      } else if (m->kind == 4) {                  // laufender Titel: "Sender\nTitel\nInterpret"
        char *s1 = m->text;
        char *s2 = strchr(s1, '\n'); if (s2) *s2++ = 0; else s2 = s1 + strlen(s1);
        char *s3 = strchr(s2, '\n'); if (s3) *s3++ = 0; else s3 = s2 + strlen(s2);
        strncpy(np_station, s1, sizeof(np_station) - 1); np_station[sizeof(np_station) - 1] = 0;
        strncpy(np_title,   s2, sizeof(np_title)   - 1); np_title[sizeof(np_title)   - 1] = 0;
        strncpy(np_artist,  s3, sizeof(np_artist)  - 1); np_artist[sizeof(np_artist)  - 1] = 0;
        utf8ToCp437(np_station); utf8ToCp437(np_title); utf8ToCp437(np_artist);
        np_changeMs = millis();                   // Titelwechsel: der Durchlauf beginnt neu, die Anzeige wird frei
      } else if (m->kind == 5) {                  // roter Hinweis (ohne "Alexo:")
        curRole = ROLE_WARN;
        snprintf(tmp, sizeof(tmp), "%s", m->text);
        utf8ToCp437(tmp);
        addText(tmp); pushLine("");
        voiceDur = 0; mode = MODE_AUTO;           // ans Ende springen
      } else if (m->kind == 3) {                  // Frage des Nutzers
        curRole = ROLE_USER;
        snprintf(tmp, sizeof(tmp), "Du: %s", m->text);
        utf8ToCp437(tmp);
        addText(tmp); pushLine("");
        voiceDur = 0; mode = MODE_AUTO;           // ans Ende springen
      } else {                                    // Antwort von Alexo
        respStart = nLines;
        // "[...]" ist eine Systemmeldung (Fehler, Aktualisierung) und wird rot,
        // alles andere ist eine Antwort von Alexo.
        curRole = isSysMsg(m->text) ? ROLE_SYS : ROLE_ALEXO;
        snprintf(tmp, sizeof(tmp), "Alexo: %s", m->text);
        utf8ToCp437(tmp);
        addText(tmp); pushLine("");
        respLines  = nLines - respStart;
        voiceStart = millis();
        voiceDur   = (uint32_t)respLines * MS_PER_LINE_DEFAULT;  // wird später überschrieben
        mode = MODE_AUTO;
      }
      free(m);
    }

    // 2) Der Drehgeber ist die einzige Eingabe. Drei Gesten:
    //    - freies Drehen        -> von Hand im Chat blättern
    //    - GEDRÜCKT und gedreht -> Lautstärke (im Uhrzeigersinn lauter)
    //    - kurzer Klick (drücken und loslassen, ohne zu drehen):
    //          bei Ruhe         -> STARTET den Chat und nimmt auf
    //          während der Aufnahme -> BEENDET sie und schickt ab
    //  Hier wird keine Ton-Hardware angefasst, VS1053 und Mikrofon hängen an
    //  Kern 1. Gesammelt werden nur Anforderungen (volumeRequest, talk, stop),
    //  die Kern 1 ausführt. Ein eigenes "zurück ans Ende" braucht es nicht: jede
    //  neue Nachricht schaltet ohnehin zurück in die automatische Betriebsart.
    // Entprellen der Taste, um Störungen auszufiltern. Bei offenem Anschluss
    // (Drehgeber noch nicht verdrahtet) erzeugen Einstreuungen "Geisterklicks",
    // daraus werden Aufnahmen von Stille, und darauf halluziniert Whisper.
    // Verlangt werden deshalb drei übereinstimmende Bilder (etwa 90 ms), bevor
    // ein Wechsel des Tastenzustands gilt.
    static uint8_t dbCount = 0;
    static bool    dbHeld  = false;
    bool rawHeld = encoderButtonHeld();
    if (rawHeld != dbHeld) { if (++dbCount >= 3) { dbHeld = rawHeld; dbCount = 0; } }
    else                     dbCount = 0;
    bool    held = dbHeld;
    int32_t d    = encoderTake();

    // Während der MUSIK regelt jedes Drehen, gedrückt oder nicht, die
    // LAUTSTÄRKE, wie am Knopf eines Radios. Der Senderwechsel liegt NICHT mehr
    // auf gedrückt und gedreht, das war unbequem, sondern auf dem einfachen
    // KLICK (siehe unten). Ausserhalb der Musik gilt: gedrückt und gedreht ist
    // die Lautstärke, freies Drehen blättert.
    if (g_state == (uint8_t)ST_MUSIC) {
      if (d != 0) volumeRequest(d);                          // in der Musik: jedes Drehen regelt die Lautstärke
    } else if (held) {
      if (d != 0) volumeRequest(d);                          // gedrückt und gedreht: Lautstärke
    } else if (d != 0) {
      mode = MODE_MANUAL; posY += (float)d * LINEH;          // freies Drehen: blättern
    }
    // Bestätigter EINFACHER Klick (vom Doppelklick bereits unterschieden, und
    // ausgeschlossen, wenn dabei gedreht wurde):
    //   in der MUSIK   -> der NÄCHSTE Sender, im Kreis
    //   beim Zuhören   -> beendet die Aufnahme
    //   sonst          -> startet den Chat
    // Im Zustand "du dran" (fortlaufender Chat, es wird auf die nächste Frage
    // gewartet) zählt der Klick wie beim Zuhören und beendet die Aufnahme. Da
    // keine Stimme zu hören war, schliesst Kern 1 das Gespräch, der Klick ist
    // also der sofortige Ausweg. Ohne diesen Zweig landete er im else und würde
    // einen gerade geschlossenen Chat erneut starten.
    if (encoderButtonPressed()) {
      if      (g_state == (uint8_t)ST_MUSIC)     g_musSeek += 1;   // Klick: nächster Sender
      else if (g_state == (uint8_t)ST_LISTENING ||
               g_state == (uint8_t)ST_FOLLOWUP)  g_stopReq = true; // beendet die Aufnahme
      else                                       g_talkReq = true; // startet den Chat
    }
    // Ein DOPPELKLICK in der MUSIK verlässt das Radio und kehrt zum Chat zurück;
    // musicPlay liest gobboTakeTalkRequest als Halt. Ausserhalb der Musik
    // übernimmt main.cpp den Doppelklick (er schaltet das Reagieren des Rings um),
    // dort ist Kern 1 nicht blockiert.
    if (g_state == (uint8_t)ST_MUSIC && encoderDoublePressed()) {
      g_talkReq = true;
    }

    // Jede Benutzung des Drehgebers zählt als Bedienung, und ebenso jeder
    // Zustand, in dem Alexo beschäftigt ist. So bleibt das Display an.
    if (d != 0 || held) activity = true;
    if (g_state != (uint8_t)ST_IDLE) activity = true;

    // 3) MUSIK: die eigene Radioanzeige mit Laufschrift, die ständig neu
    //    gezeichnet wird. Der Drehgeber oben bleibt aktiv und regelt die
    //    Lautstärke. Sonst wird der Chat gezeichnet.
    if (g_state == (uint8_t)ST_MUSIC) {
      renderNowPlaying();
      lastState = 0xFF;   // erzwingt ein vollständiges Neuzeichnen des Chats danach
    } else {
      // 3b) Position berechnen (der blätterbare Bereich ist CHATH, unter der Leiste)
      float maxScroll = (nLines * LINEH > CHATH) ? (float)(nLines * LINEH - CHATH) : 0.0f;
      if (mode == MODE_MANUAL) {
        if (posY < 0) posY = 0; else if (posY > maxScroll) posY = maxScroll;
      } else {  // AUTO
        if (voiceDur > 0 && maxScroll > 0) {
          float f = (float)(millis() - voiceStart) / (float)voiceDur;
          if (f < 0) f = 0; else if (f > 1) f = 1;
          float readPx = (float)(respStart * LINEH) + f * (respLines * LINEH);
          float tgt = readPx - READ_ANCHOR;
          if (tgt < 0) tgt = 0; else if (tgt > maxScroll) tgt = maxScroll;
          posY = tgt;
        } else {
          posY = maxScroll;   // am Ende bleiben
        }
      }

      // 4) Nur neu zeichnen, wenn es nötig ist, sonst würde bei stehendem Bild
      //    unnötig übertragen.
      //    Zu den Dingen, die sich ändern, gehören auch die Punkte für Cloud und
      //    zu Hause in der Leiste: ohne sie zu beobachten, sähe man einen Dienst,
      //    der bei stehendem Bild nach Hause wechselt, nie. Der Punkt behielte
      //    die Farbe der letzten Übertragung.
      uint8_t dots = 0;
      for (int i = 0; i < LOC_COUNT; i++)
        if (localOn((LocalSvc)i)) dots |= (1 << i);
      if (posY != lastPosY || nLines != lastN || g_state != lastState || dots != lastDots) {
        render();
        lastPosY = posY; lastN = nLines; lastState = g_state; lastDots = dots;
      }
    }

    // 5) Hintergrundbeleuchtung: beim ersten Eingriff an, nach der Ruhezeit aus.
    //    (digitalWrite geht auf einen GPIO und nicht auf den SPI-Bus, es gibt
    //    also keinen Streit um den Bus.)
#if DISPLAY_SLEEP_MS > 0
    if (activity) {
      lastActivity = millis();
      if (!blOn) { digitalWrite(TFT_BL_PIN, HIGH); blOn = true; }
    } else if (blOn && (millis() - lastActivity) > DISPLAY_SLEEP_MS) {
      digitalWrite(TFT_BL_PIN, LOW); blOn = false;
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
  }
}

// --- API --------------------------------------------------------------------
void gobboBegin(Adafruit_ST7735 *disp) {
  D = disp;
  buf     = (char *)ps_malloc((size_t)MAXLINES * (COLS + 1));
  if (!buf) buf = (char *)malloc((size_t)MAXLINES * (COLS + 1));  // Rückfall
  rolebuf = (uint8_t *)ps_malloc(MAXLINES);
  if (!rolebuf) rolebuf = (uint8_t *)malloc(MAXLINES);
  webChat = (WebMsg *)ps_malloc(sizeof(WebMsg) * WEBCHAT_MAX);   // Kopie des Chats fürs Panel
  if (!webChat) webChat = (WebMsg *)malloc(sizeof(WebMsg) * WEBCHAT_MAX);

  // Zeichenfläche im RAM (128 mal 160 mal 2 = 40 KB internes SRAM): GFXcanvas16
  // reserviert mit malloc. 40 KB hat der S3 bequem übrig; der Chatverlauf liegt
  // dagegen im PSRAM.
  CV = new GFXcanvas16(VIEWW, VIEWH);
  if (CV) {
    CV->setTextSize(1);
    CV->setTextWrap(false);
    CV->cp437(true);              // richtige Zuordnung der Sonderzeichen (CP437)
    CV->fillScreen(ST77XX_BLACK);
  }
  D->fillScreen(ST77XX_BLACK);

  q = xQueueCreate(8, sizeof(GobboMsg *));
  xTaskCreatePinnedToCore(gobboTask, "gobbo", 6144, nullptr, 1, nullptr, 0);
}

static void send(uint8_t kind, const String &text, uint32_t ms) {
  if (!q) return;
  GobboMsg *m = (GobboMsg *)malloc(sizeof(GobboMsg));
  if (!m) return;
  m->kind = kind; m->ms = ms;
  strncpy(m->text, text.c_str(), sizeof(m->text) - 1);
  m->text[sizeof(m->text) - 1] = 0;
  if (xQueueSend(q, &m, 0) != pdTRUE) free(m);
}

void gobboSetState(AlexoState s)        { g_state = (uint8_t)s; }

// Klick zum Starten: liefert EINMAL true, wenn eine Anforderung vorliegt, und
// setzt sie zurück.
bool gobboTakeTalkRequest() { bool r = g_talkReq; g_talkReq = false; return r; }
// Klick zum Beenden während der Aufnahme (von der Abbruchbedingung in micRecord
// gelesen).
bool gobboStopRequested()   { return g_stopReq; }
// Setzt den offenen Abbruchwunsch zurück: Kern 1 ruft das unmittelbar vor der
// Aufnahme auf.
void gobboClearStopRequest(){ g_stopReq = false; }
int32_t gobboTakeMusicSeek() { int32_t d = g_musSeek; g_musSeek -= d; return d; }
void gobboPrint(const String &text)     { if (!text.isEmpty()) { webChatPush(isSysMsg(text.c_str()) ? ROLE_SYS : ROLE_ALEXO, text.c_str()); send(0, text, 0); } }
void gobboPrintUser(const String &text) { if (!text.isEmpty()) { webChatPush(ROLE_USER, text.c_str()); send(3, text, 0); } }
void gobboPrintWarn(const String &text) { if (!text.isEmpty()) { webChatPush(ROLE_WARN, text.c_str()); send(5, text, 0); } }

// Zähler für Änderungen am Chat: springt bei jeder neuen Nachricht weiter. Das
// Panel liest ihn in /api/live und lädt /api/chat nur nach, wenn er sich
// geändert hat.
uint32_t gobboChatRev() { return g_chatRev; }

// Zugriffe, um den Chat STÜCKWEISE auszuliefern: das Panel schickt eine
// Nachricht nach der anderen, ohne im RAM eine riesige Zeichenkette zu bauen. Der
// Zeiger gilt für die Dauer der Anfrage; webui und gobboPrint laufen beide auf
// Kern 1 und kommen sich deshalb nicht in die Quere.
int gobboChatCount() { return webCount; }
bool gobboChatItem(int i, uint8_t *role, const char **text) {
  if (i < 0 || i >= webCount) return false;
  int idx = (webStart + i) % WEBCHAT_MAX;
  *role = webChat[idx].role;
  *text = webChat[idx].text;
  return true;
}
void gobboScrollOver(uint32_t ms)       { send(2, "", ms); }
void gobboClear()                       { send(1, "", 0); }
void gobboOtaProgress(uint8_t percent)  { g_otaPct = percent > 100 ? 100 : percent; }
void gobboOtaKind(bool isData)          { g_otaData = isData; }
void gobboNowPlaying(const char *station, const char *title, const char *artist) {
  // Packt die drei Zeilen mit '\n' getrennt zusammen; die Aufgabe teilt sie
  // wieder auf, siehe kind==4
  String p = String(station ? station : "") + "\n" +
             String(title   ? title   : "") + "\n" +
             String(artist  ? artist  : "");
  send(4, p, 0);
}
