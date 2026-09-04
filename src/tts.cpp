// ============================================================================
//  ALEXO - Text-to-Speech -> streaming MP3 sul VS1053
//  L'MP3 ricevuto viene dato a pezzetti al VS1053 man mano che arriva (bassa
//  latenza, niente buffer enorme). Due strade:
//    CLOUD  ElevenLabs (voce clonata)
//    CASA   server compatibile OpenAI sulla LAN (vedi localai.h), senza chiave
//  Si va in casa solo se il pannello ha un indirizzo E il PC risponde; se il
//  server locale sbaglia, si ripiega su ElevenLabs. In entrambi i casi serve
//  MP3: il VS1053 non decodifica altro.
// ============================================================================
#include "tts.h"
#include "secrets.h"
#include "gobbo.h"
#include "volume.h"
#include "settings.h"
#include "localai.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mic.h"
#include "ui.h"
#include <math.h>

// Livello del ring DURANTE la voce: legge il MIC (come la musica) e ne ricava un
// livello VU. MENO sensibile della musica (VOICE_LVL_DIV grande) perche' la voce e'
// a volume alto. Passa-alto + MAD + noise-floor auto + envelope; cala nei silenzi.
#define VOICE_LVL_DIV   30.0f
#define VOICE_FLOOR     90.0f
static uint8_t voiceLevel(const int16_t *s, size_t n) {
  if (!n) return 0;
  static double hpX = 0, hpY = 0; const double hpR = 0.95;
  float acc = 0;
  for (size_t i = 0; i < n; i++) {
    double v = (double)s[i];
    double y = v - hpX + hpR * hpY; hpX = v; hpY = y;
    acc += fabsf((float)y);
  }
  float mad = acc / (float)n;
  static float nf = -1.0f;
  if (nf < 0.0f) nf = mad;
  float k = (mad < nf) ? 0.05f : 0.0005f;
  nf += (mad - nf) * k;
  float thresh = nf + VOICE_FLOOR;
  float target = (mad - thresh) / VOICE_LVL_DIV;
  if (target < 0) target = 0; else if (target > 255) target = 255;
  static float env = 0;
  float ek = (target > env) ? 0.7f : 0.55f;  // attack veloce, release piu' svelto (reattivo)
  env += (target - env) * ek;
  return (uint8_t)(env + 0.5f);
}

// La voce di DEFAULT e' ora RUNTIME (gSettings.voiceId, dal pannello web); il
// default di fabbrica sta in config.h (ELEVEN_VOICE_DEF).
#define ELEVEN_MODEL    "eleven_flash_v2_5"       // veloce, 0,5 crediti/carattere
#define TTS_MS_PER_CHAR 65        // stima durata parlato: ms per carattere
                                  // (per sync gobbo<->voce; ritocca se serve)

// Normalizza il testo per la PRONUNCIA (solo per la TTS, NON per il display:
// a video restano "36°C" e "19,5"). Espande i simboli che ElevenLabs legge male
// nella loro forma parlata italiana. Per aggiungere casi: una nuova condizione
// nel loop. Il testo in ingresso e' UTF-8 (es. "°" = 0xC2 0xB0).
// Prova a leggere un ORARIO "H:MM" o "HH:MM" a partire da in[i]. Se combacia,
// accoda a 'out' la forma parlata e ritorna quanti caratteri ha consumato;
// altrimenti ritorna 0 (e 'out' non viene toccato). Regole:
//   00:00 -> "mezzanotte"    12:00 -> "mezzogiorno"
//   HH:00 -> "HH in punto"   HH:MM -> "HH e MM"   (ora 0 -> "zero")
// Vincoli: ore 0-23, minuti a 2 cifre 0-59; non deve seguire un'altra cifra o ':'
// (cosi' non trasforma numeri lunghi o orari con i secondi HH:MM:SS).
static int leggiOrario(const String &in, int i, int n, String &out) {
  int j = i, oreDigits = 0;
  while (j < n && isDigit((uint8_t)in[j]) && oreDigits < 2) { j++; oreDigits++; }
  if (oreDigits < 1) return 0;
  int colon = j;
  if (colon >= n || in[colon] != ':') return 0;
  if (colon + 2 >= n) return 0;
  if (!isDigit((uint8_t)in[colon + 1]) || !isDigit((uint8_t)in[colon + 2])) return 0;
  int after = colon + 3;
  if (after < n && (isDigit((uint8_t)in[after]) || in[after] == ':')) return 0;

  int H = in.substring(i, colon).toInt();
  int M = ((uint8_t)in[colon + 1] - '0') * 10 + ((uint8_t)in[colon + 2] - '0');
  if (H > 23 || M > 59) return 0;

  if      (H == 0  && M == 0) out += "mezzanotte";
  else if (H == 12 && M == 0) out += "mezzogiorno";
  else if (M == 0)            { out += String(H); out += " in punto"; }   // HH:00
  else if (H == 0)            { out += "zero ";   out += String(M); }     // 00:MM -> "zero MM"
  else                        { out += String(H); out += " e "; out += String(M); }  // HH:MM
  return after - i;   // caratteri consumati (le ore + ':' + i 2 minuti)
}

