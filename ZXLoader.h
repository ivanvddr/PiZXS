/*
 * Questo file fa parte del progetto PiZXS.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 *
 * ZXLoader.h — Loader unificato .z80 / .tap per PiZXS (ZX Spectrum 48K su RP2040)
 *
 * La gestione dei .tap non viene integrata nell'emulatore (non testata)
 *
 * ============================================================
 * ZXLoader — Caricamento trasparente di snapshot (.z80) e nastri (.tap)
 * ============================================================
 *
 * L'utente seleziona un file (.z80 o .tap): il loader riconosce il
 * tipo dall'estensione e lo avvia nel modo corretto senza che serva
 * distinguere manualmente:
 *
 *   .z80  → SNAPSHOT: stato CPU + RAM applicati direttamente,
 *           il programma riparte esattamente da dove fu salvato.
 *           (vedi la history precedente di questo file per i dettagli
 *           del parsing v1/v2/v3)
 *
 *   .tap  → NASTRO: la macchina viene resettata alla BASIC pulita,
 *           il loader digita da solo "LOAD ""[ENTER]" simulando la
 *           tastiera, e da quel momento intercetta la routine ROM
 *           LD-BYTES ($0556) per servire ogni blocco istantaneamente
 *           invece di emulare i T-state reali del segnale audio.
 *
 * ------------------------------------------------------------
 * FORMATO .TAP (riassunto)
 * ------------------------------------------------------------
 *
 * Sequenza di blocchi, ciascuno:
 *   [2 byte: lunghezza blocco N, little-endian]
 *   [N byte: flag(1) + dati(N-2) + checksum(1)]
 *
 * Non contiene informazioni di timing: è esattamente ciò che la
 * routine ROM standard LD-BYTES leggerebbe da un nastro reale.
 * Per questo il trap su LD-BYTES copre il 100% dei file .tap validi
 * (a differenza del .tzx, che può contenere loader turbo custom).
 *
 * ---------------------------
 * TECNICA DEL TRAP LD-BYTES 
 * ---------------------------
 *
 * All'ingresso di $0556:
 *   A       = flag byte atteso dal chiamante (0x00 header, 0xFF dati)
 *   IX      = indirizzo di destinazione in RAM
 *   DE      = lunghezza attesa (byte dati, esclusi flag e checksum)
 *   Carry   = 1 per LOAD, 0 per VERIFY
 *
 * Il trap, invece di emulare gli impulsi audio:
 *   1. legge il blocco corrente del .tap (che DEVE combaciare in ordine
 *      con quello che il programma si aspetta, essendo il .tap una
 *      registrazione lineare della sessione originale),
 *   2. scrive i byte dati in RAM a partire da IX (o li confronta, se
 *      VERIFY), calcolando il checksum,
 *   3. simula il RET della routine (pop manuale di PC dallo stack),
 *      impostando Carry=1 se il checksum combacia, altrimenti Carry=0,
 *   4. avanza al blocco successivo per la prossima chiamata.
 *
 */

#ifndef ZXLOADER_H
#define ZXLOADER_H

#include <Arduino.h>
#include "LittleFS.h"
#include <SD.h>
#include "Z80.h"

// ============================================================================
// Filesystem
// ============================================================================
enum ZXFSType {
    ZXFS_NONE,
    ZXFS_SD,
    ZXFS_LITTLEFS
};

#ifndef SD_CS
#define SD_CS     13
#define SD_MISO   12
#define SD_MOSI   11
#define SD_SCLK   10
#endif

// ============================================================================
// Costanti ZX Spectrum 48K
// ============================================================================
#define ZX_RAM_BASE   0x4000u
#define ZX_RAM_SIZE   49152u

// Indirizzo della routine ROM LD-BYTES (48K), punto di aggancio del trap.
#define ZX_LD_BYTES_ADDR   0x0556u

// Tipo di media attualmente caricato
enum ZXMediaType {
    ZXMEDIA_NONE,
    ZXMEDIA_SNAPSHOT,   // .z80
    ZXMEDIA_TAPE        // .tap
};

