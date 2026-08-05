#pragma once

class OpcodeDecoder
{
public:
    OpcodeDecoder(uint16_t *binCode, int *binCodePosition, char *opcodeDestination);
    bool Decode();
    static bool DecodeInstruction(uint16_t *binCode, int *binCodePosition, char *opcodeDestination);
private:
    enum class RegisterType : uint16_t
    {
        Data,
        Address
    };

    void AppendRegisterName(RegisterType registerType, uint16_t registerNumber);
    void AppendDataSizeMnemonic();
    void AppendEffectiveAddress();
    void AppendEffectiveAddress(uint16_t eaword);
    void AppendNumericValue();
    void AppendNumericValue(uint16_t byteSize);
    void AppendComma();
    void AppendSpace();
    void Append(const char *pszString);
    void AppendFormat(const char *pszFormat, int value);
    void AppendRegisterList(uint16_t bitmask, bool reverse);

    uint16_t ReadBinCode();
    void RollbackBinCode();

    uint16_t *mpbBincode;
    int *mpnBincodePosition;
    char *mszOpcodeDestination;
    
    uint16_t mnCurrentOpcode;
    uint16_t mnDataByteSize;

    typedef bool (OpcodeDecoder::*opcodeDecoder)();

	struct opcodedesc
    {
		opcodeDecoder decoder;
		uint16_t mask;
		uint16_t maskedValue;
	};

	static const opcodedesc maOpcodeTable[];
	static const char *maConditionalTestsTable[];

    bool Opcode_Tool_Reg_Address_Variant(const char *opcode);
    bool Opcode_Tool_Immediate_Address(const char *opcode);
    bool Opcode_Tool_BitChange(const char *opcode);
    bool Opcode_Tool_Branch(const char *opcode);
    bool Opcode_Tool_Cmp(const char *opcode);
    bool Opcode_Tool_MulDiv(const char *opcode);
    bool Opcode_Tool_Shift(const char *opcodebase);
    bool Opcode_Tool_SignExtension(const char *opcode);
    bool Opcode_Tool_OpeOperandWithSize(const char *opcode);
    bool Opcode_Tool_LogicItoCCR(const char *opcode);
    bool Opcode_Tools_AddASubA(const char *opcode);
    bool Opcode_AddSubQ(const char *opcode);

    bool Opcode_Nop();
    bool Opcode_Abcd();
    bool Opcode_Add();
    bool Opcode_AddA();
    bool Opcode_AddI();
    bool Opcode_AddQ();
    bool Opcode_And();
    bool Opcode_AndI();
    bool Opcode_AndItoCCR();
    bool Opcode_ASL_ASR();
    bool Opcode_Bcc();
    bool Opcode_Bchg();
    bool Opcode_Bclr();
    bool Opcode_Bra();
    bool Opcode_Bset();
    bool Opcode_Bsr();
    bool Opcode_Btst();
    bool Opcode_Clr();
    bool Opcode_Cmp();
    bool Opcode_CmpA();
    bool Opcode_CmpI();
    bool Opcode_AddX();
    bool Opcode_Chk();
    bool Opcode_CmpM();
    bool Opcode_DBcc();
    bool Opcode_Divs();
    bool Opcode_Divu();
    bool Opcode_Eor();
    bool Opcode_EorI();
    bool Opcode_EorItoCCR();
    bool Opcode_Exg();
    bool Opcode_Ext();
    bool Opcode_Illegal();
    bool Opcode_Jmp();
    bool Opcode_Jsr();
    bool Opcode_Lea();
    bool Opcode_Link();
    bool Opcode_LSL_LSR_reg();
    bool Opcode_LSL_LSR_mem();
    bool Opcode_Move();
    bool Opcode_MoveA();
    bool Opcode_MoveToCCR();
    bool Opcode_MoveFromSR();
    bool Opcode_MoveM();
    bool Opcode_MoveP();
    bool Opcode_MoveQ();
    bool Opcode_Muls();
    bool Opcode_Mulu();
    bool Opcode_Nbcd();
    bool Opcode_Neg();
    bool Opcode_Negx();
    bool Opcode_Not();
    bool Opcode_Or();
    bool Opcode_OrI();
    bool Opcode_OrItoCCR();
    bool Opcode_Pea();
    bool Opcode_ROL_ROR_reg();
    bool Opcode_ROL_ROR_mem();
    bool Opcode_ROXL_ROXR_reg();
    bool Opcode_ROXL_ROXR_mem();
    bool Opcode_Rtr();
    bool Opcode_Rte();
    bool Opcode_Rts();
    bool Opcode_Sbcd();
    bool Opcode_Scc();
    bool Opcode_Sub();
    bool Opcode_SubA();
    bool Opcode_SubI();
    bool Opcode_SubQ();
    bool Opcode_SubX();
    bool Opcode_Swap();
    bool Opcode_Tas();
    bool Opcode_Trap();
    bool Opcode_TrapV();
    bool Opcode_Tst();
    bool Opcode_Unlk();
};

/*
Skipped:

EffectiveAddesses



 */