# ALEXO — La wake word locale (microWakeWord)

La parola di attivazione **"Okay Nabu"** è riconosciuta **dentro l'ESP32-S3**, offline:
nessun audio esce di casa per il solo fatto di stare in ascolto. È l'unica intelligenza
artificiale che gira sul microcontrollore; tutto il resto della catena vocale è altrove
(cloud o server di casa).

Il wake **sostituisce solo l'avvio**: da lì in poi la pipeline è identica a quella del
click sull'encoder, che resta disponibile in parallelo come avvio manuale e come stop.

- Implementazione: [`src/wakeword.cpp`](src/wakeword.cpp), modello in
  [`src/wake_model.h`](src/wake_model.h), microfrontend in [`lib/microfrontend/`](lib/microfrontend/).
- Si accende con `WAKE_ENABLE 1` in [`include/config.h`](include/config.h) (default).

## Come funziona la catena

```
🎤 I2S 16 kHz (flusso continuo)
   → microfrontend: 40 feature mel ogni 10 ms
   → modello microWakeWord INT8 (streaming, mantiene uno stato interno)
   → probabilità 0–255
   → media mobile su WAKE_WINDOW step > WAKE_PROB_CUTOFF
   → TRIGGER: stesso ingresso del click encoder → parte la conversazione
```

Il modello è **streaming**: non guarda uno spezzone di audio alla volta, ma un flusso
continuo di cui conserva memoria fra un'inferenza e l'altra. Da qui la regola più
importante di tutte: **il flusso non va interrotto**. Ogni pausa nella lettura del
microfono gli fa perdere il filo e la parola non viene più riconosciuta.

### I numeri in uso

| | Valore | Dove |
| --- | --- | --- |
| Modello | microWakeWord **v2 "okay_nabu"**, INT8, **60264 byte** | `src/wake_model.h` (`g_wake_model`) |
| Tensor arena | ~26 KB richiesti, **40 KB allocati in PSRAM** | `WAKE_ARENA_BYTES` in `wakeword.cpp` |
| Soglia probabilità | **246** su 255 (manifest: 0.97) | `WAKE_PROB_CUTOFF` |
| Finestra media mobile | **5** step | `WAKE_WINDOW` |
| Guadagno digitale | **3** | `WAKE_GAIN` |

> Le tre `WAKE_*` sono **regolabili dal pannello web** senza ricompilare: in `config.h`
> ci sono solo i valori di fabbrica.

### Il frontend: la specifica deve combaciare col training

Le 40 feature vanno calcolate **esattamente** come quando il modello è stato addestrato,
altrimenti il modello riceve numeri che non riconosce. Configurazione (da
`preprocessor_settings.h` di ESPHome, preprocessore micro_speech / TFLM microfrontend):

- sample rate **16000**, finestra **30 ms** (480 campioni), passo **10 ms** (160 campioni)
- **40** canali mel, banda **125–7500 Hz**
- noise reduction: `smoothing_bits=10`, `even=0.025`, `odd=0.06`, `min_signal_remaining=0.05`
- PCAN (controllo automatico del guadagno): `enable=true`, `strength=0.95`, `offset=80.0`, `gain_bits=21`
- log scale: `enable=true`, `scale_shift=6`
- uscita: 40 feature per slice, una ogni step da 10 ms

Il modello accumula `stride` slice (letto a runtime da `input->dims[1]`) prima di ogni
`Invoke()`.

## L'ordine in cui è stato costruito

Ogni passo era verificabile da solo, prima di passare al successivo: se la catena
smette di funzionare, è l'ordine in cui conviene ricontrollarla.

**1 · Microfono pulito.** Passa-alto (~120 Hz) e shift 15 sul PCM, più la diagnostica
`MIC_DIAG` per misurare il rumore di fondo. *Verifica:* i livelli stampati distinguono
silenzio e voce. Senza questo, tutto il resto lavora su audio sporco.

