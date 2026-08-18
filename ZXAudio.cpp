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

#include "ZXAudio.h"
#include "Spectrum.h"
#include <hardware/pwm.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>

ZXAudio zxAudio;

void ZXAudio::begin() {
    // ---- PWM: 8 bit (wrap=255), frequenza = frequenza di campionamento ----
    pwmSlice = pwm_gpio_to_slice_num(PIN_AUDIO_PWM);
    uint channel = pwm_gpio_to_channel(PIN_AUDIO_PWM);
    pwmChannelIsB = (channel == PWM_CHAN_B);

    gpio_set_function(PIN_AUDIO_PWM, GPIO_FUNC_PWM);

    pwm_config cfg = pwm_get_default_config();
    float divider = (float)clock_get_hz(clk_sys) / (AUDIO_SAMPLE_RATE * 256.0f);
    if (divider < 1.0f) divider = 1.0f; // limite minimo del divisore PWM
    pwm_config_set_clkdiv(&cfg, divider);
    pwm_config_set_wrap(&cfg, 255);
    pwm_init(pwmSlice, &cfg, true);
    pwm_set_chan_level(pwmSlice, channel, 128); // livello neutro iniziale

    // ---- DMA: scrive il prossimo campione nel registro CC ad ogni wrap del PWM ----
    dmaChannel = dma_claim_unused_channel(true);

    volatile uint16_t* ccHalf = (volatile uint16_t*)&pwm_hw->slice[pwmSlice].cc;
    if (pwmChannelIsB) ccHalf++;   // canale B = word alta del registro CC (little endian)

    dma_channel_config c = dma_channel_get_default_config(dmaChannel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pwm_get_dreq(pwmSlice));

    lastPlayedBufferIdx = 0;
    dma_channel_configure(
        dmaChannel, &c,
        (volatile void*)ccHalf,                       // destinazione: metà giusta del registro CC
        spectrum.audioBuffer(lastPlayedBufferIdx),     // sorgente: primo buffer (silenzio finché non parte l'emulazione)
        Spectrum::AUDIO_BUFFER_LEN,
        true                                           // parti subito: la DMA aspetterà i DREQ del PWM
    );

    // ---- IRQ di fine-buffer: ABILITATO QUI, quindi sul NVIC di QUESTO core (core1) ----
    dma_channel_set_irq0_enabled(dmaChannel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, ZXAudio::dmaIrqHandler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void ZXAudio::dmaIrqHandler() {
    // Ack dell'interrupt sul canale
    dma_hw->ints0 = 1u << zxAudio.dmaChannel;
    zxAudio.armNextBuffer();
}

void ZXAudio::armNextBuffer() {
    int idx = spectrum.acquireReadyAudioBuffer();
    if (idx < 0) {
        // Nessun nuovo buffer pronto (core0 non ha ancora finito il frame):
        // ripeti l'ultimo, è meglio di un silenzio secco/scatto.
        idx = lastPlayedBufferIdx;
    } else {
        lastPlayedBufferIdx = idx;
    }

    dma_channel_set_read_addr(dmaChannel, spectrum.audioBuffer(idx), false);
    dma_channel_set_trans_count(dmaChannel, Spectrum::AUDIO_BUFFER_LEN, true); // true = riparti subito
}
