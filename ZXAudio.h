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
 * ZXAudio.h
 * Uscita audio del beeper via PWM + DMA, RP2040 SDK "nudo" (hardware/pwm.h,
 * hardware/dma.h) sotto Arduino-Pico.
 *
 * Architettura (come richiesto):
 *  - Lo slice PWM su GPIO lavora ESATTAMENTE alla frequenza di campionamento
 *    (22050 Hz), risoluzione 8 bit (wrap=255). Il filtro RC a due stadi (vedi
 *    schema hardware) elimina la portante e restituisce l'audio.
 *  - Un canale DMA, agganciato al DREQ generato dal wrap dello slice PWM,
 *    scrive automaticamente il campione successivo nel registro CC ad ogni
 *    periodo: la CPU non deve "spingere" i campioni uno a uno.
 *  - Il buffer è quello prodotto da Spectrum::runFrame() (441 campioni a
 *    frame, doppio buffer: mentre la DMA riproduce il buffer N, core0 sta
 *    già generando il buffer N+1).
 *  - L'IRQ di fine-buffer DMA è abilitato e gestito da CORE1:
 *    a interrupt scattato, si rilegge se c'è un nuovo buffer pronto da
 *    Spectrum e si riarma la DMA sul prossimo buffer. Se non c'è ancora nulla
 *    di pronto, si ripete l'ultimo buffer (evita un "pop" di silenzio totale,
 *    meglio di uno scatto secco).
 */

#ifndef ZX_AUDIO_H
#define ZX_AUDIO_H

#include <Arduino.h>
#include "ZXConfig.h"

class ZXAudio {
public:
    // Da chiamare da core1 (setup1()): configura PWM, DMA e ABILITA L'IRQ
    // DMA sul NVIC di core1 (irq_set_enabled chiamata da questo stesso core).
    void begin();

private:
    static void dmaIrqHandler();   // ISR statica (richiesta da irq_set_exclusive_handler)
    void armNextBuffer();          // ricarica il canale DMA col prossimo buffer pronto

    int dmaChannel = -1;
    uint pwmSlice = 0;
    bool pwmChannelIsB = false;
    int lastPlayedBufferIdx = 0;
};

extern ZXAudio zxAudio;

#endif // ZX_AUDIO_H
