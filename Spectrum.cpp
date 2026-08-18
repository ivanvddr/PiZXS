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
 */

#include "Spectrum.h"
#include "ZXDisplay.h"   // necessario per decodeScanline/commitFrame dentro runFrame()
#include <LittleFS.h>

Spectrum spectrum;

// ---------------------------------------------------------------------------
// Timing ULA (tutti i valori in T-state, riferimento: inizio frame = T=0)
// ---------------------------------------------------------------------------
// T=0         : INT generato dalla ULA
// T=0..14335  : top border (64 linee × 224 T-state)
// T=14336     : inizio display attivo (linea 0)
// T=14336+224 : linea 1, ecc.
// T=57343     : fine display attivo (fine linea 191)
// T=57344..69887 : bottom border + VBlank (ROM aggiorna listing/scroll)

static const int ULA_DISPLAY_START  = 14336;
static const int ULA_LINE_TSTATES   = 224;

Spectrum::Spectrum() {
    memset(ram, 0, sizeof(ram));
    memset(romBuf, 0, sizeof(romBuf));
    for (int i = 0; i < 8; i++) keyboardMatrix[i] = 0xFF;
    kempstonState  = 0;
    borderColor    = 7;
    frameCounter   = 0;
    frameStartCycles = 0;
    lastEdgeTstate = 0;
    lastBeeperLevel = 0;
    audioWriteIdx  = 0;
    audioReady[0]  = audioReady[1] = false;
}

bool Spectrum::begin() {
    if (!LittleFS.begin()) {
        Serial.println("[Spectrum] ERRORE: LittleFS.begin() fallito");
        return false;
    }
    File f = LittleFS.open("/rom48.rom", "r");
    if (!f) {
        Serial.println("[Spectrum] ERRORE: /rom48.rom non trovata in LittleFS.");
        return false;
    }
    size_t n = f.read(romBuf, sizeof(romBuf));
    f.close();
    if (n != sizeof(romBuf))
        Serial.printf("[Spectrum] ROM parziale (%u/%u byte)\n",
                      (unsigned)n, (unsigned)sizeof(romBuf));

    cpu.begin(Spectrum::memRead, Spectrum::memWrite,
              Spectrum::ioRead,  Spectrum::ioWrite);
    resetMachine();
    return true;
}

void Spectrum::resetMachine() {
    cpu.reset();
    cpu.setInterruptMode(1);
    memset(ram, 0, sizeof(ram));
    borderColor  = 7;
    frameCounter = 0;
}

// ---------------------------------------------------------------------------
// Callbacks memoria
// ---------------------------------------------------------------------------

uint8_t Spectrum::memRead(uint16_t addr) {
    if (addr < 0x4000) return spectrum.romBuf[addr];
    return spectrum.ram[addr - 0x4000];
}

void Spectrum::memWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x4000) return;
    spectrum.ram[addr - 0x4000] = val;
}

// ---------------------------------------------------------------------------
// Callbacks I/O
// ---------------------------------------------------------------------------

uint8_t Spectrum::ioRead(uint16_t port) {
    if ((port & 0x01) == 0) {
        // ULA (qualunque porta con bit0=0): bit 8-15 selezionano la riga
        uint8_t hi  = port >> 8;
        uint8_t row = 0x1F;
        for (int i = 0; i < 8; i++) {
            if (!(hi & (1 << i)))
                row &= (spectrum.keyboardMatrix[i] & 0x1F);
        }
        return row | 0xE0;   // bit7-5=1 (EAR alto = no cassetta)
    }
    if ((port & 0xFF) == 0x1F)
        return spectrum.kempstonState;  // Kempston

    return 0xFF;
}

void Spectrum::ioWrite(uint16_t port, uint8_t val) {
    if ((port & 0x01) == 0) {
        // ULA: bit2-0 = border, bit4 = beeper/MIC
        spectrum.borderColor = val & 0x07;
        uint8_t beeperBit    = (val >> 4) & 0x01;

        uint32_t nowAbs = spectrum.cpu.totalCycles;
        int nowRel = (int)(nowAbs - spectrum.frameStartCycles);
        if (nowRel < 0) nowRel = 0;
        if (nowRel > TSTATES_PER_FRAME) nowRel = TSTATES_PER_FRAME;

        spectrum.generateAudioUpTo(nowRel);
        spectrum.lastBeeperLevel = beeperBit;
    }
}

