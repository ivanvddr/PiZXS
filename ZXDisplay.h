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
 * ZXDisplay.h
 * Rendering scanline-accurate dello ZX Spectrum 48K.
 *
 * Modello reale della ULA:
 *   T=0        : INT fires, ISR gira (cursore, tastiera) ~ 500 T-state
 *   T=14336    : inizio scansione attiva (linea 0 di 192)
 *   T=14336+224*line : inizio scansione della linea 'line'
 *   T=57344    : fine scansione attiva (fine linea 191)
 *   T=57344..69888 : VBlank, ROM fa listing/scroll/aggiornamenti
 *
 * Questo driver replica esattamente questo comportamento:
 *   core0 (dentro Spectrum::runFrame()) decodifica UNA RIGA ogni 224 T-state
 *   nel buffer di scrittura 4bpp. Quando l'ISR ha gia' girato (T>~500) ma
 *   il listing non e' ancora iniziato (T<14336), lo stato della RAM e' stabile
 *   -> nessun cursore spurio nel listing.
 *
 * Protocollo double-buffer 4bpp (24KB cadauno):
 *   core0: beginFrame() -> decodeScanline() x192 -> commitFrame()
 *   core1: task() -> se readyFlag: espande via LUT e push DMA
 *
 * La separazione e' safe: core0 scrive SEMPRE nel buffer non-busy,
 * core1 legge SEMPRE dal buffer committed. Non servono mutex.
 */

#ifndef ZX_DISPLAY_H
#define ZX_DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>

#define ZX_SCREEN_W    256
#define ZX_SCREEN_H    192
#define ZX_FB_BYTES    (ZX_SCREEN_W * ZX_SCREEN_H / 2)   // 4bpp = 24576 byte
#define ZX_STRIP_LINES 24                                  // 192 / 8 strip esatte

extern TFT_eSPI tft;

class ZXDisplay {
public:
    // Da chiamare da core1 (setup1())
    void begin();

    // Da chiamare da core0 PRIMA di spectrum.runFrame():
    // si blocca se il buffer di scrittura e' ancora busy (core1 lo sta
    // pushando). In condizioni normali l'attesa e' 0: il push DMA ~2.5ms
    // e' molto piu' breve del frame ~20ms.
    void beginFrame();

    // Da chiamare da core0 DURANTE runFrame(), una volta per ogni riga
    // (line 0..191) nell'ordine esatto in cui la ULA le scansiona.
    // Decodifica bitmap+attributi ZX in indici palette 4bpp nel buffer
    // di scrittura corrente.
    void decodeScanline(int line, const uint8_t* ram,
                        uint8_t border, bool flashInvert);

    // Da chiamare da core0 DOPO le 192 righe: marca il buffer come pronto
    // per core1 e ruota l'indice di scrittura.
    void commitFrame(uint8_t border);

    // Da chiamare nel loop di core1: se c'e' un frame pronto, lo espande
    // via LUT di palette e lo invia al display via DMA a strisce.
    void task();

private:

    uint8_t  fb4[2][ZX_FB_BYTES];     // due framebuffer 4bpp (24KB cad.)
    uint8_t  borderBuf[2];            // border color per ogni buffer

    volatile bool readyFlag[2];       // core0->core1: "pronto da disegnare"
    volatile bool busyFlag[2];        // core1->core0: "sto disegnando"

    uint8_t writeIdx = 0;

    // Striscia RGB565 temporanea per il push DMA (ZX_STRIP_LINES righe)
    uint16_t stripBuf[ZX_SCREEN_W * ZX_STRIP_LINES];

    static const int OFFSET_X = (320 - ZX_SCREEN_W) / 2;  // 32px
    static const int OFFSET_Y = (240 - ZX_SCREEN_H) / 2;  // 24px

    void expandAndPush(uint8_t idx);
};

extern ZXDisplay zxDisplay;

#endif // ZX_DISPLAY_H