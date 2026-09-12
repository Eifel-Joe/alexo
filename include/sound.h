#pragma once
// ============================================================================
//  ALEXO - Rückmeldetöne (im Betrieb erzeugt, vom VS1053 abgespielt)
// ============================================================================
#include <VS1053.h>

void soundStart(VS1053 &player);   // hoher Ton: Aufnahme beginnt
void soundStop(VS1053 &player);    // tiefer Ton: Aufnahme endet
void soundError(VS1053 &player);   // zwei tiefe Töne: Fehler
