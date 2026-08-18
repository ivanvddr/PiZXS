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

#include "ZXLoader.h"

// ============================================================================
// Costruttore
// ============================================================================

ZXLoader::ZXLoader(uint8_t* ram, size_t ramSize)
    : _ram(ram), _ramSize(ramSize) {
    memset(&_state, 0, sizeof(_state));
}

// ============================================================================
// Filesystem
// ============================================================================

bool ZXLoader::initSD() {
    // Configura SPI1 per SD
    SPI1.setRX(SD_MISO);
    SPI1.setTX(SD_MOSI);
    SPI1.setSCK(SD_SCLK);
    SPI1.setCS(SD_CS);
    SPI1.begin();
    
    delay(100);

    // Forza un clock SPI basso e sicuro, invece del default
    //const uint32_t SD_SPI_CLOCK = 4000000; // 4 MHz
    const uint32_t SD_SPI_CLOCK = 10000000; // 10 MHz
    
    if (!SD.begin(SD_CS, SD_SPI_CLOCK, SPI1)) {
        Serial.println("SD init fallita!");
        return false;
    }
    return true;
}

bool ZXLoader::initFilesystem(ZXFSType preferredFS) {
    _filesystemReady = false;
    _currentFS = ZXFS_NONE;

    if (preferredFS == ZXFS_SD) {
        if (initSD()) {
            _currentFS = ZXFS_SD;
            _filesystemReady = true;
            Serial.println("[ZXLoader] Filesystem: SD card");
            return true;
        }
        Serial.println("[ZXLoader] SD non disponibile, provo LittleFS...");
    }

    if (LittleFS.begin()) {
        _currentFS = ZXFS_LITTLEFS;
        _filesystemReady = true;
        Serial.println("[ZXLoader] Filesystem: LittleFS");
        return true;
    }

    Serial.println("[ZXLoader] ERRORE: nessun filesystem disponibile.");
    return false;
}

File ZXLoader::openFile(const char* path, const char* mode) {
    if (_currentFS == ZXFS_SD) return SD.open(path, mode);
    if (_currentFS == ZXFS_LITTLEFS) return LittleFS.open(path, mode);
    return File();
}

bool ZXLoader::fileExists(const char* path) {
    if (_currentFS == ZXFS_SD) return SD.exists(path);
    if (_currentFS == ZXFS_LITTLEFS) return LittleFS.exists(path);
    return false;
}

void ZXLoader::closeFilesystem() {
    if (_currentFS == ZXFS_SD) SD.end();
    _currentFS = ZXFS_NONE;
    _filesystemReady = false;
}

// ============================================================================
// Selezione / caricamento — dispatch per estensione
// ============================================================================

void ZXLoader::setSelectedROM(const char* path) {
    strncpy(_selectedROMPath, path, sizeof(_selectedROMPath) - 1);
    _selectedROMPath[sizeof(_selectedROMPath) - 1] = 0;
}

bool ZXLoader::caricaROMSelezionata() {
    if (_selectedROMPath[0] == 0) {
        Serial.println("[ZXLoader] Nessun file selezionato.");
        return false;
    }
    return caricaROM(_selectedROMPath);
}

bool ZXLoader::haEstensione(const char* path, const char* ext) const {
    size_t lp = strlen(path), le = strlen(ext);
    if (lp < le) return false;
    return strcasecmp(path + (lp - le), ext) == 0;
}

void ZXLoader::estraiNomeROM(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    strncpy(_romName, base, sizeof(_romName) - 1);
    _romName[sizeof(_romName) - 1] = 0;
    char* dot = strrchr(_romName, '.');
    if (dot) *dot = 0;
}

void ZXLoader::scaricaROM() {
    _romCaricata = false;
    _romSize = 0;
    _snapVersion = 0;
    _mediaType = ZXMEDIA_NONE;
    memset(_romName, 0, sizeof(_romName));
    memset(_romPath, 0, sizeof(_romPath));
    memset(&_state, 0, sizeof(_state));
    chiudiTape();
    _autoTypeStep = 0;
    _autoTypeStepCount = 0;
}

