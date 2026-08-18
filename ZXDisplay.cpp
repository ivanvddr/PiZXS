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

#include "ZXDisplay.h"

ZXDisplay zxDisplay;

// Palette ZX Spectrum esatta in RGB565.
// Ordine indici 0-7: normali (Nero,Blu,Rosso,Magenta,Verde,Ciano,Giallo,Bianco)
// Ordine indici 8-15: bright (stessa sequenza, componenti a 0xFF)
static const uint16_t ZX_PALETTE[16] = {
    0x0000, 0x0019, 0xC800, 0xC819, 0x0660, 0x0679, 0xCE60, 0xCE79,
    0x0000, 0x001F, 0xF800, 0xF81F, 0x07E0, 0x07FF, 0xFFE0, 0xFFFF
};

void ZXDisplay::begin() {
    readyFlag[0] = readyFlag[1] = false;
    busyFlag[0]  = busyFlag[1]  = false;
    tft.init();
    tft.setRotation(1);
    tft.initDMA();
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);
}

// ---------------------------------------------------------------------------
// core0: beginFrame — attende che il buffer di scrittura sia libero
// ---------------------------------------------------------------------------
void ZXDisplay::beginFrame() {
    while (busyFlag[writeIdx]) { /* busy-wait, raro */ }
}

// ---------------------------------------------------------------------------
// core0: decodeScanline — decodifica una riga in 4bpp
// Chiamata con 'line' da 0 a 191 in ordine crescente, durante runFrame().
// La ULA scansiona una riga ogni 224 T-state a partire da T=14336.
// ---------------------------------------------------------------------------
void ZXDisplay::decodeScanline(int line, const uint8_t* ram,
                                uint8_t border, bool flashInvert)
{
    // Indirizzo bitmap nella RAM Spectrum (formula ULA standard):
    //   bits 13-12 = third (0,1,2)    <- y & 0xC0
    //   bits 10-8  = scan line in char <- y & 0x07
    //   bits  7-5  = char row in third <- y & 0x38
    //   bits  4-0  = char column       <- 0..31
    int bitmapRowBase = ((line & 0xC0) << 5)
                      | ((line & 0x07) << 8)
                      | ((line & 0x38) << 2);
    int attrRowBase   = 6144 + (line >> 3) * 32;

    uint8_t* outRow = &fb4[writeIdx][line * (ZX_SCREEN_W / 2)];

    for (int cx = 0; cx < 32; cx++) {
        uint8_t byteVal = ram[bitmapRowBase + cx];
        uint8_t attr    = ram[attrRowBase   + cx];

        uint8_t ink    =  attr & 0x07;
        uint8_t paper  = (attr >> 3) & 0x07;
        uint8_t bright = (attr >> 6) & 0x01;
        uint8_t flash  = (attr >> 7) & 0x01;

        uint8_t inkIdx   = (bright ? 8 : 0) + ink;
        uint8_t paperIdx = (bright ? 8 : 0) + paper;

        if (flash && flashInvert) {
            uint8_t t = inkIdx; inkIdx = paperIdx; paperIdx = t;
        }

        // Pacchetto 4bpp: nibble basso = pixel sinistro, nibble alto = pixel destro
        uint8_t* out = &outRow[cx * 4];
        for (int b = 0; b < 8; b += 2) {
            uint8_t p0 = (byteVal & (0x80 >> b))       ? inkIdx : paperIdx;
            uint8_t p1 = (byteVal & (0x80 >> (b + 1))) ? inkIdx : paperIdx;
            out[b >> 1] = p0 | (p1 << 4);
        }
    }
}

// ---------------------------------------------------------------------------
// core0: commitFrame — marca il buffer pronto per core1
// ---------------------------------------------------------------------------
void ZXDisplay::commitFrame(uint8_t border) {
    borderBuf[writeIdx] = border;
    readyFlag[writeIdx] = true;
    writeIdx ^= 1;
}

// ---------------------------------------------------------------------------
// core1: task — se c'e' un frame pronto, lo disegna via DMA
// ---------------------------------------------------------------------------
void ZXDisplay::task() {
    for (int i = 0; i < 2; i++) {
        if (readyFlag[i] && !busyFlag[i]) {
            readyFlag[i] = false;
            busyFlag[i]  = true;
            expandAndPush(i);
            busyFlag[i]  = false;
            return;
        }
    }
}

void ZXDisplay::expandAndPush(uint8_t idx) {
    uint16_t borderColor565 = ZX_PALETTE[borderBuf[idx] & 0x07];

    tft.startWrite();

    // Border (4 rettangoli solidi attorno all'area 256x192)
    tft.fillRect(0,              0,           320,      OFFSET_Y,     borderColor565);
    tft.fillRect(0,              240-OFFSET_Y,320,      OFFSET_Y,     borderColor565);
    tft.fillRect(0,              OFFSET_Y,    OFFSET_X, ZX_SCREEN_H,  borderColor565);
    tft.fillRect(320-OFFSET_X,  OFFSET_Y,    OFFSET_X, ZX_SCREEN_H,  borderColor565);

    // Espansione 4bpp -> RGB565 a strisce, push DMA striscia per striscia.
    // Una striscia = ZX_STRIP_LINES righe. Riusiamo lo stesso stripBuf:
    // dmaWait() garantisce che il DMA abbia finito prima di sovrascriverlo.
    const uint8_t* src = fb4[idx];

    for (int strip = 0; strip < ZX_SCREEN_H / ZX_STRIP_LINES; strip++) {
        for (int ly = 0; ly < ZX_STRIP_LINES; ly++) {
            int y = strip * ZX_STRIP_LINES + ly;
            const uint8_t* rowBytes = &src[y * (ZX_SCREEN_W / 2)];
            uint16_t*      outRow   = &stripBuf[ly * ZX_SCREEN_W];

            for (int k = 0; k < ZX_SCREEN_W / 2; k++) {
                uint8_t b = rowBytes[k];
                outRow[k * 2]     = ZX_PALETTE[b & 0x0F];
                outRow[k * 2 + 1] = ZX_PALETTE[(b >> 4) & 0x0F];
            }
        }

        tft.pushImageDMA(OFFSET_X,
                         OFFSET_Y + strip * ZX_STRIP_LINES,
                         ZX_SCREEN_W, ZX_STRIP_LINES,
                         &stripBuf[0]);
        tft.dmaWait();
    }

    tft.endWrite();
}