// Prova a leggere una DATA a partire da in[i]. Se combacia accoda a 'out' la
// forma parlata "G <mese> AAAA" e ritorna i caratteri consumati; altrimenti 0.
// Il mese diventa una parola, giorno e anno restano cifre: i motori vocali li
// leggono bene da soli, e cosi' non serve saper scrivere i numeri a lettere.
//   10/3/2026 - 01.12.1992 - 12-09-1989  ->  "10 marzo 2026" ecc.
//   2026-08-16 (all'incontrario, come la scrivono le macchine) -> "16 agosto 2026"
// Il separatore puo' essere / . - ma dev'essere lo STESSO due volte: "1.500/3"
// non e' una data.
// PERCHE': senza questa, la regola della frazione qui sotto leggeva le date con
// la barra come "10 fratto 3 fratto 2026", e quelle col punto o col trattino
// venivano lette alla lettera.
// Vincoli STRETTI perche' non morda le frazioni vere ("3/4") ne' le migliaia
// ("1.500.000"): giorno 1-31, mese 1-12, anno di ESATTAMENTE 4 cifre.
static const char *MESI_VOCE[] = { "gennaio", "febbraio", "marzo", "aprile",
                                   "maggio", "giugno", "luglio", "agosto",
                                   "settembre", "ottobre", "novembre", "dicembre" };
// Legge fino a 'max' cifre da j; torna quante ne ha lette (0 = nessuna) e sposta j.
static int cifreDa(const String &in, int n, int &j, int max) {
  int c = 0;
  while (j < n && isDigit((uint8_t)in[j]) && c < max) { j++; c++; }
  return c;
}

static int leggiData(const String &in, int i, int n, String &out) {
  int j = i;
  int p1 = i, c1 = cifreDa(in, n, j, 4);          // giorno, oppure anno se sono 4
  if (c1 < 1 || j >= n) return 0;
  const char sep = in[j];
  if (sep != '/' && sep != '.' && sep != '-') return 0;

  int p2 = ++j, c2 = cifreDa(in, n, j, 3);        // mese: sempre 1-2 cifre
  if (c2 < 1 || c2 > 2 || j >= n || in[j] != sep) return 0;

  int p3 = ++j, c3 = cifreDa(in, n, j, 5);
  if (j < n && (in[j] == sep || in[j] == ':')) return 0;   // c'e' un altro pezzo: non e' una data

  String sG, sA;
  if (c1 <= 2 && c3 == 4)        { sG = in.substring(p1, p1 + c1); sA = in.substring(p3, p3 + c3); }
  else if (c1 == 4 && c3 <= 2)   { sA = in.substring(p1, p1 + c1); sG = in.substring(p3, p3 + c3); }
  else return 0;

  int G = sG.toInt(), M = in.substring(p2, p2 + c2).toInt();
  if (G < 1 || G > 31 || M < 1 || M > 12) return 0;

  out += String(G); out += ' '; out += MESI_VOCE[M - 1]; out += ' '; out += sA;
  return j - i;
}

