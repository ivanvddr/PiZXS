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

#include "ZXJoystick.h"
#include "Spectrum.h"

ZXJoystick zxJoystick;

void ZXJoystick::begin() {
    pinMode(PIN_JOY_UP,    INPUT_PULLUP);
    pinMode(PIN_JOY_DOWN,  INPUT_PULLUP);
    pinMode(PIN_JOY_LEFT,  INPUT_PULLUP);
    pinMode(PIN_JOY_RIGHT, INPUT_PULLUP);
    pinMode(PIN_JOY_FIRE,  INPUT_PULLUP);
}

void ZXJoystick::poll() {
    uint8_t raw = 0;
    if (!digitalRead(PIN_JOY_RIGHT)) raw |= 0x01;
    if (!digitalRead(PIN_JOY_LEFT))  raw |= 0x02;
    if (!digitalRead(PIN_JOY_DOWN))  raw |= 0x04;
    if (!digitalRead(PIN_JOY_UP))    raw |= 0x08;
    if (!digitalRead(PIN_JOY_FIRE))  raw |= 0x10;

    // Debounce minimo: il valore viene applicato solo quando due letture
    // consecutive (due chiamate a poll(), quindi due frame, ~40ms) coincidono.
    if (raw == debounceMask) {
        if (raw != lastMask) {
            lastMask = raw;
            spectrum.setKempston(lastMask);
        }
    }
    debounceMask = raw;
}


