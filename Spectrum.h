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
 * Spectrum.h
 * Emulazione ULA ZX Spectrum 48K: memoria, I/O, tastiera, Kempston,
 * beeper event-driven, rendering scanline-accurate integrato.
 *
 * runFrame() esegue un frame intero (69888 T-state) e, durante l'esecuzione,
 * chiama zxDisplay.decodeScanline() per ogni riga dello schermo nel momento
 * esatto in cui la ULA reale avrebbe scansionato quella riga (T=14336 + line*224).
 * Questo replica fedelmente il comportamento hardware eliminando i cursori
 * spurii nel BASIC e gli artefatti nel listing.
 *
 * La classe Z80 deve essere quella di PiMSX patchata secondo Z80_PATCH.md
 * (IO callback a 16 bit di porta).
 */

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <Arduino.h>
#include "Z80.h"
#include "ZXConfig.h"

class Spectrum {
public:
    Spectrum();

    // Carica la ROM da LittleFS ("/rom48.rom") e inizializza la CPU.
    bool begin();

    void resetMachine();

    // Esegue un frame completo (69888 T-state) con rendering scanline-accurate.
    // Deve essere preceduto da zxDisplay.beginFrame() nel loop principale.
    void runFrame();

    // Callbacks statiche richieste dalla classe Z80
    static uint8_t memRead (uint16_t addr);
    static void    memWrite(uint16_t addr, uint8_t val);
    static uint8_t ioRead  (uint16_t port);
    static void    ioWrite (uint16_t port, uint8_t val);

    // Tastiera: riga 0..7, bit 0..4 (bit attivo basso)
    void setKey(uint8_t row, uint8_t bit, bool pressed);

    // Joystick Kempston: bit0=right,1=left,2=down,3=up,4=fire
    void setKempston(uint8_t mask);

    // Accessori per diagnostica / snapshot loader futuro
    const uint8_t* screenMemory() const { return ram; }
    uint8_t        getBorder()    const { return borderColor; }
    uint32_t       getFrameCounter() const { return frameCounter; }

    // Interfaccia audio per ZXAudio (IRQ DMA su core1)
    int acquireReadyAudioBuffer();
    const uint16_t* audioBuffer(int idx) const { return audioBuf[idx]; }
    static const int AUDIO_BUFFER_LEN = AUDIO_SAMPLES_PER_FRAME;

    Z80      cpu;
    uint8_t  ram[49152];   // pubblico per accesso diretto da decodeScanline

    void setBorder(uint8_t color) { borderColor = color & 0x07; }

private:
    uint8_t  romBuf[16384];

    uint8_t  keyboardMatrix[8];
    uint8_t  kempstonState;

    uint8_t  borderColor;
    uint32_t frameCounter;

    uint32_t frameStartCycles;
    int      lastEdgeTstate;
    uint8_t  lastBeeperLevel;

    uint16_t audioBuf[2][AUDIO_SAMPLES_PER_FRAME];
    volatile uint8_t audioWriteIdx;
    volatile bool    audioReady[2];

    void generateAudioUpTo(int tstateRelative);
};

extern Spectrum spectrum;

#endif // SPECTRUM_H