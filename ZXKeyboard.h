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
 * ZXKeyboard.h
 * Tastiera PS/2 (set di scancode "Set 2", lo standard) -> matrice ZX Spectrum.
 *
 * Driver minimale a interrupt sul fronte di discesa del CLOCK. 
 */

#ifndef ZX_KEYBOARD_H
#define ZX_KEYBOARD_H

#include <Arduino.h>
#include "ZXConfig.h"

class ZXKeyboard {
public:
    void begin();   // da chiamare da core0 (setup()), attiva l'interrupt sul pin CLOCK
    void poll();    // da chiamare nel loop di core0: svuota la coda di scancode e
                    // aggiorna la matrice tastiera dello Spectrum (spectrum.setKey)

private:
    static void clockISR();

    static const uint8_t QUEUE_SIZE = 32;
    static volatile uint8_t queueBuf[QUEUE_SIZE];
    static volatile uint8_t queueHead;
    static volatile uint8_t queueTail;

    static volatile uint32_t shiftReg;
    static volatile uint8_t  bitCount;

    bool extendedPrefix = false;  // ultimo byte era 0xE0 (tasti estesi: freccette, ecc.)
    bool breakPrefix    = false;  // ultimo byte era 0xF0 (rilascio tasto)

    bool ps2Shift = false;
    bool ps2Ctrl = false;

};

extern ZXKeyboard zxKeyboard;

#endif // ZX_KEYBOARD_H