bool ZXLoader::caricaROM(const char* path) {
    scaricaROM();

    if (!_filesystemReady) {
        Serial.println("[ZXLoader] Filesystem non pronto.");
        return false;
    }

    bool ok;
    if (haEstensione(path, ".z80")) {
        // --- SNAPSHOT ---
        File f = openFile(path, "r");
        if (!f) {
            Serial.printf("[ZXLoader] ERRORE: impossibile aprire %s\n", path);
            return false;
        }
        size_t fileSize = f.size();
        if (fileSize < 30) {
            Serial.println("[ZXLoader] ERRORE: file .z80 troppo piccolo.");
            f.close();
            return false;
        }

        uint8_t rawHeader[30];
        ok = leggiHeaderBase(f, rawHeader);
        if (ok) {
            memset(_ram, 0, _ramSize);
            uint16_t pcFromBase = rawHeader[6] | (rawHeader[7] << 8);
            if (pcFromBase != 0) {
                _snapVersion = 1;
                _state.PC = pcFromBase;
                bool compresso = (rawHeader[12] & 0x20) != 0;
                if (rawHeader[12] == 255) compresso = false;
                ok = caricaV1(f, compresso);
            } else {
                ok = caricaV2V3(f, rawHeader);
            }
        }
        f.close();

        if (ok) {
            _romSize = fileSize;
            _mediaType = ZXMEDIA_SNAPSHOT;
        }

    } else if (haEstensione(path, ".tap")) {
        // --- TAPE ---
        ok = indicizzaTap(path);
        if (ok) {
            _romSize = _tapeFile.size();
            _mediaType = ZXMEDIA_TAPE;
        }

    } else {
        Serial.printf("[ZXLoader] ERRORE: estensione non riconosciuta per %s "
                      "(supportate: .z80, .tap)\n", path);
        return false;
    }

    if (!ok) {
        Serial.println("[ZXLoader] ERRORE durante il caricamento.");
        scaricaROM();
        return false;
    }

    strncpy(_romPath, path, sizeof(_romPath) - 1);
    estraiNomeROM(path);
    _romCaricata = true;

    if (_mediaType == ZXMEDIA_SNAPSHOT) {
        Serial.printf("[ZXLoader] Snapshot '%s' caricato (v%d, PC=%04X, %u byte)\n",
                      _romName, _snapVersion, _state.PC, (unsigned)_romSize);
    } else {
        Serial.printf("[ZXLoader] Nastro '%s' caricato (%u blocchi, %u byte)\n",
                      _romName, _tapeBlockCount, (unsigned)_romSize);
    }
    return true;
}

// ============================================================================
// Avvio trasparente
// ============================================================================

bool ZXLoader::avvia(Z80& cpu, ZXSetKeyFn setKey, uint8_t* borderOut) {
    if (!_romCaricata) {
        Serial.println("[ZXLoader] Nessun file caricato da avviare.");
        return false;
    }

    if (_mediaType == ZXMEDIA_SNAPSHOT) {
        return applicaStatoCPU(cpu, borderOut);
    }

    if (_mediaType == ZXMEDIA_TAPE) {
        // La macchina deve già essere stata resettata dal chiamante
        // (spectrum.resetMachine()) PRIMA di invocare avvia(): qui ci
        // limitiamo a mettere in coda l'autotype, che partirà dal
        // prossimo aggiornaNastro() chiamato nel loop.
        preparaAutoTypeLoad();
        _tapeBlockIndex = 0;
        Serial.println("[ZXLoader] Nastro pronto: digitazione automatica LOAD \"\" in coda.");
        return true;
    }

    return false;
}

// ================
// SNAPSHOT (.z80) 
// ================

