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

#ifndef Z80_H
#define Z80_H

#include <Arduino.h>

typedef uint8_t (*Z80_ReadCallback)(uint16_t address);
typedef void    (*Z80_WriteCallback)(uint16_t address, uint8_t value);
typedef uint8_t (*Z80_IOReadCallback)(uint16_t port);
typedef void    (*Z80_IOWriteCallback)(uint16_t port, uint8_t value);

// Flag register bits
#define FLAG_C  0x01
#define FLAG_N  0x02
#define FLAG_PV 0x04
#define FLAG_X  0x08
#define FLAG_H  0x10
#define FLAG_Y  0x20
#define FLAG_Z  0x40
#define FLAG_S  0x80

class Z80 {
public:
    Z80();

    void begin(Z80_ReadCallback readMem, Z80_WriteCallback writeMem,
               Z80_IOReadCallback readIO, Z80_IOWriteCallback writeIO);
    void reset();

    int  execute(int cycles);
    void executeOne();
    int  step(); 

    void triggerNMI();
    void triggerINT(uint8_t vector = 0xFF);
    void setInterruptMode(uint8_t mode);

    inline uint16_t getPC()  const { return PC; }
    inline uint16_t getSP()  const { return SP; }
    inline uint8_t  getA()   const { return A; }
    inline uint8_t  getF()   const { return F; }
    inline uint16_t getAF()  const { return (A << 8) | F; }
    inline uint16_t getBC()  const { return (B << 8) | C; }
    inline uint16_t getDE()  const { return (D << 8) | E; }
    inline uint16_t getHL()  const { return (H << 8) | L; }
    inline uint16_t getIX()  const { return IX; }
    inline uint16_t getIY()  const { return IY; }
    inline uint8_t  getIM()  const { return IM; }

    void setPC(uint16_t v)  { PC = v; }
    void setSP(uint16_t v)  { SP = v; }
    void setAF(uint16_t v)  { A = v >> 8; F = v & 0xFF; }
    void setBC(uint16_t v)  { B = v >> 8; C = v & 0xFF; }
    void setDE(uint16_t v)  { D = v >> 8; E = v & 0xFF; }
    void setHL(uint16_t v)  { H = v >> 8; L = v & 0xFF; }

    bool IFF1, IFF2;
    bool halted;
    uint32_t totalCycles;

    uint8_t  IM;
    bool     pendingNMI;
    bool     pendingINT;
    bool     pendingEI;      //EI ritarda di un'istruzione
    uint8_t  intVector;

    // --- Setter aggiuntivi per il loader di snapshot (.z80) ---
    inline void setIX(uint16_t v) { IX = v; }
    inline void setIY(uint16_t v) { IY = v; }
    inline void setI(uint8_t v)   { I = v; }
    inline void setR(uint8_t v)   { R = v; }
    inline uint8_t getI() const   { return I; }
    inline uint8_t getR() const   { return R; }
    inline void setIM(uint8_t v)  { IM = v & 0x03; }

    inline void setShadowAF(uint16_t v) { A_ = v >> 8; F_ = v & 0xFF; }
    inline void setShadowBC(uint16_t v) { B_ = v >> 8; C_ = v & 0xFF; }
    inline void setShadowDE(uint16_t v) { D_ = v >> 8; E_ = v & 0xFF; }
    inline void setShadowHL(uint16_t v) { H_ = v >> 8; L_ = v & 0xFF; }

private:
    uint8_t  A, F;
    uint8_t  B, C, D, E, H, L;
    uint16_t PC, SP;
    uint16_t IX, IY;
    uint8_t  I, R;

    uint8_t A_, F_, B_, C_, D_, E_, H_, L_;

    Z80_ReadCallback    readMem;
    Z80_WriteCallback   writeMem;
    Z80_IOReadCallback  readIO;
    Z80_IOWriteCallback writeIO;

    static const uint8_t parityTable[256];
    static const uint8_t sz53Table[256];

    inline uint8_t readByte(uint16_t addr) {
        return readMem ? readMem(addr) : 0xFF;
    }
    inline void writeByte(uint16_t addr, uint8_t value) {
        if (writeMem) writeMem(addr, value);
    }
    inline uint16_t readWord(uint16_t addr) {
        return readByte(addr) | (readByte(addr + 1) << 8);
    }
    inline void writeWord(uint16_t addr, uint16_t value) {
        writeByte(addr,     value & 0xFF);
        writeByte(addr + 1, value >> 8);
    }

    inline void     push(uint16_t v) { SP -= 2; writeWord(SP, v); }
    inline uint16_t pop()            { uint16_t v = readWord(SP); SP += 2; return v; }

    inline uint8_t inPort(uint16_t port)               { return readIO  ? readIO(port)        : 0xFF; }
    inline void    outPort(uint16_t port, uint8_t val)  { if (writeIO) writeIO(port, val); }

    inline void setFlag(uint8_t f)        { F |=  f; }
    inline void clearFlag(uint8_t f)      { F &= ~f; }
    inline bool getFlag(uint8_t f) const  { return (F & f) != 0; }

    void    add8(uint8_t v);
    void    adc8(uint8_t v);
    void    sub8(uint8_t v);
    void    sbc8(uint8_t v);
    void    and8(uint8_t v);
    void    or8(uint8_t v);
    void    xor8(uint8_t v);
    void    cp8(uint8_t v);

    void    add16(uint16_t* reg, uint16_t v);
    void    adc16(uint16_t v);
    void    sbc16(uint16_t v);

    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);

    uint8_t rlc(uint8_t v);
    uint8_t rrc(uint8_t v);
    uint8_t rl(uint8_t v);
    uint8_t rr(uint8_t v);
    uint8_t sla(uint8_t v);
    uint8_t sra(uint8_t v);
    uint8_t sll(uint8_t v);
    uint8_t srl(uint8_t v);

   
    int exec(uint8_t opcode);               // Esegue un opcode già letto (senza incrementare R)
    int handleCB();                         // Legge il secondo byte e gestisce CB
    int handleED();                         // Legge il secondo byte e gestisce ED
    int handleDD();                         // Legge il secondo byte e gestisce DD
    int handleFD();                         // Legge il secondo byte e gestisce FD

    // Metodi di esecuzione specifici che ricevono l'opcode secondario già letto
    int executeCB(uint8_t op2);
    int executeED(uint8_t op2);
    int executeDDFD(bool isIX, uint8_t op2); // riceve l'opcode secondario

    // Metodi legacy (ora wrapper)
    int executeDD() { return handleDD(); }
    int executeFD() { return handleFD(); }

    // DDCB/FDCB (riceve op2)
    int executeDDFDCB(bool isIX, int8_t offset, uint8_t op2);
    int executeInstruction();

    inline bool checkCondition(uint8_t cc) {
        switch (cc) {
            case 0: return !getFlag(FLAG_Z);
            case 1: return  getFlag(FLAG_Z);
            case 2: return !getFlag(FLAG_C);
            case 3: return  getFlag(FLAG_C);
            case 4: return !getFlag(FLAG_PV);
            case 5: return  getFlag(FLAG_PV);
            case 6: return !getFlag(FLAG_S);
            case 7: return  getFlag(FLAG_S);
            default: return false;
        }
    }
};

#endif // Z80_H