**2 · TFLite Micro compila e gira.** Prima di scrivere una riga di wake word bisogna
sapere che il runtime esiste e non va in crash sulla board: un `Invoke()` banale su un
modellino di prova (`TFL_SELFTEST` + [`src/tfltest.cpp`](src/tfltest.cpp)). *Verifica:*
nessun crash, tempi di inferenza (38–132 µs) stampati sul log via rete. Questo passo
serve a **togliere di mezzo il rischio più grosso per primo**, quello della toolchain.

**3 · Microfrontend.** Generare le 40 feature ogni 10 ms dallo stream I2S. *Verifica:*
i valori stampati cambiano in modo evidente fra voce e silenzio.

**4 · Modello pre-addestrato.** Incorporare un modello pronto da
`esphome/micro-wake-word-models` come array `const`, collegare frontend → modello e
stampare la probabilità. *Verifica:* pronunciando la parola la probabilità sale. È
questo il passo che dimostra che la catena intera funziona.

**5 · Soglia, media mobile e trigger.** Sopra soglia per N step consecutivi → si avvia la
conversazione, dallo stesso punto del click encoder. Più il periodo **refrattario**: dopo
uno scatto la finestra si svuota, o la stessa parola ne farebbe partire tre.

**6 · Rifinitura.** Sordità durante la conversazione, rientro in ascolto pulito,
integrazione con l'audio a riposo (sotto).

**Passo che resta aperto:** una parola **tutta propria** al posto di un modello
pre-addestrato (in fondo a questa pagina).

## Le due decisioni tecniche che hanno pesato

### Quale runtime TFLite Micro (risolta: libreria Arduino)

Il progetto è Arduino/PlatformIO, mentre TFLite Micro nasce per ESP-IDF. Due strade:

1. **`esp-tflite-micro`** (Espressif): la più veloce, ha i kernel ottimizzati `esp-nn`
   sulle istruzioni vettoriali dell'S3. Ma è un componente ESP-IDF, e in
   PlatformIO-Arduino va innestato a mano: integrazione delicata.
2. **Una libreria Arduino che impacchetta TFLM**: si mette in `lib_deps` e funziona,
   al prezzo di rinunciare all'accelerazione `esp-nn`.

**Ha vinto la (2)**: `esp-tflite-micro` in PlatformIO-Arduino si comporta male, quindi si
usa **Chirale_TensorFlowLite** (in `platformio.ini`). La performance non è il collo di
bottiglia — l'inferenza è microsecondi contro i 10 ms di ogni step.

### Il microfrontend va vendorizzato

Chirale **non** include il microfrontend, che però è obbligatorio (senza, il modello non
ha input). Sta quindi in [`lib/microfrontend/`](lib/microfrontend/), copiato dai sorgenti
TFLM. Cosa contiene, per chi dovesse rifarlo:

- Da `tensorflow/lite/experimental/microfrontend/lib/` (escludendo `_io`/`_test`/`_main`/
  `memmap`/`BUILD`): `frontend`, `frontend_util`, `filterbank`(+`util`),
  `noise_reduction`(+`util`), `pcan_gain_control`(+`util`), `log_scale`(+`util`),
  `log_lut`, `window`(+`util`), `fft`, `fft_util`, `kiss_fft_int16`,
  `kiss_fft_common.h`, `bits.h`.
- **Dipendenza kissfft**: `kiss_fft_int16` include, dentro il namespace
  `kissfft_fixed16` con `FIXED_POINT=16`, i sorgenti `kiss_fft.h/.c` e
  `tools/kiss_fftr.h/.c` (+ `_kiss_fft_guts.h`) dal repo `mborgerding/kissfft`, alla
  versione indicata da `tensorflow/lite/micro/tools/make/kissfft_download.sh`
  (+ la patch `third_party/kissfft/kissfft.patch`).
- **Struttura della cartella**: `lib/microfrontend/src/tensorflow/...`, perché TFLM usa
  include assoluti; i sorgenti kissfft devono essere raggiungibili come `kiss_fft.h` e
  `tools/kiss_fftr.h` dall'include path.