bool ZXLoader::leggiHeaderBase(File& f, uint8_t rawHeader[30]) {
    if (f.read(rawHeader, 30) != 30) return false;

    _state.A = rawHeader[0];
    _state.F = rawHeader[1];
    uint16_t bc = rawHeader[2] | (rawHeader[3] << 8);
    _state.B = bc >> 8; _state.C = bc & 0xFF;
    uint16_t hl = rawHeader[4] | (rawHeader[5] << 8);
    _state.H = hl >> 8; _state.L = hl & 0xFF;
    uint16_t sp = rawHeader[8] | (rawHeader[9] << 8);
    _state.SP = sp;
    _state.I = rawHeader[10];
    uint8_t rLow7 = rawHeader[11] & 0x7F;
    uint8_t rBit7 = (rawHeader[12] & 0x01) << 7;
    _state.R = rLow7 | rBit7;
    _state.borderColor = (rawHeader[12] >> 1) & 0x07;
    uint16_t de = rawHeader[13] | (rawHeader[14] << 8);
    _state.D = de >> 8; _state.E = de & 0xFF;
    uint16_t bc_ = rawHeader[15] | (rawHeader[16] << 8);
    _state.B_ = bc_ >> 8; _state.C_ = bc_ & 0xFF;
    uint16_t de_ = rawHeader[17] | (rawHeader[18] << 8);
    _state.D_ = de_ >> 8; _state.E_ = de_ & 0xFF;
    uint16_t hl_ = rawHeader[19] | (rawHeader[20] << 8);
    _state.H_ = hl_ >> 8; _state.L_ = hl_ & 0xFF;
    _state.A_ = rawHeader[21];
    _state.F_ = rawHeader[22];
    _state.IY = rawHeader[23] | (rawHeader[24] << 8);
    _state.IX = rawHeader[25] | (rawHeader[26] << 8);
    _state.IFF1 = rawHeader[27] != 0;
    _state.IFF2 = rawHeader[28] != 0;
    _state.IM = rawHeader[29] & 0x03;

    _state.valid = true;
    return true;
}

bool ZXLoader::caricaV1(File& f, bool compresso) {
    if (!compresso) return copiaRaw(f, _ram, ZX_RAM_SIZE);
    return decomprimiRLE(f, _ram, ZX_RAM_SIZE);
}

bool ZXLoader::caricaV2V3(File& f, const uint8_t rawHeader[30]) {
    uint8_t lenBuf[2];
    if (f.read(lenBuf, 2) != 2) return false;
    uint16_t extraLen = lenBuf[0] | (lenBuf[1] << 8);

    uint8_t extra[64];
    if (extraLen > sizeof(extra)) {
        Serial.println("[ZXLoader] ERRORE: header aggiuntivo troppo grande.");
        return false;
    }
    if (f.read(extra, extraLen) != extraLen) return false;

    _snapVersion = (extraLen == 23) ? 2 : 3;
    _state.PC = extra[0] | (extra[1] << 8);

    uint8_t hwMode = extra[2];
    bool is48k = (_snapVersion == 2) ? (hwMode <= 1 || hwMode == 3)
                                      : (hwMode == 0 || hwMode == 1 || hwMode == 3);
    if (!is48k) {
        Serial.printf("[ZXLoader] ERRORE: snapshot non-48K (modo %u).\n", hwMode);
        return false;
    }

    while (f.available() >= 3) {
        uint8_t blkLenBuf[2];
        if (f.read(blkLenBuf, 2) != 2) break;
        uint16_t blkLen = blkLenBuf[0] | (blkLenBuf[1] << 8);

        uint8_t pageNum;
        if (f.read(&pageNum, 1) != 1) break;

        if (!caricaBloccoPagina(f, blkLen, pageNum)) {
            size_t skip = (blkLen == 0xFFFF) ? 16384 : blkLen;
            f.seek(f.position() + skip);
        }
    }
    return true;
}