// Prova a leggere un numero col PUNTO DELLE MIGLIAIA a partire da in[i]
// ("230.000"). Se combacia accoda a 'out' lo stesso numero senza punti
// ("230000") e ritorna i caratteri consumati; altrimenti 0.
// PERCHE': ElevenLabs i numeri se li normalizza da solo, i motori in casa no -
// Kokoro legge il punto alla lettera ("duecentotrenta punto zerozerozero").
// Regola: 1-3 cifre, poi uno o piu' gruppi di ESATTAMENTE 3 cifre preceduti dal
// punto, e dopo l'ultimo gruppo niente cifre ne' altri punti. Cosi' restano fuori
// il decimale all'inglese ("3.14", il gruppo non e' di 3 cifre) e gli indirizzi
// di rete ("192.168.1.50", dopo l'ultimo gruppo c'e' ancora un punto).
static int leggiMigliaia(const String &in, int i, int n, String &out) {
  int j = i, cifre = 0;
  while (j < n && isDigit((uint8_t)in[j]) && cifre < 4) { j++; cifre++; }
  if (cifre < 1 || cifre > 3) return 0;

  int gruppi = 0;
  while (j + 3 < n && in[j] == '.' &&
         isDigit((uint8_t)in[j + 1]) && isDigit((uint8_t)in[j + 2]) &&
         isDigit((uint8_t)in[j + 3])) {
    if (j + 4 < n && isDigit((uint8_t)in[j + 4])) return 0;   // gruppo di 4+: non e' un separatore
    j += 4; gruppi++;
  }
  if (gruppi == 0) return 0;
  if (j < n && (isDigit((uint8_t)in[j]) || in[j] == '.')) return 0;

  for (int k = i; k < j; k++) if (in[k] != '.') out += in[k];
  return j - i;
}

// ---------------------------------------------------------------------------
//  ORDINALI: "85esima" -> "ottantacinquesima"
//  Le voci (tutte e due: ElevenLabs e il server di casa) leggono la cifra da
//  sola e poi il suffisso attaccato, e ne esce "ottocinquesima". L'unico modo
//  di sistemarlo e' scrivere l'ordinale per esteso, quindi serve saper mettere
//  un numero in lettere. Si copre 1..999999: oltre, meglio lasciare com'e'.
// ---------------------------------------------------------------------------
static const char *ORD_U[]  = { "", "uno", "due", "tre", "quattro", "cinque",
                                "sei", "sette", "otto", "nove" };
static const char *ORD_T[]  = { "dieci", "undici", "dodici", "tredici",
                                "quattordici", "quindici", "sedici",
                                "diciassette", "diciotto", "diciannove" };
static const char *ORD_D[]  = { "", "", "venti", "trenta", "quaranta",
                                "cinquanta", "sessanta", "settanta", "ottanta",
                                "novanta" };
// I primi dieci ordinali non seguono nessuna regola: vanno a memoria.
static const char *ORD_IRR[] = { "", "prim", "second", "terz", "quart", "quint",
                                 "sest", "settim", "ottav", "non", "decim" };

static String cardinaleSotto100(int v) {
  if (v < 10)  return String(ORD_U[v]);
  if (v < 20)  return String(ORD_T[v - 10]);
  String s = ORD_D[v / 10];
  int u = v % 10;
  if (u == 1 || u == 8) s.remove(s.length() - 1);   // venti+uno -> "ventuno"
  s += ORD_U[u];
  return s;
}

static String cardinaleSotto1000(int v) {
  if (v < 100) return cardinaleSotto100(v);
  int c = v / 100, r = v % 100;
  String s = (c == 1) ? String("") : String(ORD_U[c]);
  s += "cento";
  if (r == 0) return s;
  String rr = cardinaleSotto100(r);
  char p = rr[0];
  if (p == 'o' || p == 'u') s.remove(s.length() - 1);   // "centotto", "centuno"
  s += rr;
  return s;
}

static String cardinaleParola(long v) {
  if (v == 0) return String("zero");
  if (v < 1000) return cardinaleSotto1000((int)v);
  int m = (int)(v / 1000), r = (int)(v % 1000);
  String s = (m == 1) ? String("mille") : (cardinaleSotto1000(m) + "mila");
  if (r) s += cardinaleSotto1000(r);
  return s;
}

// L'ordinale si forma dal cardinale: via l'ultima vocale, poi "-esim-" e la
// desinenza (o/a/i/e). Tre eccezioni che sembrano pignoleria e non lo sono:
//   ...tre -> ventitreESIMO (la "e" resta)   ...sei -> ventiseiESIMO (resta la "i")
//   ...mila -> duemilLESIMO (non "duemilaesimo")
static String ordinaleParola(long v, char genere) {
  String base;
  if (v >= 1 && v <= 10) {
    base = ORD_IRR[v];
  } else {
    String c = cardinaleParola(v);
    if (c.endsWith("mila")) {
      c.remove(c.length() - 4);
      base = c + "millesim";
    } else if (c.endsWith("tre") || c.endsWith("sei")) {
      base = c + "esim";
    } else {
      char u = c[c.length() - 1];
      if (u == 'a' || u == 'e' || u == 'i' || u == 'o' || u == 'u')
        c.remove(c.length() - 1);
      base = c + "esim";
    }
  }
  base += genere;
  return base;
}

