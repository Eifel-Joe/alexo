#!/usr/bin/env python3
"""Sucht italienische Reste in den uebersetzten Dateien.

Die Liste enthaelt italienische Funktions- und Allerweltswoerter, die im
Deutschen nicht vorkommen. Sie ist eine Heuristik, kein Beweis: sie findet
zuverlaessig ganze Saetze, die stehen geblieben sind, und uebersieht
einzelne Fachbegriffe.

Ausgenommen sind Fremdbibliotheken, erzeugte Modelldaten, Sicherungskopien
und Dateien, die laut Spezifikation italienisch bleiben.

Aufruf:  python tools/pruefe_sprache.py [--details]
Rueckgabe 0, wenn nichts gefunden wurde.
"""
import re
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent

ENDUNGEN = ('.cpp', '.h', '.md', '.html', '.ini', '.csv')

AUSGENOMMEN = (
    '.pio/',                    # Build-Verzeichnis mit Fremdbibliotheken
    '.git/',
    'lib/',                     # Fremdbibliothek, englisch
    'src/wake_model.h',         # erzeugte Modelldaten
    'src/tfl_hello_model.h',    # erzeugte Modelldaten
    'tools/',                   # dieses Werkzeug samt Wortliste
    'docs/',                    # Spezifikation und Plan nennen die Woerter
    'include/secrets.h',        # lokal, nicht im Repo
    '.bak',                     # Sicherungskopien im Repo
    '.prima_di_mycroft',        # Sicherungskopien im Repo
)

# Italienische Woerter, die im Deutschen nicht vorkommen. Wortgrenzen beidseitig.
WOERTER = """
della delle degli dello dei dal dalla dalle nel nella nelle sul sulla sulle
col coi che non perche piu gia cosi quando quindi anche senza dopo sempre
ancora adesso solo ogni tutto tutti tutte questo questa questi queste quello
quella essere fare viene vengono sono siamo deve devono serve servono arriva
resta torna tocca mette prende legge scrive chiama vale manca basta cambia
finisce apre chiude parola parole frase frasi numero numeri voce voci suono
schermo display riga righe scelta scelte errore errori messaggio messaggi
risposta risposte domanda domande memoria ricerca musica canzone stazione
volume acceso spento vuoto pieno lungo corto grande piccolo nuovo vecchio
primo ultimo dentro fuori sopra sotto avanti indietro insieme invece oppure
mentre finche affinche siccome poiche dunque allora ecco ormai appena
""".split()

# Woerter, die auch im Deutschen oder im Fachjargon vorkommen und deshalb
# nicht als Treffer zaehlen.
UNVERDAECHTIG = {'display', 'volume', 'numero'}

# Italienische BEZEICHNER, die laut Spezifikation bleiben: Variablennamen, der
# Werkzeugname fuer Claude und Dateinamen einer realen Installation. Sie werden
# aus der Zeile entfernt, BEVOR gesucht wird, damit der Rest der Zeile weiter
# geprueft wird. Ein vergessener italienischer Satz faellt trotzdem auf, denn er
# enthaelt fast immer eines der vielen Funktionswoerter aus der Liste oben.
BEZEICHNER = (
    'riproduci_musica', 'MESI_VOCE', 'ST_COL', 'voce_alexo',
    'campione_corto20', 'NonToccare', 'campioni',
    'risposta', 'ancora', 'acceso', 'voce', 'col',
)

MUSTER = re.compile(
    r'(?<![A-Za-zÀ-ÿ])(' +
    '|'.join(w for w in WOERTER if w not in UNVERDAECHTIG) +
    r')(?![A-Za-zÀ-ÿ])', re.IGNORECASE)

# Typisch italienische Apostroph-Schreibung fuer Akzente: e' gia' piu' puo'
APOSTROPH = re.compile(r"(?<![A-Za-z])(e|gia|piu|puo|perche|cosi|citta|liberta|qualita)'")


def dateien():
    for p in sorted(WURZEL.rglob('*')):
        if not p.is_file() or p.suffix not in ENDUNGEN:
            continue
        rel = p.relative_to(WURZEL).as_posix()
        if any(a in rel for a in AUSGENOMMEN):
            continue
        yield p, rel


def main():
    details = '--details' in sys.argv
    gesamt = 0
    for p, rel in dateien():
        treffer = []
        for nr, zeile in enumerate(p.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
            rein = zeile
            for b in BEZEICHNER:
                rein = re.sub(r'(?<![A-Za-z_])' + re.escape(b) + r'(?![A-Za-z_])', ' ', rein)
            gefunden = set(m.group(1).lower() for m in MUSTER.finditer(rein))
            gefunden |= set(m.group(0) for m in APOSTROPH.finditer(rein))
            if gefunden:
                treffer.append((nr, sorted(gefunden), zeile.strip()[:90]))
        if treffer:
            gesamt += len(treffer)
            print(f"{rel:34} {len(treffer):4} Zeilen")
            if details:
                for nr, woerter, zeile in treffer[:12]:
                    print(f"      {nr:5}  {','.join(woerter):28} {zeile}")
                if len(treffer) > 12:
                    print(f"      ... und {len(treffer) - 12} weitere")

    if gesamt:
        print(f"\nInsgesamt {gesamt} Zeilen mit italienischen Resten.")
        return 1
    print("Keine italienischen Reste gefunden.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