bool ZXLoader::caricaBloccoPagina(File& f, uint16_t lunghezzaCompressa, uint8_t numPagina) {
    uint16_t z80Base;
    switch (numPagina) {
        case 8: z80Base = 0x4000; break;
        case 4: z80Base = 0x8000; break;
        case 5: z80Base = 0xC000; break;
        default: return false;
    }
    uint8_t* dest = ramPtr(z80Base);
    if (!dest) return false;

    if (lunghezzaCompressa == 0xFFFF) return copiaRaw(f, dest, 16384);
    return decomprimiRLE(f, dest, 16384);
}

bool ZXLoader::decomprimiRLE(File& f, uint8_t* dest, size_t destLen) {
    size_t written = 0;
    while (written < destLen) {
        int b = f.read();
        if (b < 0) return written > 0;

        if (b == 0xED) {
            int b2 = f.read();
            if (b2 < 0) { dest[written++] = (uint8_t)b; break; }

            if (b2 == 0xED) {
                int count = f.read();
                int value = f.read();
                if (count < 0 || value < 0) return written > 0;
                if (count == 0) return written > 0;
                for (int i = 0; i < count && written < destLen; i++) {
                    dest[written++] = (uint8_t)value;
                }
                continue;
            } else {
                if (written < destLen) dest[written++] = 0xED;
                if (written < destLen) dest[written++] = (uint8_t)b2;
                continue;
            }
        }

        dest[written++] = (uint8_t)b;
    }
    return true;
}

bool ZXLoader::copiaRaw(File& f, uint8_t* dest, size_t lunghezza) {
    size_t r = f.read(dest, lunghezza);
    if (r != lunghezza) {
        Serial.printf("[ZXLoader] ATTENZIONE: attesi %u byte, letti %u.\n",
                      (unsigned)lunghezza, (unsigned)r);
        return r > 0;
    }
    return true;
}

bool ZXLoader::applicaStatoCPU(Z80& cpu, uint8_t* borderOut) const {
    if (_mediaType != ZXMEDIA_SNAPSHOT || !_state.valid) {
        Serial.println("[ZXLoader] Nessuno snapshot valido da applicare.");
        return false;
    }

    cpu.setAF((_state.A << 8) | _state.F);
    cpu.setBC((_state.B << 8) | _state.C);
    cpu.setDE((_state.D << 8) | _state.E);
    cpu.setHL((_state.H << 8) | _state.L);
    cpu.setIX(_state.IX);
    cpu.setIY(_state.IY);
    cpu.setPC(_state.PC);
    cpu.setSP(_state.SP);
    cpu.setI(_state.I);
    cpu.setR(_state.R);

    cpu.setShadowAF((_state.A_ << 8) | _state.F_);
    cpu.setShadowBC((_state.B_ << 8) | _state.C_);
    cpu.setShadowDE((_state.D_ << 8) | _state.E_);
    cpu.setShadowHL((_state.H_ << 8) | _state.L_);

    cpu.IFF1 = _state.IFF1;
    cpu.IFF2 = _state.IFF2;
    cpu.setIM(_state.IM);
    cpu.halted = false;
    cpu.pendingEI = false;
    cpu.pendingNMI = false;
    cpu.pendingINT = false;

    if (borderOut) *borderOut = _state.borderColor;

    Serial.printf("[ZXLoader] Stato CPU applicato: PC=%04X SP=%04X IM=%d IFF1=%d border=%d\n",
                  _state.PC, _state.SP, _state.IM, (int)_state.IFF1, _state.borderColor);
    return true;
}

// ============================================================================
// TAPE (.tap) — indicizzazione e trap LD-BYTES
// ============================================================================

void ZXLoader::chiudiTape() {
    if (_tapeFileOpen) {
        _tapeFile.close();
        _tapeFileOpen = false;
    }
    if (_tapeBlockTable) {
        delete[] _tapeBlockTable;
        _tapeBlockTable = nullptr;
    }
    _tapeBlockCount = 0;
    _tapeBlockIndex = 0;
}

