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
 * PiZXS.ino
 * Emulatore ZX Spectrum 48K — Raspberry Pi Pico (RP2040), Arduino IDE.
 *
 * ARCHITETTURA:
 *   core0:
 *     1. zxKeyboard.poll() + zxJoystick.poll()   — input fresco prima della CPU
 *     2. zxDisplay.beginFrame()                  — attende buffer libero
 *     3. spectrum.runFrame()                     — Z80 + scanline decode + audio
 *        Dentro runFrame(): ogni 224 T-state viene chiamato
 *        zxDisplay.decodeScanline() esattamente come fa la ULA reale.
 *        Al termine: zxDisplay.commitFrame() marca il buffer pronto.
 *
 *   core1:
 *     setup1(): TFT_eSPI + DMA, PWM + DMA audio + IRQ DMA (sul NVIC di core1)
 *     loop1():  zxDisplay.task() — espande 4bpp via LUT, push DMA al display
 *               L'audio e' completamente automatico via IRQ DMA.
 *
 * RENDERING SCANLINE-ACCURATE:
 *   La ULA reale legge la RAM durante T=14336..57343 (192 linee × 224 T-state).
 *   Viene decodificata ogni riga nel momento esatto in cui la "beam" la avrebbe
 *   letta, con la RAM nello stesso stato. Questo elimina:
 *     - cursori spurii nel listing BASIC
 *     - artefatti di tearing durante scroll/ridisegno
 *   L'impatto in performance e' ~0.3ms su 20ms di budget frame (~1.5%).
 *
 * DIPENDENZE:
 *   - TFT_eSPI (Bodmer), User_Setup.h configurato per ILI9341 + DMA (vedi README)
 *   - Core RP2040 di Earle Philhower (LittleFS, multicore, hardware/pwm.h, dma.h)
 *
 * ROM: caricare bios con nome "rom48.rom" (16384 byte) via LittleFS Upload Tool nella cartella
 *      /data dello sketch.
 */

#include "ZXConfig.h"
#include "Spectrum.h"
#include "ZXDisplay.h"
#include "ZXAudio.h"
#include "ZXKeyboard.h"
#include "ZXJoystick.h"
#include <hardware/clocks.h>
#include "hardware/vreg.h"    // Per vreg_set_voltage
#include "hardware/clocks.h"  // Per clock_configure e clk_peri
#include "ZXLoader.h"
#include "ZXMenu.h"
#include "ZXGameBrowser.h"
#include "PiZXSLogo.h"

volatile bool core1CanStart = false;

TFT_eSPI    tft;

enum AppState {
    STATE_MENU,
    STATE_BASIC,
    STATE_GAME
};

AppState currentState = STATE_MENU;

ZXMenu* menu = nullptr;
ZXLoader zxLoader(spectrum.ram, sizeof(spectrum.ram));
ZXGameBrowser* gameBrowser = nullptr;

void setKeyWrapper(uint8_t row, uint8_t bit, bool pressed) {
    spectrum.setKey(row, bit, pressed);
}

void cleanupTFT() {
    Serial.println("Cleanup TFT...");
    
    tft.endWrite();
    
    uint32_t timeout = millis();
    while (tft.dmaBusy() && (millis() - timeout < 200)) {
        delay(1);
    }
    
    if (tft.dmaBusy()) {
        Serial.println("TFT DMA force stop!");
        // TFT_eSPI usa di default channel 0 e 1
        dma_channel_abort(0);
        dma_channel_abort(1);
    }
    
    tft.fillScreen(TFT_BLACK);
    delay(50); // aspetta che fillScreen finisca
    
    tft.setViewport(0, 0, 320, 240);
    tft.setTextDatum(TL_DATUM);
    
    Serial.println("TFT clean");
}

void resetAllDMA() {
    Serial.println("Reset DMA channels...");
    
    // Aspetta che il TFT finisca prima di toccare qualsiasi canale
    tft.dmaWait();
    tft.endWrite();
    
    // Non fare abort su tutti i canali: distrugge lo stato TFT_eSPI
    // Basta assicurarsi che non ci siano trasferimenti pendenti
    delay(20);
    
    Serial.println("DMA reset complete");
}

