#pragma once
// ============================================================================
//  ALEXO - Geheimnisse (dieses File nach "secrets.h" KOPIEREN und die Werte
//  eintragen). secrets.h wird von git ignoriert und landet nie in einem
//  öffentlichen Repository.
// ============================================================================

// --- WLAN -------------------------------------------------------------------
#define WIFI_SSID        "dein-wlan"
#define WIFI_PASSWORD    "dein-passwort"

// --- Schlüssel für die Dienste ----------------------------------------------
#define GROQ_API_KEY       "gsk_..."      // Whisper-Spracherkennung (kostenlos auf console.groq.com)
#define OPENAI_API_KEY     "sk-..."       // optional (Spracherkennung/Sprachausgabe von OpenAI)
#define ANTHROPIC_API_KEY  "sk-ant-..."   // Claude (das Gehirn)
#define ELEVENLABS_API_KEY "sk_..."       // Stimme für die Sprachausgabe (elevenlabs.io)
