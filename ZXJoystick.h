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
 * ZXJoystick.h
 * Joystick Kempston (porta 0x1F) pilotato da una semplice schedina
 * direzionale passiva a 5 vie + fire, contatti verso GND, pull-up interni.
 *
 * Formato Kempston (bit=1 significa "attivo"):
 *   bit0=RIGHT  bit1=LEFT  bit2=DOWN  bit3=UP  bit4=FIRE
 */

#ifndef ZX_JOYSTICK_H
#define ZX_JOYSTICK_H

#include <Arduino.h>
#include "ZXConfig.h"

class ZXJoystick {
public:
    void begin();   // da chiamare da core0 (setup())
    void poll();    // da chiamare nel loop di core0: legge i pin e aggiorna spectrum.setKempston()
    inline uint8_t joystickReadHardware() {
        uint8_t state = 0xFF;

        if (digitalRead(PIN_JOY_UP)    == LOW) state &= ~0x01;
        if (digitalRead(PIN_JOY_DOWN)  == LOW) state &= ~0x02;
        if (digitalRead(PIN_JOY_LEFT)  == LOW) state &= ~0x04;
        if (digitalRead(PIN_JOY_RIGHT) == LOW) state &= ~0x08;
        if (digitalRead(PIN_JOY_FIRE) == LOW) state &= ~0x10;

        return state;
    };

private:
    uint8_t lastMask = 0;
    uint8_t debounceMask = 0; // letture grezze del ciclo precedente, per un debounce minimo
};

extern ZXJoystick zxJoystick;

#endif // ZX_JOYSTICK_H
