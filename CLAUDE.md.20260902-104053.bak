# Alexo — panoramica tecnica

Riferimento tecnico sintetico del firmware **Alexo** (assistente vocale su ESP32-S3).
Per la guida completa e spiegata passo-passo vedi [`MANUALE.md`](MANUALE.md); per la
wake word [`WAKEWORD.md`](WAKEWORD.md). Progetto in **italiano** (codice, commenti,
messaggi seriali).

## Architettura (satellite + cloud)

La catena vocale è ibrida: il riconoscimento della parola di attivazione è **locale**
(offline), il resto sono chiamate **HTTPS** a servizi cloud.

```mermaid
flowchart TD
    START(["🗣️ Wake word 'Okay Nabu' (locale, offline)<br/>oppure click encoder"])
    MIC["🎤 Mic ICS-43434 I2S<br/>registra PCM 16 kHz in PSRAM<br/>stop al silenzio ~1.5 s"]
    STT["STT · Groq Whisper"]
    LLM["🧠 Claude (modello configurabile)<br/>il cervello · ricerca web"]
    TTS["TTS · ElevenLabs<br/>MP3 streaming"]
    OUT["🔊 VS1053 SPI → altoparlante"]
    TFT["📺 TFT ST7735<br/>chat / teleprompter"]
    RING["💍 Ring 12 LED WS2812<br/>animazioni di stato"]

    START --> MIC --> STT -->|testo| LLM -->|risposta| TTS --> OUT
    MIC -. aggiorna .-> TFT
    LLM -. aggiorna .-> TFT
    MIC -. stato .-> RING

    classDef cloud fill:#0e2a33,stroke:#00e5ff,color:#dfeef2;
    classDef ui fill:#1a1030,stroke:#ff2ea6,color:#dfeef2;
    class STT,LLM,TTS cloud;
    class TFT,RING ui;
```

> Riquadri **ciano** = servizi cloud (HTTPS); **magenta** = uscite UI (aggiornate lungo
> tutta la catena). Le frecce tratteggiate = "riflette lo stato", non passaggio di dati.

- **Ogni anello può girare IN CASA** invece che in cloud: se nel pannello si mette
  l'indirizzo di un server compatibile OpenAI sulla rete locale (LM Studio, un server
  Whisper, un TTS) e quel server risponde, Alexo usa quello; altrimenti torna al cloud
  da solo. Vedi "AI in casa" più sotto.
- **Cervello**: API Anthropic, modello `claude-haiku-4-5` (economico/veloce; si può
  passare a `claude-opus-5` per risposte più capaci — runtime dal pannello web).
- **STT**: **Groq** Whisper (gratis). **TTS**: **ElevenLabs** (supporta voce clonata).
  Servono 3 API key (Groq + Anthropic + ElevenLabs); OpenAI è opzionale.
- **Attivazione**: **wake word locale "Okay Nabu"** (microWakeWord/TFLite Micro, tutto
  offline sull'S3) **oppure** click dell'**encoder** — in parallelo. La registrazione si
  chiude da sola dopo **1.5 s di silenzio** (tetto 20 s).
- **Chat continua** (`gSettings.chatContinua`, pannello, di fabbrica spenta): finita una
  risposta il mic si **riapre da solo**, così la domanda dopo non vuole di nuovo la wake
  word. Mentre aspetta, il ring sta in **`ST_FOLLOWUP`** (due punti **ambra** che girano a
  luminosità **costante** — non il VU-meter, che direbbe "ti sto già registrando"; e non un
  respiro, perché calando fino al buio sembrava che la chat si chiudesse e riaprisse a ogni
  ciclo) e il display scrive "a te"; appena
  parti davvero — stessa soglia adattiva dello stop-al-silenzio, via `micVoiceStarted()` —
  passa a `ST_LISTENING`. Si esce da tre parti: **nessuno parla** entro `CHAT_FOLLOWUP_MS`
  (3 s, `micSetNoVoiceMs` accorcia l'attesa di cortesia per la sola registrazione dopo), un
  **click** dell'encoder (ferma la registrazione a vuoto = come non aver parlato: in
  `gobbo.cpp` il click in `ST_FOLLOWUP` vale come in ascolto, altrimenti riavvierebbe una
  chat appena chiusa), o un errore. La memoria della conversazione c'era già (storico 8
  messaggi in `llm.cpp`): la chat continua toglie solo la wake word, non aggiunge contesto.
  Prima di riaprire il mic si fanno **400 ms di `micFlush()`** — la coda della voce appena
  detta rientra nel microfono e senza questo partirebbe una domanda fantasma.