// Parole che dopo un "N°" dicono che NON e' un ordinale ma i gradi: un ordinale
// e' seguito da un nome ("21° secolo"), i gradi da una preposizione, da un
// avverbio o da niente ("fa 30° all'ombra", "ci sono 30°.").
static const char *DOPO_GRADI[] = {
  "a","ad","al","all","alla","alle","allo","ai","agli","con","da","dal","dall",
  "dalla","di","dei","del","dell","della","e","ed","in","nel","nell","nella",
  "o","od","per","su","sul","sull","sulla","tra","fra","ma","che","non","circa",
  "sotto","sopra","oggi","domani","ieri","stanotte","stamattina","stasera",
  "ora","adesso","ancora","verso","fuori","dentro","qui","qua","il","lo","la",
  "i","gli","le","un","uno","una","gradi","meno","piu","quando","mentre","se"
};

// Prova a leggere un ORDINALE che parte da in[i]:
//   "85esima" (anche esimo/esimi/esime)   "3ª" / "21º" (indicatori ordinali)
//   "21° secolo"  -> ordinale;  "36°C" / "30° all'ombra" -> NON e' un ordinale
// Ritorna i caratteri consumati, 0 se non combacia (e allora 'out' non si tocca).
static int leggiOrdinale(const String &in, int i, int n, String &out) {
  int j = i, cifre = 0;
  while (j < n && isDigit((uint8_t)in[j]) && cifre < 7) { j++; cifre++; }
  if (cifre < 1) return 0;
  long v = in.substring(i, j).toInt();
  if (v < 1 || v > 999999) return 0;

  char genere = 0;
  int  consumed = 0;

  // suffisso scritto per esteso: "esimo" / "esima" / "esimi" / "esime"
  if (j + 5 <= n) {
    String s = in.substring(j, j + 5);
    s.toLowerCase();
    if (s == "esimo" || s == "esima" || s == "esimi" || s == "esime") {
      if (!(j + 5 < n && isAlphaNumeric((uint8_t)in[j + 5]))) {
        genere = s[4];
        consumed = (j + 5) - i;
      }
    }
  }

  // indicatori ordinali veri: "ª" (0xC2 0xAA) e "º" (0xC2 0xBA). Da non
  // confondere col grado "°" (0xC2 0xB0), che qui sotto ha la sua regola.
  if (!consumed && j + 1 < n && (uint8_t)in[j] == 0xC2 &&
      ((uint8_t)in[j + 1] == 0xAA || (uint8_t)in[j + 1] == 0xBA)) {
    genere = ((uint8_t)in[j + 1] == 0xAA) ? 'a' : 'o';
    consumed = (j + 2) - i;
  }

  // "N°": gradi oppure ordinale? Decide la parola dopo (vedi DOPO_GRADI).
  if (!consumed && j + 1 < n && (uint8_t)in[j] == 0xC2 &&
      (uint8_t)in[j + 1] == 0xB0) {
    int k = j + 2;
    while (k < n && in[k] == ' ') k++;
    int p = k;
    while (p < n && isAlpha((uint8_t)in[p]) && (uint8_t)in[p] < 0x80) p++;
    if (p == k) return 0;                       // niente parola dopo: gradi
    if (p < n && (uint8_t)in[p] >= 0x80) return 0;   // parola accentata: gradi
    String w = in.substring(k, p);
    w.toLowerCase();
    if (w == "c" || w == "f") return 0;         // "36 °C"
    for (unsigned d = 0; d < sizeof(DOPO_GRADI) / sizeof(DOPO_GRADI[0]); d++)
      if (w == DOPO_GRADI[d]) return 0;
    genere = 'o';
    consumed = (j + 2) - i;
  }

  if (!consumed) return 0;
  out += ordinaleParola(v, genere);
  return consumed;
}