bool ZXLoader::indicizzaTap(const char* path) {
    chiudiTape();

    File f = openFile(path, "r");
    if (!f) {
        Serial.printf("[ZXLoader] ERRORE: impossibile aprire %s\n", path);
        return false;
    }

    // Primo passaggio: conta i blocchi per allocare la tabella esatta.
    uint16_t count = 0;
    while (f.available() >= 2) {
        uint8_t lenBuf[2];
        if (f.read(lenBuf, 2) != 2) break;
        uint16_t blkLen = lenBuf[0] | (lenBuf[1] << 8);
        if (blkLen == 0) break; // blocco a lunghezza 0: fine anomala, ci fermiamo
        uint32_t pos = f.position();
        if (pos + blkLen > f.size()) break; // file troncato
        f.seek(pos + blkLen);
        count++;
    }

    if (count == 0) {
        Serial.println("[ZXLoader] ERRORE: nessun blocco valido trovato nel .tap.");
        f.close();
        return false;
    }

    _tapeBlockTable = new (std::nothrow) ZXTapBlockIndex[count];
    if (!_tapeBlockTable) {
        Serial.println("[ZXLoader] ERRORE: memoria insufficiente per indicizzare il nastro.");
        f.close();
        return false;
    }

    // Secondo passaggio: registra offset e lunghezza di ogni blocco.
    f.seek(0);
    uint16_t idx = 0;
    while (idx < count && f.available() >= 2) {
        uint8_t lenBuf[2];
        if (f.read(lenBuf, 2) != 2) break;
        uint16_t blkLen = lenBuf[0] | (lenBuf[1] << 8);
        if (blkLen == 0) break;

        _tapeBlockTable[idx].fileOffset = f.position();
        _tapeBlockTable[idx].blockLen = blkLen;
        f.seek(f.position() + blkLen);
        idx++;
    }
    _tapeBlockCount = idx;
    _tapeBlockIndex = 0;

    f.close();

    // Riapriamo il file e lo teniamo aperto per tutta la sessione di
    // gioco: ogni chiamata al trap farà solo un seek() + read() mirato.
    _tapeFile = openFile(path, "r");
    if (!_tapeFile) {
        Serial.println("[ZXLoader] ERRORE: impossibile riaprire il .tap per la lettura.");
        chiudiTape();
        return false;
    }
    _tapeFileOpen = true;

    Serial.printf("[ZXLoader] Nastro indicizzato: %u blocchi.\n", _tapeBlockCount);
    return true;
}

bool ZXLoader::servizioLDBytes(Z80& cpu, uint8_t* ram) {
    if (_mediaType != ZXMEDIA_TAPE || !_tapeFileOpen) return false;
    if (_tapeBlockIndex >= _tapeBlockCount) {
        // Nastro finito: nessun blocco da servire, l'errore restituito
        // dal chiamante (carry=0, gestito sotto) segnala fine caricamento.
    }

    uint16_t sp = cpu.getSP();
    uint8_t* spPtr = ramPtr(sp);
    if (!spPtr || sp > 0xFFFE) {
        // Stack fuori dalla RAM emulata: non dovrebbe mai capitare in
        // condizioni normali, ma proteggiamo comunque.
        return false;
    }
    uint16_t retAddr = spPtr[0] | (spPtr[1] << 8);

    bool successo = false;
    uint16_t ix = cpu.getIX();
    uint16_t de = cpu.getBC(); // placeholder, sovrascritto sotto
    de = 0;

    if (_tapeBlockIndex < _tapeBlockCount) {
        const ZXTapBlockIndex& blk = _tapeBlockTable[_tapeBlockIndex];
        uint16_t declaredLen = blk.blockLen; // flag(1) + dati + checksum(1)

        if (declaredLen >= 2) {
            _tapeFile.seek(blk.fileOffset);

            int flagByte = _tapeFile.read();
            uint16_t dataLen = declaredLen - 2; // esclude flag e checksum

            uint16_t expectedDE = cpu.getDE();
            uint16_t toCopy = (dataLen < expectedDE) ? dataLen : expectedDE;

            uint8_t checksum = (uint8_t)flagByte;
            bool isLoad = (cpu.getF() & FLAG_C) != 0;

            for (uint16_t i = 0; i < dataLen; i++) {
                int byteVal = _tapeFile.read();
                if (byteVal < 0) break;
                checksum ^= (uint8_t)byteVal;

                if (i < toCopy) {
                    uint8_t* dest = ramPtr(ix + i);
                    if (dest) {
                        if (isLoad) {
                            *dest = (uint8_t)byteVal;
                        } else {
                            // VERIFY: nessuna scrittura, il confronto reale
                            // andrebbe fatto byte per byte; per semplicità
                            // consideriamo il VERIFY riuscito se il checksum
                            // finale combacia (sufficiente per l'uso comune).
                        }
                    }
                }
            }

            int fileChecksum = _tapeFile.read();
            successo = (fileChecksum >= 0) && (checksum == (uint8_t)fileChecksum)
                       && (flagByte == cpu.getA());

            ix = ix + toCopy;
            de = expectedDE - toCopy;
        }

        _tapeBlockIndex++;
    }

    // Simulazione del RET: pop manuale di PC dallo stack.
    cpu.setSP(sp + 2);
    cpu.setPC(retAddr);

    // Aggiorna i registri come farebbe la routine reale al termine.
    cpu.setIX(ix);
    cpu.setDE(de);
    uint8_t f = cpu.getF() & ~FLAG_C;
    if (successo) f |= FLAG_C;
    cpu.setAF((cpu.getA() << 8) | f);

    if (!successo) {
        Serial.printf("[ZXLoader] Blocco %u/%u: checksum non valido o nastro finito "
                      "(atteso flag=%02X).\n",
                      _tapeBlockIndex, _tapeBlockCount, cpu.getA());
    }

    return true;
}