// Stato snapshot risultante dal parsing dell'header .z80, da applicare alla CPU
struct ZXSnapshotState {
    uint8_t  A, F, B, C, D, E, H, L;
    uint8_t  A_, F_, B_, C_, D_, E_, H_, L_;
    uint16_t IX, IY, PC, SP;
    uint8_t  I, R;
    bool     IFF1, IFF2;
    uint8_t  IM;
    uint8_t  borderColor;
    bool     valid = false;
};

// Voce della tabella indice dei blocchi di un .tap (costruita una volta
// al caricamento, scorrendo il file per intero)
struct ZXTapBlockIndex {
    uint32_t fileOffset;   // offset nel file del PRIMO byte dati (dopo i 2 di lunghezza)
    uint16_t blockLen;     // lunghezza dichiarata nel .tap (flag + dati + checksum)
};

// Callback per iniettare una pressione/rilascio tasto nella matrice
// Spectrum, stessa firma di Spectrum::setKey(row, bit, pressed).
using ZXSetKeyFn = void (*)(uint8_t row, uint8_t bit, bool pressed);

// ============================================================================
// Classe ZXLoader
// ============================================================================
class ZXLoader {
public:
    explicit ZXLoader(uint8_t* ram, size_t ramSize);
    ~ZXLoader() { scaricaROM(); }

    // -----------------------------------------------------------------------
    // Filesystem
    // -----------------------------------------------------------------------
    bool initFilesystem(ZXFSType preferredFS = ZXFS_SD);
    ZXFSType getCurrentFilesystem() const { return _currentFS; }
    bool isFilesystemReady() const { return _filesystemReady; }
    File openFile(const char* path, const char* mode = "r");
    bool fileExists(const char* path);
    void closeFilesystem();

    // -----------------------------------------------------------------------
    // Selezione e caricamento — rileva .z80 vs .tap dall'estensione
    // -----------------------------------------------------------------------
    void setSelectedROM(const char* path);
    const char* getSelectedROM() const { return _selectedROMPath; }

    bool caricaROMSelezionata();
    bool caricaROM(const char* path);
    void scaricaROM();

    ZXMediaType getTipoMedia() const { return _mediaType; }

    // -----------------------------------------------------------------------
    // Avvio trasparente: fa la cosa giusta a seconda del tipo di file.
    //   SNAPSHOT → applica subito lo stato CPU (ex applicaStatoCPU)
    //   TAPE     → richiede che la macchina sia già stata resettata
    //              (spectrum.resetMachine() PRIMA di chiamare avvia()),
    //              poi mette in coda l'autotype di LOAD ""+ENTER
    //
    // borderOut, se non nullptr e si tratta di uno SNAPSHOT, riceve il
    // colore di bordo salvato (da applicare con spectrum.setBorder()).
    // Per un TAPE non viene toccato (resta quello impostato dal reset).
    // -----------------------------------------------------------------------
    bool avvia(Z80& cpu, ZXSetKeyFn setKey, uint8_t* borderOut = nullptr);

    // Da chiamare OGNI FRAME dal loop principale. Non fa nulla se il
    // media corrente non è un nastro attivo (safe da chiamare sempre).
    // Si occupa di:
    //   1. far avanzare la sequenza di autotype "LOAD """ se ancora attiva
    //   2. intercettare LD-BYTES ($0556) e servire il blocco corrente
    void aggiornaNastro(Z80& cpu, uint8_t* ram, ZXSetKeyFn setKey);

    bool isAutoTypeActive() const { return _autoTypeStep < _autoTypeStepCount; }
    bool isTapeFinished() const { return _tapeBlockIndex >= _tapeBlockCount; }

    // -----------------------------------------------------------------------
    // Getters stato (validi per lo SNAPSHOT più recente caricato)
    // -----------------------------------------------------------------------
    bool isROMCaricata() const { return _romCaricata; }
    size_t getROMSize() const { return _romSize; }
    const char* getROMName() const { return _romName; }
    uint8_t getSnapshotVersion() const { return _snapVersion; }
    const ZXSnapshotState& getSnapshotState() const { return _state; }

    // Applica lo stato dello snapshot alla CPU (usato internamente da
    // avvia(), esposto anche a parte per compatibilità/debug).
    bool applicaStatoCPU(Z80& cpu, uint8_t* borderOut = nullptr) const;