// Abbreviazioni di unita' di misura: "km" -> "chilometri". Senza, le voci le
// leggono a lettere ("kappa emme") o all'inglese. Le piu' LUNGHE per prime:
// "km/h" va cercata prima di "km", altrimenti resta "chilometri fratto acca"
// (stessa cosa per "m2" rispetto a "m", "cm" rispetto a "c...").
// PER AGGIUNGERNE UNA: una riga qui, singolare e plurale. Il confronto ignora
// maiuscole e minuscole (i modelli scrivono "km" o "KM" indifferentemente).
// L'ultimo campo e' 'serveNum': se true l'abbreviazione vale solo quando ha un
// NUMERO davanti. Serve alle sigle di una lettera sola, che altrimenti fanno
// disastri ("l'acqua" diventerebbe "litriacqua", "3 a 4" -> "3 ampere 4").
struct UnitaVoce { const char *abbr; const char *sing; const char *plur; bool serveNum; };
static const UnitaVoce UNITA[] = {
  { "km/h", "chilometro orario",   "chilometri orari",    false },
  { "km\xC2\xB2", "chilometro quadrato", "chilometri quadrati", false },
  { "kcal", "chilocaloria",        "chilocalorie",        false },
  { "kwh",  "chilowattora",        "chilowattora",        false },
  { "khz",  "chilohertz",          "chilohertz",          false },
  { "mhz",  "megahertz",           "megahertz",           false },
  { "ghz",  "gigahertz",           "gigahertz",           false },
  { "min",  "minuto",              "minuti",              true  },
  { "cm\xC2\xB2", "centimetro quadrato", "centimetri quadrati", false },
  { "m\xC2\xB2",  "metro quadrato", "metri quadrati",      false },
  { "m\xC2\xB3",  "metro cubo",     "metri cubi",          false },
  { "mq",   "metro quadrato",      "metri quadrati",      false },
  { "km",   "chilometro",          "chilometri",          false },
  { "kg",   "chilogrammo",         "chilogrammi",         false },
  { "kw",   "chilowatt",           "chilowatt",           false },
  { "kb",   "chilobyte",           "chilobyte",           false },
  { "mb",   "megabyte",            "megabyte",            false },
  { "gb",   "gigabyte",            "gigabyte",            false },
  { "tb",   "terabyte",            "terabyte",            false },
  { "cm",   "centimetro",          "centimetri",          false },
  { "mm",   "millimetro",          "millimetri",          false },
  { "ml",   "millilitro",          "millilitri",          false },
  { "mg",   "milligrammo",         "milligrammi",         false },
  { "hz",   "hertz",               "hertz",               false },
  { "h",    "ora",                 "ore",                 true  },
  { "m",    "metro",               "metri",               true  },
  { "l",    "litro",               "litri",               true  },
  { "g",    "grammo",              "grammi",              true  },
  { "s",    "secondo",             "secondi",             true  },
};

// Se in[i] apre una di quelle abbreviazioni COME PAROLA A SE' (non "kmart", non
// "okm"), scrive la forma parlata e ritorna quanti caratteri consumare.
// Singolare solo se prima c'e' esattamente "1" o "un/uno/una": "21 km" in
// italiano e' plurale, quindi non basta guardare l'ultima cifra.
// Il punto NON viene consumato: "230 km." di solito e' fine frase, e togliere il
// punto toglierebbe la pausa. La 'm' resta attaccata solo se e' parte di parola.
static int leggiUnita(const String &in, int i, int n, String &out) {
  if (i > 0 && isAlphaNumeric((uint8_t)in[i - 1])) return 0;   // "okm"
  for (unsigned u = 0; u < sizeof(UNITA) / sizeof(UNITA[0]); u++) {
    int L = (int)strlen(UNITA[u].abbr);
    if (i + L > n) continue;
    bool uguale = true;
    for (int k = 0; k < L && uguale; k++) {
      uint8_t a = (uint8_t)in[i + k], b = (uint8_t)UNITA[u].abbr[k];
      if (a >= 'A' && a <= 'Z') a += 32;        // minuscolo solo sull'ASCII:
      if (a != b) uguale = false;               // "\xC2\xB2" va confrontato grezzo
    }
    if (!uguale) continue;
    if (i + L < n && isAlphaNumeric((uint8_t)in[i + L])) continue;   // "kmart"

    // Cosa c'e' prima: salta gli spazi, poi prendi la parola/numero attaccato.
    int j = i - 1;
    while (j >= 0 && in[j] == ' ') j--;
    int fine = j;
    while (j >= 0 && isAlphaNumeric((uint8_t)in[j])) j--;
    String prima = in.substring(j + 1, fine + 1);
    prima.toLowerCase();
    bool numPrima = prima.length() && isDigit((uint8_t)prima[prima.length() - 1]);
    if (UNITA[u].serveNum && !numPrima) continue;
    bool sing = (prima == "1" || prima == "un" || prima == "uno" || prima == "una");
    out += sing ? UNITA[u].sing : UNITA[u].plur;
    return L;
  }
  return 0;
}

