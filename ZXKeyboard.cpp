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

#include "ZXKeyboard.h"
#include "Spectrum.h"

ZXKeyboard zxKeyboard;

volatile uint8_t  ZXKeyboard::queueBuf[ZXKeyboard::QUEUE_SIZE];
volatile uint8_t  ZXKeyboard::queueHead = 0;
volatile uint8_t  ZXKeyboard::queueTail = 0;
volatile uint32_t ZXKeyboard::shiftReg = 0;
volatile uint8_t  ZXKeyboard::bitCount = 0;

void ZXKeyboard::begin() {
    pinMode(PIN_PS2_DATA, INPUT_PULLUP);
    pinMode(PIN_PS2_CLOCK, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_PS2_CLOCK), ZXKeyboard::clockISR, FALLING);
}

void ZXKeyboard::clockISR() {
    int dataBit = digitalRead(PIN_PS2_DATA);
    shiftReg |= ((uint32_t)dataBit << bitCount);
    bitCount++;
    if (bitCount >= 11) {
        uint8_t data = (shiftReg >> 1) & 0xFF;
        uint8_t next = (queueHead + 1) % QUEUE_SIZE;
        if (next != queueTail) {
            queueBuf[queueHead] = data;
            queueHead = next;
        }
        shiftReg = 0;
        bitCount = 0;
    }
}

static int mapScancode(uint8_t code, bool extended, bool shift,
                        int8_t& row1, int8_t& bit1,
                        int8_t& row2, int8_t& bit2) {
    row2 = -1; bit2 = -1;

    if (extended) {
        switch (code) {
            case 0x75: row1=0; bit1=0; row2=4; bit2=3; return 2; // UP
            case 0x72: row1=0; bit1=0; row2=4; bit2=4; return 2; // DOWN
            case 0x6B: row1=0; bit1=0; row2=3; bit2=4; return 2; // LEFT
            case 0x74: row1=0; bit1=0; row2=4; bit2=2; return 2; // RIGHT
        }
        return 0;
    }

    switch (code) {
        case 0x1C: row1=1; bit1=0; return 1; // A
        case 0x32: row1=7; bit1=4; return 1; // B
        case 0x21: row1=0; bit1=3; return 1; // C
        case 0x23: row1=1; bit1=2; return 1; // D
        case 0x24: row1=2; bit1=2; return 1; // E
        case 0x2B: row1=1; bit1=3; return 1; // F
        case 0x34: row1=1; bit1=4; return 1; // G
        case 0x33: row1=6; bit1=4; return 1; // H
        case 0x43: row1=5; bit1=2; return 1; // I
        case 0x3B: row1=6; bit1=3; return 1; // J
        case 0x42: row1=6; bit1=2; return 1; // K
        case 0x4B: row1=6; bit1=1; return 1; // L
        case 0x3A: row1=7; bit1=2; return 1; // M
        case 0x31: row1=7; bit1=3; return 1; // N
        case 0x44: row1=5; bit1=1; return 1; // O
        case 0x4D: row1=5; bit1=0; return 1; // P
        case 0x15: row1=2; bit1=0; return 1; // Q
        case 0x2D: row1=2; bit1=3; return 1; // R
        case 0x1B: row1=1; bit1=1; return 1; // S
        case 0x2C: row1=2; bit1=4; return 1; // T
        case 0x3C: row1=5; bit1=3; return 1; // U
        case 0x2A: row1=0; bit1=4; return 1; // V
        case 0x1D: row1=2; bit1=1; return 1; // W
        case 0x22: row1=0; bit1=2; return 1; // X
        case 0x35: row1=5; bit1=4; return 1; // Y
        case 0x1A: row1=0; bit1=1; return 1; // Z

        // Numeri e simboli Shift+Numero (Layout US)
        case 0x45: // 0 e )
            if (shift) { row1=7; bit1=1; row2=4; bit2=1; return 2; } // ) -> SYMBOL + 9
            row1=4; bit1=0; return 1; // 0
        case 0x16: // 1 e !
            if (shift) { row1=7; bit1=1; row2=3; bit2=0; return 2; } // ! -> SYMBOL + 1
            row1=3; bit1=0; return 1; // 1
        case 0x1E: // 2 e @
            if (shift) { row1=7; bit1=1; row2=3; bit2=1; return 2; } // @ -> SYMBOL + 2
            row1=3; bit1=1; return 1; // 2
        case 0x26: // 3 e #
            if (shift) { row1=7; bit1=1; row2=3; bit2=2; return 2; } // # -> SYMBOL + 3
            row1=3; bit1=2; return 1; // 3
        case 0x25: // 4 e $             if (shift) { row1=7; bit1=1; row2=3; bit2=3; return 2; } // $ -> SYMBOL + 4
            row1=3; bit1=3; return 1; // 4
        case 0x2E: // 5 e %
            if (shift) { row1=7; bit1=1; row2=3; bit2=4; return 2; } // % -> SYMBOL + 5
            row1=3; bit1=4; return 1; // 5
        case 0x36: // 6 e ^
            if (shift) { row1=7; bit1=1; row2=4; bit2=4; return 2; } // & -> SYMBOL + 6 (Mappato su &)
            row1=4; bit1=4; return 1; // 6
        case 0x3D: // 7 e &
            if (shift) { row1=7; bit1=1; row2=4; bit2=3; return 2; } // ' -> SYMBOL + 7
            row1=4; bit1=3; return 1; // 7
        case 0x3E: // 8 e *
            if (shift) { row1=7; bit1=1; row2=7; bit2=4; return 2; } // * -> SYMBOL + B
            row1=4; bit1=2; return 1; // 8
        case 0x46: // 9 e (
            if (shift) { row1=7; bit1=1; row2=4; bit2=2; return 2; } // ( -> SYMBOL + 8
            row1=4; bit1=1; return 1; // 9

        // Simboli dedicati sulla riga superiore
        case 0x55: // = e +
            if (shift) { row1=7; bit1=1; row2=6; bit2=2; return 2; } // + -> SYMBOL + K
            row1=7; bit1=1; row2=6; bit2=1; return 2; // = -> SYMBOL + L
        case 0x4E: // - e _
            if (shift) { row1=7; bit1=1; row2=4; bit2=0; return 2; } // _ -> SYMBOL + 0
            row1=7; bit1=1; row2=6; bit2=3; return 2; // - -> SYMBOL + J

        // Simboli sul lato destro
        case 0x4A: // / e ?
            if (shift) { row1=7; bit1=1; row2=0; bit2=4; return 2; } // ? -> SYMBOL + V (Mappato su /)
            row1=7; bit1=1; row2=0; bit2=4; return 2; // / -> SYMBOL + V
        case 0x52: // ' e "
            if (shift) { row1=7; bit1=1; row2=5; bit2=0; return 2; } // " -> SYMBOL + P
            row1=7; bit1=1; row2=4; bit2=3; return 2; // ' -> SYMBOL + 7
        case 0x41: // , e <
            if (shift) { row1=7; bit1=1; row2=2; bit2=3; return 2; } // < -> SYMBOL + R
            row1=7; bit1=1; row2=7; bit2=3; return 2; // , -> SYMBOL + N
        case 0x49: // . e >
            if (shift) { row1=7; bit1=1; row2=2; bit2=4; return 2; } // > -> SYMBOL + T
            row1=7; bit1=1; row2=7; bit2=2; return 2; // . -> SYMBOL + M
        case 0x4C: // ; e :
            if (shift) { row1=7; bit1=1; row2=0; bit2=1; return 2; } // : -> SYMBOL + Z
            row1=7; bit1=1; row2=5; bit2=1; return 2; // ; -> SYMBOL + O

        case 0x5A: row1=6; bit1=0; return 1; // ENTER
        case 0x29: row1=7; bit1=0; return 1; // SPACE
        case 0x66: row1=0; bit1=0; row2=4; bit2=0; return 2; // BACKSPACE -> CS+0
    }
    return 0;
}