    // -----------------------------------------------------------------------
    // Utilità / debug
    // -----------------------------------------------------------------------
    void elencaFileROM(const char* cartella = "/PiSMS/games");
    void debugStatoCPU() const;
    void debugRAM(uint16_t inizioZ80, uint16_t fineZ80) const;
    void debugTape() const;

private:
    // -----------------------------------------------------------------------
    // Buffer / stato generale
    // -----------------------------------------------------------------------
    uint8_t* _ram;
    size_t   _ramSize;

    bool     _romCaricata = false;
    size_t   _romSize = 0;
    char     _romName[64] = {0};
    char     _romPath[128] = {0};

    ZXMediaType _mediaType = ZXMEDIA_NONE;

    // -----------------------------------------------------------------------
    // Stato SNAPSHOT (.z80)
    // -----------------------------------------------------------------------
    uint8_t  _snapVersion = 0;
    ZXSnapshotState _state;

    bool leggiHeaderBase(File& f, uint8_t rawHeader[30]);
    bool caricaV1(File& f, bool compresso);
    bool caricaV2V3(File& f, const uint8_t rawHeader[30]);
    bool caricaBloccoPagina(File& f, uint16_t lunghezzaCompressa, uint8_t numPagina);
    bool decomprimiRLE(File& f, uint8_t* dest, size_t destLen);
    bool copiaRaw(File& f, uint8_t* dest, size_t lunghezza);

    inline uint8_t* ramPtr(uint16_t z80Addr) const {
        if (z80Addr < ZX_RAM_BASE) return nullptr;
        size_t off = (size_t)(z80Addr - ZX_RAM_BASE);
        if (off >= _ramSize) return nullptr;
        return &_ram[off];
    }

    // -----------------------------------------------------------------------
    // Stato TAPE (.tap)
    // -----------------------------------------------------------------------
    File _tapeFile;
    bool _tapeFileOpen = false;

    ZXTapBlockIndex* _tapeBlockTable = nullptr;
    uint16_t _tapeBlockCount = 0;
    uint16_t _tapeBlockIndex = 0;   // prossimo blocco da servire

    bool indicizzaTap(const char* path);   // scorre il file una volta, costruisce _tapeBlockTable
    void chiudiTape();

    // Servizio del trap LD-BYTES per il blocco corrente. Ritorna false
    // se non c'era nulla da servire (nastro finito o non attivo).
    bool servizioLDBytes(Z80& cpu, uint8_t* ram);

    // -----------------------------------------------------------------------
    // Autotype (digitazione automatica di "LOAD """ + ENTER)
    // -----------------------------------------------------------------------
    struct ZXAutoTypeStep {
        int8_t row1, bit1;   // prima tasto del "chord" (-1 = nessuno)
        int8_t row2, bit2;   // secondo tasto del "chord", per combinazioni (-1 = nessuno)
    };

    static const uint8_t AUTOTYPE_MAX_STEPS = 8;
    ZXAutoTypeStep _autoTypeSteps[AUTOTYPE_MAX_STEPS];
    uint8_t  _autoTypeStepCount = 0;
    uint8_t  _autoTypeStep = 0;      // indice dello step corrente (== count quando finito)
    uint16_t _autoTypeFrameCounter = 0;
    bool     _autoTypeKeyDown = false;
    uint16_t _autoTypeDelayFrames = 0; // attesa iniziale prima di iniziare a digitare

    static const uint16_t AUTOTYPE_HOLD_FRAMES = 6;   // ~120ms a 50Hz
    static const uint16_t AUTOTYPE_GAP_FRAMES  = 6;
    static const uint16_t AUTOTYPE_BOOT_DELAY_FRAMES = 25; // ~0.5s dopo il reset

    void preparaAutoTypeLoad();
    void aggiornaAutoType(ZXSetKeyFn setKey);

    // -----------------------------------------------------------------------
    // Comune
    // -----------------------------------------------------------------------
    ZXFSType _currentFS = ZXFS_NONE;
    bool     _filesystemReady = false;
    char     _selectedROMPath[128] = {0};

    bool initSD();
    void estraiNomeROM(const char* path);
    bool haEstensione(const char* path, const char* ext) const;
};

#endif // ZXLOADER_H