static String normalizzaPerVoce(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  int n = in.length();
  for (int i = 0; i < n; i++) {
    uint8_t c = (uint8_t)in[i];

    // orario "HH:MM" -> forma parlata. Solo all'INIZIO di un numero (il carattere
    // prima non e' una cifra), cosi' non spezza numeri piu' lunghi.
    if (isDigit(c) && (i == 0 || !isDigit((uint8_t)in[i - 1]))) {
      int consumed = leggiOrario(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
      // "85esima" -> "ottantacinquesima", "3a" (indicatore ordinale) -> "terza",
      // "21 grado secolo" -> "ventunesimo secolo". Se non e' un ordinale torna 0
      // e il grado se lo prende la regola piu' sotto.
      consumed = leggiOrdinale(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
      // stesso punto di partenza: la data PRIMA della frazione qui sotto, che
      // altrimenti se la mangia ("10 fratto 3 fratto 2026").
      consumed = leggiData(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
      // "230.000" -> "230000" (vedi leggiMigliaia)
      consumed = leggiMigliaia(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
    }

    // Unita' di misura abbreviate: "20 km/h" -> "20 chilometri orari". Prima
    // dello strip del markdown: qui non c'e' markdown di mezzo, e la 'k' non e'
    // un carattere che quel filtro tocca.
    if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) &&
        (i == 0 || !isAlphaNumeric((uint8_t)in[i - 1]))) {
      int consumed = leggiUnita(in, i, n, out);
      if (consumed > 0) { i += consumed - 1; continue; }
    }

    // "3x4" -> "3 per 4" (solo tra due cifre: "x" da sola resta com'e')
    if ((c == 'x' || c == 'X') && i > 0 && i + 1 < n &&
        isDigit((uint8_t)in[i - 1]) && isDigit((uint8_t)in[i + 1])) {
      out += " per ";
      continue;
    }

    // Markdown: i modelli lo usano anche quando il prompt dice di non farlo (i
    // locali soprattutto). A voce "**Emilia-Romagna**" diventa un balbettio o
    // una lettura degli asterischi, quindi qui i segni si tolgono. A VIDEO
    // restano: questa normalizzazione vale solo per il parlato.
    if (c == '*' || c == '`' || c == '_' || c == '#') continue;

    // grado "°" (UTF-8 0xC2 0xB0), eventualmente seguito da C/F.
    // Gli ORDINALI ("21° secolo") sono gia' stati presi da leggiOrdinale, qui
    // sopra: se si arriva fin qui sono gradi davvero (o un "n°" = numero).
    if (c == 0xC2 && i + 1 < n && (uint8_t)in[i + 1] == 0xB0) {
      // "n° 5" -> "numero 5": la 'n' e' gia' finita in 'out', si toglie.
      if (i > 0 && (in[i - 1] == 'n' || in[i - 1] == 'N') &&
          (i < 2 || !isAlphaNumeric((uint8_t)in[i - 2]))) {
        int k = i + 2;
        while (k < n && in[k] == ' ') k++;
        if (k < n && isDigit((uint8_t)in[k])) {
          out.remove(out.length() - 1);
          out += "numero";
          i += 1;
          continue;
        }
      }
      char next = (i + 2 < n) ? in[i + 2] : 0;
      if      (next == 'C' || next == 'c') { out += " gradi centigradi"; i += 2; }
      else if (next == 'F' || next == 'f') { out += " gradi Fahrenheit"; i += 2; }
      else                                 { out += " gradi";            i += 1; }
      continue;
    }

    // virgola decimale tra cifre: "19,5" -> "19 virgola 5"
    if (c == ',' && i > 0 && i + 1 < n &&
        isDigit((uint8_t)in[i - 1]) && isDigit((uint8_t)in[i + 1])) {
      out += " virgola ";
      continue;
    }

    // percentuale: "100%" -> "100 per cento"
    if (c == '%') { out += " per cento"; continue; }

    // frazione tra cifre: "10/3" -> "10 fratto 3"
    // NB: tocca solo cifra/cifra; attenzione alle date "10/3/2026" -> diventano
    // "10 fratto 3 fratto 2026" (Claude in genere scrive "10 marzo", raro).
    if (c == '/' && i > 0 && i + 1 < n &&
        isDigit((uint8_t)in[i - 1]) && isDigit((uint8_t)in[i + 1])) {
      out += " fratto ";
      continue;
    }

    // valute: "€" (0xE2 0x82 0xAC), "$", "£" (0xC2 0xA3)
    if (c == 0xE2 && i + 2 < n && (uint8_t)in[i + 1] == 0x82 &&
        (uint8_t)in[i + 2] == 0xAC) { out += " euro"; i += 2; continue; }
    if (c == 0xC2 && i + 1 < n && (uint8_t)in[i + 1] == 0xA3) {
      out += " sterline"; i += 1; continue;
    }
    if (c == '$') { out += " dollari"; continue; }

    // numero negativo: "-5 gradi" -> "meno 5 gradi". Solo a inizio parola, cosi'
    // gli intervalli ("18-20") e le parole col trattino non si toccano.
    if (c == '-' && (i == 0 || in[i - 1] == ' ' || in[i - 1] == '(') &&
        i + 1 < n && isDigit((uint8_t)in[i + 1])) {
      out += "meno ";
      continue;
    }

    out += (char)c;
  }
  return out;
}

// Prende l'MP3 dalla risposta gia' aperta e lo dà a pezzetti al VS1053 man mano
// che arriva. Identico sulle due strade (cloud e casa): quello che cambia e' solo
// come si e' chiesto l'audio. Chiude da se' la connessione.
static bool ttsStream(HTTPClient &http, VS1053 &player, const String &parlato,
                      uint32_t bytePerMs) {
  uint32_t t0 = millis();
  int len = http.getSize();             // -1 se sconosciuto (chunked)
  WiFiClient *stream = http.getStreamPtr();
  player.setVolume(volumeVsValue());   // volume utente rimappato nella zona udibile

  // L'audio sta per partire: lega lo scroll del gobbo alla DURATA dell'audio.
  // Quanti byte valgono un millisecondo dipende dal formato, e lo dice chi
  // chiama: MP3 a 128 kbps = 16, WAV 24 kHz 16 bit mono = 48. Se arriva il
  // Content-Length (len>0) la durata e' esatta; se la risposta e' chunked
  // (len<0) ricado sulla stima da lunghezza testo.
  uint32_t durMs = (len > 0) ? (uint32_t)len / bytePerMs
                             : (uint32_t)parlato.length() * TTS_MS_PER_CHAR;
  gobboScrollOver(durMs);
  Serial.printf("[tts] durata audio: %lu ms (%s)\n", (unsigned long)durMs,
                len > 0 ? "esatta da Content-Length" : "stimata da testo");

  uint8_t buf[512];
  size_t total = 0;
  uint32_t idle = millis();
  uint32_t lastLvl = 0;
  while (http.connected() || (stream && stream->available())) {
    volumeApplyPending(player);   // regola il volume al volo MENTRE Alexo parla
    // Ring REATTIVO alla voce: ogni ~30ms leggo il mic e pilotO il livello (ciano),
    // come la musica ma meno sensibile. Throttlato per non affamare il feed VS1053.
    if (millis() - lastLvl >= 30) {
      lastLvl = millis();
      static int16_t vbuf[160];
      micFlush();
      size_t g = micReadChunk(vbuf, 160);
      if (g) uiSetLevel(voiceLevel(vbuf, g));
    }
    int avail = stream->available();
    if (avail > 0) {
      int toRead = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
      int c = stream->readBytes(buf, toRead);
      if (c > 0) {
        player.playChunk(buf, c);
        total += c;
        idle = millis();
        if (len > 0) { len -= c; if (len == 0) break; }
      }
    } else {
      if (millis() - idle > 2000) break;   // stream finito/timeout
      delay(1);
    }
  }
  http.end();

  // svuota il decoder con un po' di silenzio
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 4; i++) player.playChunk(buf, sizeof(buf));
  uiSetLevel(0);   // fine voce: ring giu'

  Serial.printf("[tts] riprodotti %u byte in %lu ms\n",
                (unsigned)total, (unsigned long)(millis() - t0));
  return total > 0;
}

// Voce IN CASA: server compatibile OpenAI (/audio/speech) sulla LAN, senza
// chiave. Si chiede WAV, non MP3: il VS1053 lo decodifica nativamente (i bip di
// sound.cpp sono gia' WAV) e cosi' il server non deve comprimere niente - che
// significa meno attesa e nessun ffmpeg da installare sul PC. Costa piu' banda,
// ~380 kbit/s contro 128, che sulla WiFi di casa non si sente.
// Torna false se non ce l'ha fatta: chi chiama ripiega su ElevenLabs.
#define WAV_BYTE_PER_MS 48    // 24000 campioni/s x 2 byte = 48 byte per ms
#define MP3_BYTE_PER_MS 16    // 128 kbps CBR = 16 byte per ms
static bool speakLocal(VS1053 &player, const String &parlato) {
  // Indirizzo "risolto": se nel pannello manca la porta, e' quella che ha
  // risposto (8002 Kokoro / 8003 Chatterbox). Con la porta scritta e' identico
  // a quello del pannello e non costa nessuna attesa.
  const String base  = localBaseUsed(LOC_TTS);
  if (base.isEmpty()) return false;
  String model = localModelName(LOC_TTS);
  if (model.isEmpty()) model = "tts-1";       // la gran parte dei server lo ignora

  JsonDocument req;
  req["model"]           = model;
  req["input"]           = parlato;
  req["response_format"] = "wav";
  // Voce vuota = lascia scegliere al server la sua predefinita.
  if (gSettings.localTtsVoice.length()) req["voice"] = gSettings.localTtsVoice;
  String body;
  serializeJson(req, body);

  WiFiClient client;
  client.setTimeout(30000);

  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, base + "/audio/speech");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/wav");

  Serial.printf("[tts] sintesi in casa di %u caratteri (%s)...\n",
                (unsigned)parlato.length(), model.c_str());
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[tts] errore locale HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }
  return ttsStream(http, player, parlato, WAV_BYTE_PER_MS);
}