bool avviaGioco(const char* path) {
    spectrum.resetMachine();              // sempre, indipendentemente dal tipo
    if (!zxLoader.caricaROM(path)) return false;

    uint8_t border = 7;
    if (!zxLoader.avvia(spectrum.cpu, setKeyWrapper, &border)) return false;

    if (zxLoader.getTipoMedia() == ZXMEDIA_SNAPSHOT) {
        spectrum.setBorder(border);
    }
    return true;
}

void handleGameBrowser(ZXJoystick* zxjoy) {
     // Crea browser solo una volta
    if (!gameBrowser) {
        // USA LOADER per init filesystem
        // Prova prima LittleFS, poi fallback su SD
        if (!zxLoader.initFilesystem(ZXFS_SD)) {
            Serial.println("No filesystem available!");
            tft.fillScreen(TFT_LIGHTGREY);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("NO FILESYSTEM!", 160, 120);
            delay(3000);
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Crea browser (passa il filesystem dal Loader)
        gameBrowser = new ZXGameBrowser(&tft, &zxLoader, SD_CS);
        gameBrowser->setLogo(logoPiZXS_small, 61, 30);
        gameBrowser->setPath("/games");
        gameBrowser->setFileExt(".z80");
        
        if (!gameBrowser->begin(zxLoader.getCurrentFilesystem())) {
            Serial.println("Browser init failed!");
            delete gameBrowser;
            gameBrowser = nullptr;
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Browser OK, libera menu
        delete menu;
        menu = nullptr;
        Serial.printf("Browser active (%s)\n", 
                     zxLoader.getCurrentFilesystem() == ZXFS_SD ? "SD" : "LittleFS");
    }
    
    uint8_t joyState = zxjoy->joystickReadHardware();
    
    // Aggiorna browser
    bool gameSelected = gameBrowser->update(joyState);
    
    if (gameSelected) {
        Serial.println("Game selected!");

        // Browser salva il path nel Loader
        String gamePath = gameBrowser->getPrgSelected();
        
        // ========================================
        // STEP 1: Libera browser
        // ========================================
        delete gameBrowser;
        gameBrowser = nullptr;
        Serial.println("Browser destroyed");
                
        // ========================================
        // STEP 2: Reset hardware
        // ========================================
        resetAllDMA();
        cleanupTFT();
        delay(100);

        ZXsetup();
        
               
        // ========================================
        // STEP 3: Carica gioco con Loader!
        // ========================================
        if (!avviaGioco(gamePath.c_str())) {
            Serial.println("Loading error!");
            tft.fillScreen(TFT_LIGHTGREY);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("LOAD ERROR!", 160, 120);
            delay(3000);
            return;
        }

               
        Serial.println("Game started!");
        
        ZXloopGames();
    }
    
    delay(10);
    yield();

}

void handleBasic() {
  SPI1.end();
  delay(100);
  resetAllDMA();
  cleanupTFT();
  delay(100);
  ZXsetup();
  ZXloop();
}

void handleMenu() {
    MenuAction action = menu->update(); 
    
    switch (action) {
        case ACTION_START_BASIC:
            Serial.println("BASIC start...");
            currentState = STATE_BASIC;
            break;
            
        case ACTION_START_GAME:
            Serial.println("GAME menu start...");
            currentState = STATE_GAME;
            break;
        case ACTION_NONE:
        default:
            // Nessuna azione
            break;
    }
}

void setup() {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    set_sys_clock_khz(PICO_CLOCK_KHZ, true);
    clock_configure(
      clk_peri,
      0, // Nessun glitchless mux
      CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // Sorgente: System PLL
      PICO_CLOCK_KHZ * 1000, // Frequenza di ingresso (276 MHz)
      PICO_CLOCK_KHZ * 1000  // Frequenza richiesto in uscita (276 MHz)
    );

    Serial.begin(115200);
    delay(200);

    tft.init();
    tft.initDMA();
    tft.setRotation(1); // 320x240
    tft.fillScreen(TFT_BLACK);

    zxJoystick.begin();

    menu = new ZXMenu(&tft, &zxJoystick);

        //Imposta logo
    menu->setLogo(logoPiZXS, 143, 70);
    
    // Inizializza menu
    menu->begin();
    
    Serial.println("Setup completed!");

}

void loop() {

      switch (currentState) {
        case STATE_MENU:
            handleMenu();
            break;
            
        case STATE_BASIC:
            handleBasic();
            break;
            
        case STATE_GAME:
            handleGameBrowser(&zxJoystick);
            break;
      } 

}


// ===========================================================================
// CORE 0
// ===========================================================================

void ZXsetup() {

    core1CanStart = true;

    if (!spectrum.begin()) {
        Serial.println("[FATAL] ROM 48K mancante in LittleFS.");
        while (true) delay(1000);
    }

    zxKeyboard.begin();

    Serial.println("ZX Spectrum 48K — avviato.");
}


void ZXloop() {

  while(true) {

      /*
      static uint32_t frameCount = 0;
      static uint32_t t0 = millis();
      frameCount++;
      if (frameCount % 250 == 0) {
          uint32_t elapsed = millis() - t0;
          Serial.printf("250 frame emulati in %lu ms (atteso: 5000ms per 50Hz reale)\n", elapsed);
          t0 = millis();
      }*/

        static uint32_t nextFrameMicros = micros();

        // 1. Input: matrice tastiera e joystick aggiornati PRIMA che la CPU giri
        zxKeyboard.poll();
        zxJoystick.poll();

        // 2. Attende che il buffer di scrittura sia libero (di solito istantaneo)
        zxDisplay.beginFrame();

        // 3. Esegue un frame completo:
        //    - triggerINT() a T=0
        //    - step() per 69888 T-state
        //    - decodeScanline() per ogni riga nel momento ULA corretto (T=14336+n*224)
        //    - commitFrame() al termine
        //    - chiusura buffer audio
        spectrum.runFrame();

            nextFrameMicros += 20000;               // 1 frame = 20000us a 50Hz
        int32_t remaining = (int32_t)(nextFrameMicros - micros());
        if (remaining > 0) {
            delayMicroseconds(remaining);
        } else {
            nextFrameMicros = micros();          // eravamo in ritardo, non accumulare debito
        }

  }
}

void ZXloopGames() {

  while(true) {

      /*
      static uint32_t frameCount = 0;
      static uint32_t t0 = millis();
      frameCount++;
      if (frameCount % 250 == 0) {
          uint32_t elapsed = millis() - t0;
          Serial.printf("250 frame emulati in %lu ms (atteso: 5000ms per 50Hz reale)\n", elapsed);
          t0 = millis();
      }*/

        static uint32_t nextFrameMicros = micros();

        // 1. Input: matrice tastiera e joystick aggiornati PRIMA che la CPU giri
        zxKeyboard.poll();
        zxJoystick.poll();

        // 2. Attende che il buffer di scrittura sia libero (di solito istantaneo)
        zxDisplay.beginFrame();

        // 3. Esegue un frame completo:
        //    - triggerINT() a T=0
        //    - step() per 69888 T-state
        //    - decodeScanline() per ogni riga nel momento ULA corretto (T=14336+n*224)
        //    - commitFrame() al termine
        //    - chiusura buffer audio
        spectrum.runFrame();

        //zxLoader.aggiornaNastro(spectrum.cpu, spectrum.ram, setKeyWrapper);

        nextFrameMicros += 20000;               // 1 frame = 20000us a 50Hz
        int32_t remaining = (int32_t)(nextFrameMicros - micros());
        if (remaining > 0) {
            delayMicroseconds(remaining);
        } else {
            nextFrameMicros = micros();          // eravamo in ritardo, non accumulare debito
        }

  }
}

// ===========================================================================
// CORE 1
// ===========================================================================

void setup1() {
    while(!core1CanStart) { tight_loop_contents(); }
    zxDisplay.begin();
    zxAudio.begin();   // Configura PWM+DMA audio e abilita IRQ DMA su QUESTO core
}

void loop1() {
    // Espande il frame 4bpp via LUT palette e lo pusha al display via DMA.
    // L'audio e' completamente gestito dall'IRQ DMA, core1 non deve fare nulla.
    zxDisplay.task();
}
