#!/usr/bin/env python3
"""Prueft die Zeichentabelle des Displays in src/gobbo.cpp.

Das Display kann kein UTF-8. cpFromUnicode() bildet Unicode-Zeichen auf
die Zeichenwerte von CP437 ab, und jedes Zeichen ohne Eintrag landet auf
einem Fragezeichen. Der Test prueft zweierlei:

  1. Jeder numerische Eintrag stimmt mit dem cp437-Codec von Python
     ueberein. Ein Zahlendreher faellt sonst erst am Geraet auf.
  2. Alle deutschen Sonderzeichen haben einen Eintrag.

Eintraege, die ein Zeichenliteral zurueckgeben (etwa 'A' fuer À), sind
bewusste Vereinfachungen fuer Zeichen, die CP437 nicht kennt, und werden
uebersprungen.

Aufruf: python tools/test_cp437.py
"""
import re
import sys
from pathlib import Path

QUELLE = Path(__file__).resolve().parent.parent / "src" / "gobbo.cpp"
DEUTSCHE_ZEICHEN = "äöüÄÖÜß"


def funktionsrumpf(text):
    start = text.index("static uint8_t cpFromUnicode(")
    tiefe, i = 0, text.index("{", start)
    anfang = i
    while True:
        if text[i] == "{":
            tiefe += 1
        elif text[i] == "}":
            tiefe -= 1
            if tiefe == 0:
                return text[anfang : i + 1]
        i += 1


def eintraege(rumpf):
    """Liefert {unicode: zeichenwert} fuer alle numerischen Rueckgaben."""
    gefunden = {}
    for zeile in rumpf.splitlines():
        rueck = re.search(r"return\s+0x([0-9A-Fa-f]{2})\s*;", zeile)
        if not rueck:
            continue
        wert = int(rueck.group(1), 16)
        for fall in re.findall(r"case\s+0x([0-9A-Fa-f]{4})\s*:", zeile):
            gefunden[int(fall, 16)] = wert
    return gefunden


def main():
    rumpf = funktionsrumpf(QUELLE.read_text(encoding="utf-8"))
    tabelle = eintraege(rumpf)
    fehler = []

    for codepunkt, wert in sorted(tabelle.items()):
        zeichen = chr(codepunkt)
        try:
            erwartet = zeichen.encode("cp437")[0]
        except UnicodeEncodeError:
            fehler.append(
                f"U+{codepunkt:04X} ({zeichen}) kennt CP437 nicht, "
                f"Tabelle liefert aber 0x{wert:02X}"
            )
            continue
        if wert != erwartet:
            fehler.append(
                f"U+{codepunkt:04X} ({zeichen}): Tabelle 0x{wert:02X}, "
                f"CP437 erwartet 0x{erwartet:02X}"
            )

    fehlend = [z for z in DEUTSCHE_ZEICHEN if ord(z) not in tabelle]
    if fehlend:
        fehler.append(
            "Ohne Eintrag und damit als Fragezeichen auf dem Display: "
            + " ".join(f"{z} (U+{ord(z):04X}, CP437 0x{z.encode('cp437')[0]:02X})" for z in fehlend)
        )

    if fehler:
        print(f"FEHLGESCHLAGEN: {len(fehler)} Beanstandung(en)")
        for f in fehler:
            print("  -", f)
        return 1

    print(f"OK: {len(tabelle)} Zuordnungen stimmen, alle deutschen Zeichen vorhanden.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