- **Gesti encoder**: **click** = avvia/ferma chat; **doppio click** = on/off del ring
  reattivo al suono; **premuto+giro** = volume; **giro** = scroll. **In MUSICA**
  (ST_MUSIC) il ramo cambia: **click** = stazione successiva, **doppio click** = esci,
  **giro** = volume. (Il click singolo è "differito" ~350 ms per distinguerlo dal doppio:
  la libreria Versatile_RotaryEncoder chiama `handlePress` anche sul 1° click di un doppio
  → disambiguazione in `encoder.cpp`.) GPIO14 (vecchio push-to-talk) pilota il
  **backlight del display** (spegnimento a riposo).
- **Splash di boot**: animazione HUD su TFT + ring "carica" all'avvio (`bootSplash()` in
  main.cpp, flag `SPLASH_BOOT`).

> **Wake word**: "Okay Nabu" è un modello **pre-addestrato** (preciso). I modelli inglesi
> "hey jarvis"/"alexa" davano problemi (accento; "alexa" scattava su qualsiasi "-xa"). Una
> parola **"Alexo" CUSTOM** è l'upgrade futuro (training microWakeWord via Colab). Pipeline
> completa in [`WAKEWORD.md`](WAKEWORD.md).

## Hardware

| Componente | Modello | Ruolo |
|---|---|---|
| MCU | ESP32-S3 **N16R8** (16MB flash QIO, 8MB PSRAM **octal/OPI**) | — |
| Microfono | **ICS-43434** I2S (attivo, `MIC_USE_I2S=1`) — SCK5/WS6/SD7, L/R→GND. MAX4466 analogico come backend alternativo (`MIC_USE_I2S=0`, GPIO4) | input voce |
| Audio out | **VS1053 / VS1003** (SPI). NB: alcuni moduli "VS1053" sono in realtà un VS**1003** (SCI_STATUS versione 3); la libreria li gestisce uguale per l'MP3. **DREQ su GPIO18** (spostato da 47), **XRST su GPIO8** (spostato da 38 = builtin LED) | decoder MP3 → linea audio |
| Ampli | **PAM8302A** Class-D mono (2.5W) | amplifica LOUT/ROUT del VS1053 → altoparlante. SD (shutdown, att. basso) su **GPIO39**: acceso solo durante l'interazione, muto a riposo (no fruscio Class-D) |
| Display | **ST7735** TFT 1.8" 128x160 SPI | chat/teleprompter a colori. Bus SPI **dedicato HSPI**, separato dal VS1053. **Backlight su GPIO14**: spento dopo 2 min di inattività, riacceso al primo intervento |
| LED | ring **WS2812** 12 LED NeoPixel (5V) | animazioni di stato |