// ============================================================================
// Autotype "LOAD ""[ENTER]"
// ============================================================================

void ZXLoader::preparaAutoTypeLoad() {
    // Tasti presi dalla mappatura reale di ZXKeyboard::mapScancode:
    //   L              → row1=6 bit1=1  (K-mode: espande in "LOAD ")
    //   SYMBOL SHIFT+P → row 7 bit1 + row5 bit0  (virgolette ")
    //   ENTER          → row1=6 bit1=0
    uint8_t i = 0;
    _autoTypeSteps[i++] = { 6, 1, -1, -1 };   // L → "LOAD "
    _autoTypeSteps[i++] = { 7, 1,  5,  0 };   // SYMBOL SHIFT + P → "
    _autoTypeSteps[i++] = { 7, 1,  5,  0 };   // SYMBOL SHIFT + P → " (chiusura)
    _autoTypeSteps[i++] = { 6, 0, -1, -1 };   // ENTER
    _autoTypeStepCount = i;
    _autoTypeStep = 0;
    _autoTypeFrameCounter = 0;
    _autoTypeKeyDown = false;
    _autoTypeDelayFrames = AUTOTYPE_BOOT_DELAY_FRAMES;
}

void ZXLoader::aggiornaAutoType(ZXSetKeyFn setKey) {
    if (_autoTypeStep >= _autoTypeStepCount) return;

    if (_autoTypeDelayFrames > 0) {
        _autoTypeDelayFrames--;
        return;
    }

    if (!_autoTypeKeyDown) {
        // Fase di "gap" tra la fine dello step precedente e la pressione
        // del prossimo: consumata un frame alla volta.
        if (_autoTypeFrameCounter > 0) {
            _autoTypeFrameCounter--;
            return;
        }
        // Gap esaurito: premi il/i tasto/i dello step corrente.
        const ZXAutoTypeStep& step = _autoTypeSteps[_autoTypeStep];
        setKey(step.row1, step.bit1, true);
        if (step.row2 >= 0) setKey(step.row2, step.bit2, true);
        _autoTypeKeyDown = true;
        _autoTypeFrameCounter = AUTOTYPE_HOLD_FRAMES;
        return;
    }

    // Tasto premuto: aspetta che il tempo di pressione sia esaurito.
    if (_autoTypeFrameCounter > 0) {
        _autoTypeFrameCounter--;
        return;
    }

    // Tempo scaduto: rilascia e passa allo step successivo.
    const ZXAutoTypeStep& step = _autoTypeSteps[_autoTypeStep];
    setKey(step.row1, step.bit1, false);
    if (step.row2 >= 0) setKey(step.row2, step.bit2, false);
    _autoTypeKeyDown = false;
    _autoTypeStep++;
    _autoTypeFrameCounter = AUTOTYPE_GAP_FRAMES; // consumato al prossimo giro, se resta altro da digitare
}