void ZXKeyboard::poll() {
    while (queueTail != queueHead) {
        uint8_t b = queueBuf[queueTail];
        queueTail = (queueTail + 1) % QUEUE_SIZE;

        if (b == 0xE0) { extendedPrefix = true; continue; }
        if (b == 0xF0) { breakPrefix = true; continue; }

        bool pressed = !breakPrefix;

        // Intercetta stati di Shift e Ctrl per gestire le combinazioni
        if (!extendedPrefix) {
            if (b == 0x12 || b == 0x59) {
                ps2Shift = pressed;
                spectrum.setKey(0, 0, pressed); // CAPS SHIFT diretto
                extendedPrefix = false; breakPrefix = false;
                continue;
            }
            if (b == 0x14) {
                ps2Ctrl = pressed;
                spectrum.setKey(7, 1, pressed); // SYMBOL SHIFT diretto
                extendedPrefix = false; breakPrefix = false;
                continue;
            }
        }

        int8_t r1, c1, r2, c2;
        int n = mapScancode(b, extendedPrefix, ps2Shift, r1, c1, r2, c2);

        // Controlla se stiamo iniettando una combinazione di SYMBOL SHIFT
        bool isSymbolCombo = (n == 2 && r1 == 7 && c1 == 1);
        
        // Se Shift è premuto ma Ctrl non lo è, dobbiamo "sopprimere" il CAPS SHIFT 
        // per evitare il BREAK (CAPS+SYMBOL) durante la digitazione di un simbolo.
        bool suppressCaps = isSymbolCombo && ps2Shift && !ps2Ctrl;

        if (suppressCaps) {
            spectrum.setKey(0, 0, false); // Rilascia momentaneamente CAPS SHIFT
        }

        if (n >= 1) spectrum.setKey(r1, c1, pressed);
        if (n >= 2) spectrum.setKey(r2, c2, pressed);

        if (suppressCaps) {
            spectrum.setKey(0, 0, true); // Ripremi CAPS SHIFT se era ancora tenuto giù
        }

        extendedPrefix = false;
        breakPrefix = false;
    }
}