> Nota: un "Sound Sensor LM358" NON è adatto alla voce (rileva solo il livello sonoro,
> non la forma d'onda): serve un microfono vero (ICS-43434 I2S o MAX4466 analogico).

Tutti i pin sono in [`include/config.h`](include/config.h) — **unica fonte di verità**.

## Build / Flash

Firmware in **C++ / PlatformIO** (Arduino). Comandi principali:

```bash
# build
pio run -e esp32-s3-devkitc-1
# flash firmware (primo caricamento via USB)
pio run -e esp32-s3-devkitc-1 -t upload
# flash del FILESYSTEM (pagina del pannello web data/index.html -> LittleFS)
# serve solo se hai toccato data/
pio run -e esp32-s3-devkitc-1 -t uploadfs
# monitor seriale (via USB)
pio run -e esp32-s3-devkitc-1 -t monitor
```

- **Primo flash via USB**; poi, se vuoi, aggiornamenti **OTA via WiFi** (`platformio.ini`
  con `upload_protocol = espota` e `upload_port = alexo.local`). Durante l'OTA il display
  mostra una schermata dedicata con la percentuale.
- Su Windows, se `pio` non è nel PATH, invocalo col percorso completo
  (`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`).

**Log via rete (Telnet)**: per leggere l'output senza cavo USB, il firmware espone un
**log Telnet sulla porta 23** ([`src/netlog.cpp`](src/netlog.cpp)): `telnet alexo.local`
(o PuTTY in Raw/Telnet). Non bloccante, non interferisce con l'OTA. Ci passa l'output di
`micDiag()`; `netlogPrintln()` è riusabile per altri log.

### ⚠️ Config critica (non rimuovere)
La board `esp32-s3-devkitc-1` è definita **8MB**. Senza queste righe in `platformio.ini`
il bootloader viene scritto a 8MB e la partizione custom (che arriva a ~16MB) manda in
**boot loop infinito**:

```ini
board_upload.flash_size  = 16MB          ; FIX boot loop
board_upload.maximum_size = 16777216
board_build.arduino.memory_type = qio_opi ; attiva la PSRAM octal (altrimenti 0 byte)
```

Sintomo: sulla seriale solo messaggi ROM ripetuti + `rst:0x3 (RTC_SW_SYS_RST)`, nessun
output del programma.

## Struttura

```
include/
  config.h           # tutti i pin + parametri hardware (+ flag WAKE_*, REC_*, MIC_DIAG, TFL_SELFTEST) + DEFAULT del pannello web
  mic.h net.h stt.h llm.h tts.h ui.h sound.h netlog.h wakeword.h tfltest.h music.h
  encoder.h gobbo.h volume.h
  localai.h          # servizi AI in casa: raggiungibilita' + nome modello (vedi sotto)
  settings.h webui.h # pannello impostazioni web (parametri runtime in NVS)
  secrets.example.h  # template -> copiare in secrets.h (gitignored)
src/
  main.cpp           # macchina a stati (loop su core 1): ascolto->pensa->parla. runConversation = una domanda o tante di fila (chat continua). matchAnyTerm (trigger voce alt) + rispostaPersonalizzata + isAllucinazione (anti-fantasma) + skip se !micHeardVoice
  mic.cpp            # mic I2S: registra (stop al silenzio ADATTIVO/RMS) + livello ring + micReadChunk/micFlush per il wake + micGetLive + micHeardVoice
  net.cpp            # connessione WiFi (credenziali da secrets.h) + orologio NTP (timeBegin, fuso Europe/Rome) + nowContextString per Claude
  stt.cpp            # POST multipart del WAV -> Whisper (Groq, o server in casa) -> testo
  llm.cpp            # due strade: Anthropic Messages API (+ricerca web) oppure server compatibile OpenAI in casa. Modello+prompt da gSettings, +data/ora NTP nel system. ripuliMarkdown sulla risposta (i modelli lo usano anche se il prompt lo vieta): a video e a voce lo stesso testo
  tts.cpp            # voce -> streaming al VS1053: ElevenLabs (MP3) o server in casa (WAV). normalizzaPerVoce: gradi/%/frazioni/valute + ORARI (leggiOrario) + MIGLIAIA (leggiMigliaia) + DATE (leggiData) + UNITA' abbreviate (leggiUnita, tabella UNITA estendibile) + ORDINALI in lettere (leggiOrdinale/ordinaleParola: "85esima" -> "ottantacinquesima") + via il markdown
  localai.cpp        # servizi AI IN CASA: il PC risponde? che modello ha caricato? (cache + giro di controllo per le spie del display)
  ui.cpp             # animazioni ring NeoPixel su TASK dedicato (core 0)
  sound.cpp          # bip di feedback (toni WAV generati al volo sul VS1053)
  gobbo.cpp          # chat/teleprompter sul TFT (task core 0, bus HSPI, canvas 16bit) + schermata OTA HUD verde (renderOtaScreen) + anello chat UTF-8 per il pannello web (gobboChatRev/Count/Item)
  encoder.cpp        # encoder rotativo (lib Versatile_RotaryEncoder, polling su core 0)
  volume.cpp         # volume VS1053 (premuto+giro encoder / pannello web), salvato in NVS
  music.cpp          # web-radio MP3 -> VS1053 (http E https via WiFiClientSecure). Stazioni editabili (gSettings.musicStations). Ring reattivo alla cassa. Solo MP3 (NO AAC/HLS)
  netlog.cpp         # log via rete (Telnet porta 23): leggere l'output senza cavo USB
  settings.cpp       # parametri RUNTIME (gSettings) caricati/salvati in NVS (default = config.h)
  webui.cpp          # web server (porta 80) + LittleFS: pannello http://alexo.local/ + API JSON + live mic
  wakeword.cpp       # WAKE WORD locale (microWakeWord): frontend->modello->detection (gain/cutoff/window da gSettings). Vedi WAKEWORD.md
  wake_model.h       # modello wake INT8 (g_wake_model): "okay_nabu". Sostituibile (drop-in)
  tfltest.cpp        # self-test TFLite Micro (flag TFL_SELFTEST), usato per validare il runtime
data/
  index.html         # pagina del pannello impostazioni (servita da LittleFS; -t uploadfs per caricarla)
lib/microfrontend/   # microfrontend TFLM (40 feature mel) + kissfft v130, vendorizzato per il wake
partitions_custom.csv  # tabella partizioni 16MB OTA (in uso)
```

> **Pannello impostazioni web** (`http://alexo.local/`): i parametri "tarabili"
> (mic/stop-al-silenzio/LED, **chat continua**, wake, volume+voci, modello+prompt Claude, risposta personalizzata,
> frasi anti-fantasma Whisper, **indirizzi e modelli dei servizi in casa** + **"solo casa"**)
> sono **runtime**
> in `gSettings` (modulo settings), caricati
> dall'NVS all'avvio (default = macro `*_DEF` di config.h) e modificabili dal browser senza
> ricompilare. `webui.cpp` serve la pagina da **LittleFS** + API JSON (`/api/settings`
> GET/POST, `/api/live`, `/api/reset`, `/api/music/stop`, `/api/chat`, `/api/local/test`). Server sincrono:
> durante un'interazione la pagina non risponde per qualche secondo (normale). Lettura LIVE
> del mic (`micGetLive`) per tarare le soglie dal browser. **Card "Chat"**: rispecchia la
> conversazione del TFT (anello UTF-8 in PSRAM nel gobbo, `/api/chat` in streaming; refresh
> guidato da `chatRev` in `/api/live`, cioè a ogni nuovo messaggio, non a timer).

> **AI in casa (al posto del cloud)**: ognuno dei tre anelli — trascrizione, cervello,
> voce — può essere servito da un PC sulla stessa rete, purché esponga l'API in **formato
> OpenAI** (`/v1/audio/transcriptions`, `/v1/chat/completions`, `/v1/audio/speech`).
> Nel pannello si mettono indirizzo e nome modello per ciascuno. **Regola unica**: indirizzo
> vuoto = cloud come sempre; indirizzo pieno e server che risponde = si va in casa; server
> spento o in errore = si ricade sul cloud da solo, senza intervento. **Nome modello vuoto**
> = Alexo chiede al server quale ha caricato (`GET /v1/models`), così si cambia modello dal
> PC senza toccare il pannello. Dettagli in [`localai.h`](include/localai.h). Da sapere:
> - il **cervello in casa non ha la ricerca web** (il tool `web_search` è di Anthropic), e il
>   system prompt glielo dice, altrimenti inventa;
> - per il cervello "disponibile" richiede anche un **modello caricato**: un server acceso ma
>   vuoto accetta la connessione e poi rifiuta la domanda;
> - la **temperatura** del solo modello locale è regolabile dal pannello (i server locali
>   partono da 0.7-0.8, troppo alta per un assistente vocale);
> - la voce in casa si chiede in **WAV**, non in MP3: il VS1053 lo decodifica nativamente
>   (come i bip di `sound.cpp`), quindi il server non deve comprimere niente e non serve
>   ffmpeg. Costa più banda (~380 kbit/s contro 128), irrilevante su WiFi;
> - **"Solo casa"** (interruttore nel pannello, `gSettings.localOnly`, di fabbrica spento): il
>   ripiego sul cloud è comodo ma **silenzioso**, e le tre spie non bastano a escluderlo (dicono
>   com'è andato l'ultimo controllo, non dove è finita la frase appena detta). Con l'interruttore
>   acceso trascrizione, cervello e voce **non escono mai**: se il servizio in casa manca o
>   sbaglia, Alexo lo scrive e si ferma. Si applica al click, senza premere Salva. Restano fuori
>   la web-radio (la si chiede esplicitamente) e l'orologio NTP (non trasporta niente di detto);
> - **"Voce sempre in casa"** (`gSettings.ttsLocalOnly`, di fabbrica spento) è l'interruttore
>   ristretto al solo TTS, per non consumare i crediti gratuiti di ElevenLabs: la voce si chiede
>   sempre al server di casa, **senza il controllo di raggiungibilità** (un'attesa in meno) e
>   **senza ripiego** sul cloud; se il server non risponde Alexo lo scrive e non parla.
>   Trascrizione e cervello non cambiano. **Per la voce la regola generale non vale**: il TTS di
>   casa entra in gioco solo se lo chiede uno dei due interruttori, non perché il server risponde
>   (indirizzo compilato + nessun flag = ElevenLabs). Quando tocca a casa, la risposta a video
>   (TFT e pannello) è preceduta da **`[LOC]`** — il testo parlato resta pulito;
> - quando un pezzo **esce comunque** su internet lo dice in chat una riga di avviso
>   (`localSayCloud`/`localSayBlocked` in localai.cpp): **verde sul TFT** — il rosso su ST7735 è
>   illeggibile — e **rossa nel pannello web**. Compare solo se quel servizio in casa è configurato
>   **e solo con "solo casa" acceso**: a interruttore spento il cloud è il funzionamento normale e
>   l'avviso sarebbe rumore a ogni frase;
> - la **memoria della conversazione è una sola** per le due strade, quindi viene **azzerata al
>   cambio strada** e nel momento del ripiego: quello che è stato detto in casa non parte verso
>   il cloud insieme alla domanda successiva;
> - **tre pallini nell'header del TFT** (ordine: trascrizione, cervello, voce) dicono a colpo
>   d'occhio chi sta girando dove: verde = in casa, rosso = in cloud. Il display gira sul core
>   0 e non può aspettare la rete, quindi legge l'ultimo esito noto; a tenerlo aggiornato è
>   `localRefreshTick()` nel loop, che riprova **un servizio per volta ogni ~20 s** e subito
>   dopo svuota il mic (l'attesa del controllo è un buco in cui il wake word non ascolta).

> **Wake word / TFLite Micro**: runtime = lib **Chirale_TensorFlowLite** (in
> `platformio.ini`); `esp-tflite-micro` scartato (gira male in PlatformIO). Il microfrontend
> NON è in Chirale → vendorizzato in `lib/microfrontend/`. Per cambiare wake word: sostituisci
> `src/wake_model.h` (`xxd -i` del nuovo `.tflite`, simbolo `g_wake_model`) e aggiorna
> `WAKE_PROB_CUTOFF`/`WAKE_WINDOW` dal manifest. Flag diagnostici in `config.h`: `WAKE_TEST`
> (prova la catena leggendo il mic), `TFL_SELFTEST` (hello_world), `MIC_DIAG` (rumore mic).

### La voce: come si legge un testo scritto

Il testo passa da `normalizzaPerVoce` (`tts.cpp`) **prima** del bivio cloud/casa, quindi le
correzioni valgono su **entrambe** le voci. Oltre a gradi, percentuali, orari, date, migliaia
e unita' di misura, ci sono gli **ordinali**: le voci leggono "85esima" cifra-per-cifra
("ottocinquesima"), quindi l'ordinale va scritto per esteso — `cardinaleParola` (numeri in
lettere, 0..999999) + `ordinaleParola`, con gli irregolari 1-10, ...tre/...sei che tengono la
vocale (ventitreesimo, ventiseiesimo) e ...mila → millesimo (duemillesimo).
Si intercettano `85esima`, gli indicatori `ª`/`º` e il grado usato come ordinale
(`21° secolo`). Quest'ultimo e' ambiguo: decide la **parola dopo** — un ordinale e' seguito
da un nome, i gradi da una preposizione o da niente (lista `DOPO_GRADI`); nel dubbio restano
**gradi**. Per aggiungere un'unita' di misura basta una riga nella tabella `UNITA`; il campo
`serveNum` serve alle sigle di una lettera sola (`m`, `l`, `g`, `s`, `h`), che valgono solo
con un numero davanti.

## Segreti

WiFi e API key vanno in `include/secrets.h` (copiato da `secrets.example.h`, **ignorato da
git**). Mai committare le chiavi.

## Scelte tecniche e trappole risolte (utili per chi costruisce/forka)

- **VS1053/VS1003 cold-boot**: DREQ e XRST originariamente su GPIO47/38 non partivano in modo
  affidabile a freddo (GPIO38 = builtin LED che sporca il reset). Spostati su **GPIO18/GPIO8**
  → avvio affidabile.
- **Stop-al-silenzio ADATTIVO** (mic.cpp): la soglia "voce" è **relativa** al rumore di fondo
  stimato in continuo (`soglia = noiseFloor*margin + floor`), su **energia media (RMS)** non
  sul picco → robusto ai rumori impulsivi (colpi, folate d'aria sul mic). Param runtime
  `REC_SILENCE_MARGIN`/`REC_SILENCE_FLOOR`.
- **Anti-fantasma Whisper**: i falsi avvii registravano silenzio → Whisper "allucinava"
  ("Grazie", ecc.). Difese: skip pre-Whisper se non c'è voce vera (`micHeardVoice`) + filtro
  `isAllucinazione` con lista editabile dal pannello (`gSettings.hallucTerms`).
- **Ora reale a Claude**: orologio via **NTP** (net.cpp `timeBegin`) + data/ora (locale+UTC)
  iniettate nel system prompt (`nowContextString` → llm.cpp), altrimenti Claude sbaglia ora/fusi.
- **Idle reattivo (mic → LED)**: i NeoPixel accesi sporcavano il mic via alimentazione →
  risolto con **condensatori di disaccoppiamento** (470µF sul VCC del mic + 1000µF sul 5V del
  ring). Col mic I2S il problema è molto ridotto.
- **NeoPixel**: `setBrightness(N basso)` **quantizza** il fading → tenere brightness a 255 e
  usare valori bassi direttamente nei colori delle animazioni.
- **VS1053 `stopSong()`** sui toni brevi "incanta" il chip (solo il 1° bip suona) → non usarlo:
  feed dati + coda di silenzio (~2KB di zero/endFillByte). Vale per bip e TTS.
- **Musica web-radio**: il VS1053 decodifica **SOLO MP3** (no AAC/HLS `.m3u8`). La riproduzione
  "a scatti" di alcune radio dipende dal **server/rete**, non dal codice/formato (stream MP3
  identici possono comportarsi diversi sullo stesso WiFi). URL http e https (WiFiClientSecure).
- **Due core**: pipeline pesante (rete) su **core 1**; animazioni LED/display + encoder su
  **core 0** (TIME-BASED con `millis()` → restano fluide anche mentre il core 1 è bloccato in
  rete). Tutte le chiamate HTTPS con TLS `setInsecure()`.

## Convenzioni

- Tutto in italiano. Pin solo in `config.h`.
- Audio in: mic I2S ICS-43434 (`MIC_USE_I2S 1`, attuale) o MAX4466 analogico su ADC1 (GPIO
  non-ADC2 per non confliggere con la WiFi). Audio out: VS1053 SPI.
- Buffer audio in PSRAM (`ps_malloc`).