// ============================================================================
// Aggiornamento nastro — da chiamare ogni frame dal loop principale
// ============================================================================

void ZXLoader::aggiornaNastro(Z80& cpu, uint8_t* ram, ZXSetKeyFn setKey) {
    if (_mediaType != ZXMEDIA_TAPE) return;

    if (_autoTypeStep < _autoTypeStepCount) {
        aggiornaAutoType(setKey);
    }

    if (cpu.getPC() == ZX_LD_BYTES_ADDR) {
        servizioLDBytes(cpu, ram);
    }
}

// ============================================================================
// Utilità / debug
// ============================================================================

void ZXLoader::elencaFileROM(const char* cartella) {
    if (!_filesystemReady) {
        Serial.println("[ZXLoader] Filesystem non pronto.");
        return;
    }

    File dir = openFile(cartella, "r");
    if (!dir || !dir.isDirectory()) {
        Serial.printf("[ZXLoader] Cartella non trovata: %s\n", cartella);
        return;
    }

    Serial.printf("[ZXLoader] File in %s:\n", cartella);
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".z80") || name.endsWith(".Z80") ||
                name.endsWith(".tap") || name.endsWith(".TAP")) {
                Serial.printf("  - %s (%u byte)\n", entry.name(), (unsigned)entry.size());
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
}

void ZXLoader::debugStatoCPU() const {
    if (!_state.valid) {
        Serial.println("[ZXLoader] Nessuno stato snapshot disponibile.");
        return;
    }
    Serial.println("[ZXLoader] --- Stato snapshot ---");
    Serial.printf("  AF=%02X%02X  BC=%02X%02X  DE=%02X%02X  HL=%02X%02X\n",
                  _state.A, _state.F, _state.B, _state.C,
                  _state.D, _state.E, _state.H, _state.L);
    Serial.printf("  IX=%04X IY=%04X PC=%04X SP=%04X\n",
                  _state.IX, _state.IY, _state.PC, _state.SP);
    Serial.printf("  I=%02X R=%02X IFF1=%d IFF2=%d IM=%d border=%d\n",
                  _state.I, _state.R, (int)_state.IFF1, (int)_state.IFF2,
                  _state.IM, _state.borderColor);
}

void ZXLoader::debugRAM(uint16_t inizioZ80, uint16_t fineZ80) const {
    Serial.printf("[ZXLoader] RAM %04X-%04X:\n", inizioZ80, fineZ80);
    for (uint16_t addr = inizioZ80; addr <= fineZ80; addr++) {
        uint8_t* p = ramPtr(addr);
        Serial.printf("%02X ", p ? *p : 0xFF);
        if ((addr - inizioZ80) % 16 == 15) Serial.println();
        if (addr == 0xFFFF) break;
    }
    Serial.println();
}

void ZXLoader::debugTape() const {
    if (_mediaType != ZXMEDIA_TAPE) {
        Serial.println("[ZXLoader] Nessun nastro caricato.");
        return;
    }
    Serial.printf("[ZXLoader] Nastro: %u blocchi totali, prossimo da servire: %u\n",
                  _tapeBlockCount, _tapeBlockIndex);
    for (uint16_t i = 0; i < _tapeBlockCount && i < 20; i++) {
        Serial.printf("  blocco %u: offset=%lu len=%u\n",
                      i, (unsigned long)_tapeBlockTable[i].fileOffset,
                      _tapeBlockTable[i].blockLen);
    }
}