// ---------------------------------------------------------------------------
// Audio event-driven
// ---------------------------------------------------------------------------

void Spectrum::generateAudioUpTo(int tstateRelative) {
    int sFrom = (lastEdgeTstate * (int)AUDIO_SAMPLES_PER_FRAME) / TSTATES_PER_FRAME;
    int sTo   = (tstateRelative * (int)AUDIO_SAMPLES_PER_FRAME) / TSTATES_PER_FRAME;
    if (sTo > (int)AUDIO_SAMPLES_PER_FRAME) sTo = AUDIO_SAMPLES_PER_FRAME;

    uint16_t level = lastBeeperLevel ? 200u : 56u;
    for (int s = sFrom; s < sTo; s++)
        audioBuf[audioWriteIdx][s] = level;

    lastEdgeTstate = tstateRelative;
}

// ---------------------------------------------------------------------------
// runFrame — frame completo con rendering scanline-accurate
//
// La ULA reale scansiona le righe durante T=14336..57343 mentre la CPU gira.
// Qui faccio lo stesso: ogni volta che la CPU supera il T-state di inizio
// di una riga, decodifico subito quella riga nella RAM corrente.
// Risultato: lo snapshot e' quello che la ULA avrebbe "visto" esattamente
// in quel momento, eliminando i cursori spurii del listing BASIC.
//
// PREREQUISITO: zxDisplay.beginFrame() deve essere chiamato PRIMA di
// runFrame() nel loop principale (attende che il buffer di scrittura sia libero).
// ---------------------------------------------------------------------------

void Spectrum::runFrame() {
    frameStartCycles = cpu.totalCycles;
    lastEdgeTstate   = 0;

    // INT di fine quadro ULA (IM1 -> 0x0038), one-shot flag nella classe Z80
    cpu.triggerINT();

    const bool flashInvert = (frameCounter >> 4) & 1;

    int executed      = 0;
    int currentLine   = 0;
    int nextLineT     = ULA_DISPLAY_START;  // T-state della prossima riga da decodificare

    while (executed < (int)TSTATES_PER_FRAME) {

        int t = cpu.step();
        if (t <= 0) t = 4;  // guardia anti-freeze per HALT buggy
        executed += t;

        // Decodifica scanline nel momento esatto in cui la ULA le avrebbe lette.
        // Il while gestisce il caso (raro) in cui un'istruzione lunga supera
        // il threshold di piu' righe in un colpo.
        while (currentLine < ZX_SCREEN_H && executed >= nextLineT) {
            zxDisplay.decodeScanline(currentLine, ram, borderColor, flashInvert);
            currentLine++;
            nextLineT += ULA_LINE_TSTATES;
        }
    }

    // Safety: decodifica le righe residue (non dovrebbe mai accadere con
    // il timing corretto, ma protegge da istruzioni molto lunghe alla fine)
    while (currentLine < ZX_SCREEN_H) {
        zxDisplay.decodeScanline(currentLine, ram, borderColor, flashInvert);
        currentLine++;
    }

    // Commit del framebuffer: core1 puo' ora pusharlo al display
    zxDisplay.commitFrame(borderColor);

    // Chiude il buffer audio e lo rende disponibile per il DMA audio
    generateAudioUpTo(TSTATES_PER_FRAME);
    audioReady[audioWriteIdx] = true;
    audioWriteIdx ^= 1;

    frameCounter++;
}

// ---------------------------------------------------------------------------
// Audio buffer
// ---------------------------------------------------------------------------

int Spectrum::acquireReadyAudioBuffer() {
    for (int i = 0; i < 2; i++) {
        if (audioReady[i]) {
            audioReady[i] = false;
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Tastiera / Joystick
// ---------------------------------------------------------------------------

void Spectrum::setKey(uint8_t row, uint8_t bit, bool pressed) {
    if (row > 7 || bit > 4) return;
    if (pressed) keyboardMatrix[row] &= ~(1 << bit);
    else         keyboardMatrix[row] |=  (1 << bit);
}

void Spectrum::setKempston(uint8_t mask) {
    kempstonState = mask;
}