// Dove va la voce: la decide un INTERRUTTORE, non il fatto che il server di casa
// sia acceso. Un server raggiungibile ma non richiesto non deve dirottare niente:
// senza flag si va su ElevenLabs anche se il PC e' vivo. Serve anche a main.cpp
// per mettere "[LOC]" davanti alla risposta a video.
bool ttsUsesLocal() {
  if (localBaseUrl(LOC_TTS).isEmpty()) return false;
  return gSettings.localOnly || gSettings.ttsLocalOnly;
}

bool ttsSpeak(VS1053 &player, const String &text, const String &voiceId) {
  if (text.isEmpty()) return false;

  // Testo "parlato": uguale a quello a video ma coi simboli espansi (vedi sopra).
  String parlato = normalizzaPerVoce(text);

  // Voce in casa: solo se l'ha chiesto un interruttore ("voce sempre in casa"
  // oppure "solo casa"). Niente controllo di raggiungibilita' prima (attesa in
  // meno) e niente ripiego su ElevenLabs: e' il punto dell'interruttore, non
  // consumare i crediti gratuiti alle spalle di chi l'ha acceso.
  if (ttsUsesLocal()) {
    if (speakLocal(player, parlato)) return true;
    Serial.println("[tts] voce in casa non riuscita: non ripiego su ElevenLabs");
    localSayBlocked(LOC_TTS);
    return false;
  }

  // "Solo casa" senza indirizzo di casa: il testo non esce. Resta a video.
  if (gSettings.localOnly) { localSayBlocked(LOC_TTS); return false; }

  localSayCloud(LOC_TTS);

  // Voce: usa quella passata, altrimenti la default (dal pannello web).
  String voce = voiceId.isEmpty() ? gSettings.voiceId : voiceId;

  // Corpo JSON
  JsonDocument req;
  req["text"]     = parlato;
  req["model_id"] = ELEVEN_MODEL;
  String body;
  serializeJson(req, body);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setTimeout(25000);
  String url = String("https://api.elevenlabs.io/v1/text-to-speech/")
             + voce + "?output_format=mp3_44100_128";
  http.begin(client, url);
  http.addHeader("xi-api-key", ELEVENLABS_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/mpeg");

  Serial.printf("[tts] sintesi di %u caratteri (%s)...\n",
                (unsigned)parlato.length(), ELEVEN_MODEL);
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[tts] errore HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }

  return ttsStream(http, player, parlato, MP3_BYTE_PER_MS);
}