## Integrazione con l'audio a riposo

Il wake ha bisogno di **tutto** lo stream, in continuo. Perciò la lettura I2S a riposo è
**unificata**: nel `loop()` un solo `micReadChunk()` produce il chunk che alimenta sia
`wakeFeed()` sia il livello dell'anello LED (`micLevelFromChunk`). Due `i2s_read`
separati si ruberebbero i campioni a vicenda e il modello perderebbe metà audio. Con
`WAKE_ENABLE 0` resta la vecchia strada, `micPeekLevel()` per il solo anello reattivo.

**A fine conversazione** servono `micFlush()` + `wakeReset()`: nel buffer c'è ancora
l'audio di pochi secondi prima (inclusa la voce di Alexo dall'altoparlante) e senza
svuotarlo il wake **riparte da solo** su audio stantio.

## Come si sceglie la parola

"Okay Nabu" è un modello **pre-addestrato** scaricabile già pronto, e questo è il motivo
principale per cui è quello in uso: è preciso e non costa niente in tempo.

Provati e scartati, per ragioni istruttive:

- **"hey jarvis"** — modello inglese: con la pronuncia italiana veniva colto raramente.
- **"alexa"** — scelto per assonanza con "Alexo", ma scattava su **qualsiasi** parola
  contenente "-xa". Una wake word troppo corta e troppo comune fa più danni che comodità.

### Se vuoi una parola tutta tua

È il passo rimasto aperto. Si allena un modello proprio con il notebook Colab di
**microWakeWord**: non servono registrazioni, i campioni vocali sono sintetici (TTS), e
l'operazione dura orientativamente mezz'ora-un'ora. Poi:

1. Esporta il `.tflite` INT8 e prendi nota dei parametri del **manifest**.
2. Converti in array C con `xxd -i`, mantenendo il simbolo `g_wake_model`.
3. Sostituisci `src/wake_model.h` (è un rimpiazzo diretto, niente altro da toccare).
4. Aggiorna `WAKE_PROB_CUTOFF` e `WAKE_WINDOW` in `config.h` coi valori del manifest
   (`probability_cutoff` × 255 e `sliding_window_size`).

## Taratura e strumenti di diagnosi

Tre flag in `config.h`, ognuno lascia l'aggiornamento OTA attivo:

| Flag | A cosa serve |
| --- | --- |
| `MIC_DIAG` | livelli di rumore del microfono: per scegliere il guadagno |
| `TFL_SELFTEST` | il runtime TFLite Micro funziona? (modellino `hello_world`) |
| `WAKE_TEST` | gira la catena su audio sintetico, **senza microfono**: valida frontend e modello e mostra i falsi positivi prima della prova dal vivo |

Tre lezioni pagate durante la taratura:

- **Il flusso dev'essere continuo.** Il modello è streaming: una pausa fra le letture e
  non rileva più niente. Non inserire attese nel percorso del wake.
- **Troppo guadagno peggiora.** Alzare `WAKE_GAIN` sembra la cura ovvia quando "non
  sente", ma oltre un certo punto il segnale **satura** e il riconoscimento cala.
- **L'audio stantio fa ripartire il wake da solo.** Vedi `micFlush()`/`wakeReset()` sopra.

## Fonti

- microWakeWord: <https://microwakeword.com/> (training: <https://microwakeword.com/train>)
- Repo di training: <https://github.com/OHF-Voice/micro-wake-word>
- Modelli pronti: <https://github.com/esphome/micro-wake-word-models>
- `esp-tflite-micro`: <https://github.com/espressif/esp-tflite-micro>
- ESPHome `micro_wake_word`: <https://esphome.io/components/micro_wake_word/>
- Guida pratica ESP32-S3 + TFLM: <https://dev.to/zediot/esp32-s3-tensorflow-lite-micro-a-practical-guide-to-local-wake-word-edge-ai-inference-5540>
