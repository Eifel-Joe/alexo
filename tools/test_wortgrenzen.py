#!/usr/bin/env python3
"""Prueft die Wortgrenzen der Absichtserkennung.

Auf Italienisch genuegte es, Schluesselwoerter als Teilzeichenkette zu suchen.
Deutsch bildet Zusammensetzungen und haengt Endungen an, deshalb traf "musik" in
"Musiker", "lied" in "Mitglied" und das Ausloesewort "gut" in "guten Morgen".
Dieser Test haelt beides fest:

1. STRUKTUR. Der Quelltext muss die Hilfsfunktionen mit Wortgrenzen benutzen und
   nicht wieder auf indexOf oder startsWith zurueckfallen.
2. VERHALTEN. Dieselben Regeln sind hier in Python nachgebildet und laufen gegen
   eine Tabelle deutscher Saetze. Das ist ein Nachbau, kein Beweis fuer die
   Firmware; zusammen mit Punkt 1 faengt es aber jede Rueckkehr zum alten
   Verhalten und jede Werkseinstellung, die mit haeufigen Woertern kollidiert.

Aufruf:  python tools/test_wortgrenzen.py
Rueckgabe 0, wenn alles stimmt.
"""
import re
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
MAIN = (WURZEL / 'src/main.cpp').read_text(encoding='utf-8')
MUSIC = (WURZEL / 'src/music.cpp').read_text(encoding='utf-8')
CONFIG = (WURZEL / 'include/config.h').read_text(encoding='utf-8')

fehler = []


def pruefe(bedingung, text):
    if not bedingung:
        fehler.append(text)


# --- 1. Struktur ------------------------------------------------------------
pruefe('static bool beginntMitWort(' in MAIN,
       'src/main.cpp: beginntMitWort fehlt')
pruefe('beginntMitWort(t, term)' in MAIN,
       'src/main.cpp: matchAnyTerm prueft kein ganzes Wort mehr')
pruefe(': t.startsWith(term))' not in MAIN,
       'src/main.cpp: matchAnyTerm ist auf den blossen Praefixvergleich zurueckgefallen')
pruefe('static bool containsWord(' in MUSIC and 'static bool beginntWort(' in MUSIC,
       'src/music.cpp: die Hilfsfunktionen mit Wortgrenzen fehlen')
pruefe('static bool genreTreffer(' in MUSIC,
       'src/music.cpp: genreTreffer fehlt')
pruefe(re.search(r'static bool has\s*\(', MUSIC) is None,
       'src/music.cpp: die alte Teilzeichenketten-Suche has() ist zurueck')


# --- Wortlisten aus dem Quelltext lesen -------------------------------------
def woerter(quelle, funktion, block):
    return re.findall(funktion + r'\(t,\s*"([^"]+)"\)', block)


block = MUSIC[MUSIC.index('bool strong ='):MUSIC.index('if (!strong && !verb)')]
STRONG = woerter(MUSIC, 'containsWord', block)
VERB_ANFANG = woerter(MUSIC, 'beginntWort', block)
VERB_WORT = [w for w in woerter(MUSIC, 'containsWord', block) if w not in STRONG]
pruefe(len(STRONG) >= 3, f'src/music.cpp: zu wenige Musikwoerter gefunden: {STRONG}')
pruefe(len(VERB_ANFANG) >= 3, f'src/music.cpp: zu wenige Abspielwoerter gefunden: {VERB_ANFANG}')

m = re.search(r'#define VOICE_TRIGGER_DEF\s+"([^"]*)"', CONFIG)
pruefe(m is not None, 'include/config.h: VOICE_TRIGGER_DEF nicht gefunden')
AUSLOESER = [w.strip() for w in (m.group(1) if m else '').split(',') if w.strip()]

FUGEN = re.findall(r'"([a-z]+)"', MUSIC[MUSIC.index('FUGEN[]'):MUSIC.index('FUGEN[]') + 200])


# --- 2. Die Regeln, nachgebildet -------------------------------------------
def ist_wortzeichen(c):
    return c.isascii() and (c.islower() or c.isdigit()) or not c.isascii()


def enthaelt_wort(t, key):
    for m in re.finditer(re.escape(key), t):
        vor = t[m.start() - 1] if m.start() else ' '
        nach = t[m.end()] if m.end() < len(t) else ' '
        if not ist_wortzeichen(vor) and not ist_wortzeichen(nach):
            return True
    return False


def beginnt_wort(t, key):
    return any(not ist_wortzeichen(t[m.start() - 1] if m.start() else ' ')
               for m in re.finditer(re.escape(key), t))


def beginnt_mit_wort(t, term):
    if not t.startswith(term):
        return False
    return len(t) == len(term) or not ist_wortzeichen(t[len(term)])


def musikabsicht(t):
    return (any(enthaelt_wort(t, w) for w in STRONG)
            or any(beginnt_wort(t, w) for w in VERB_ANFANG)
            or any(enthaelt_wort(t, w) for w in VERB_WORT))


def genre(t, key):
    return enthaelt_wort(t, key) or any(enthaelt_wort(t, key + f) for f in FUGEN)


# --- 3. Die Tabelle ---------------------------------------------------------
MUSIK_JA = [
    'spiel musik', 'mach radio an', 'ich will ein lied hören', 'spiele rock',
    'leg auf was von den beatles', 'spiel rockmusik',
]
MUSIK_NEIN = [
    'wer war der beste musiker des zwanzigsten jahrhunderts',
    'ich bin mitglied in einem verein',
    'gib mir ein beispiel für eine primzahl',
    'wem gehört das haus',
    'wie hoch ist der eiffelturm',
]
for s in MUSIK_JA:
    pruefe(musikabsicht(s), f'Musikabsicht nicht erkannt: {s!r}')
for s in MUSIK_NEIN:
    pruefe(not musikabsicht(s), f'Musikabsicht faelschlich erkannt: {s!r}')

pruefe(genre('spiel rockmusik', 'rock'), 'Zusammensetzung "rockmusik" trifft den Sender "rock" nicht')
pruefe(genre('spiel rock', 'rock'), 'Das freie Wort "rock" trifft den Sender "rock" nicht')
pruefe(not genre('erzähl was über rockefeller', 'rock'), '"rockefeller" trifft faelschlich den Sender "rock"')

AUSLOESER_NEIN = ['guten morgen', 'guten tag', 'guten abend', 'gute nacht',
                  'gute frage, das weiß ich nicht', 'gutes wetter heute']
for s in AUSLOESER_NEIN:
    for w in AUSLOESER:
        pruefe(not beginnt_mit_wort(s, w),
               f'Zweite Stimme springt faelschlich an: {s!r} durch {w!r}')
if AUSLOESER:
    w = AUSLOESER[0]
    pruefe(beginnt_mit_wort(w + ', mach das licht an', w),
           f'Zweite Stimme springt beim eigenen Ausloesewort {w!r} nicht an')

# --- Ergebnis ---------------------------------------------------------------
if fehler:
    for f in fehler:
        print('FEHLER:', f)
    sys.exit(1)
print(f'OK: Struktur geprueft, {len(MUSIK_JA) + len(MUSIK_NEIN) + len(AUSLOESER_NEIN) + 4} Saetze '
      f'verhalten sich wie erwartet.')
