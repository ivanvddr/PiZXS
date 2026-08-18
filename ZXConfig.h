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
 * ZXConfig.h
 * Mappatura pin per ZX Spectrum 48K emulator su Raspberry Pi Pico (RP2040)
 *
 */

#ifndef ZX_CONFIG_H
#define ZX_CONFIG_H

// ---- Audio (PWM + DMA) -----------------------------------------------------
#define PIN_AUDIO_PWM      15

// ---- Joystick (Kempston, 5 vie + fire) ------------------------------------
#define PIN_JOY_UP          2
#define PIN_JOY_DOWN        3
#define PIN_JOY_LEFT        4
#define PIN_JOY_RIGHT       5
#define PIN_JOY_FIRE        6

// ---- Tastiera PS/2 ----------------------------------------------------------
#define PIN_PS2_DATA       27
#define PIN_PS2_CLOCK      28

// ---- Tasto opzionale di reset/menu -----------------------------------------
#define PIN_RESET_BUTTON   26

// ---- Parametri emulazione ---------------------------------------------------
#define Z80_CLOCK_HZ        3500000UL     // Z80 a 3.5MHz (Spectrum 48K)
#define TSTATES_PER_FRAME   69888         // 50Hz PAL, 48K timing
#define FRAME_HZ            50

#define AUDIO_SAMPLE_RATE   22050
#define AUDIO_SAMPLES_PER_FRAME  (AUDIO_SAMPLE_RATE / FRAME_HZ)   // 441

// Valvola di sicurezza per il timing reale: se 0, il display viene
// aggiornato ad ogni frame Spectrum (50Hz). Se >0, l'aggiornamento video
// (decode + push DMA) viene fatto solo ogni (ZX_FRAMESKIP+1) frame; la CPU e
// l'audio continuano comunque a essere eseguiti/generati OGNI frame, quindi
// velocità di emulazione e intonazione del beeper non vengono toccate: si
// perde solo fluidità video, recuperando tempo macchina se il push al
// display dovesse risultare il collo di botiglia rispetto ai 20ms disponibili.
#define ZX_FRAMESKIP        0

// Overclock consigliato (vedi PiMSX): necessario per stare in tempo reale
#define PICO_CLOCK_KHZ   252000UL

#endif // ZX_CONFIG_H
