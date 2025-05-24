#include <cstdint>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "OpcodeDecoder.h"

// https://github.com/mamedev/mame/blob/master/src/devices/cpu/m68000/m68kdasm.cpp

const char *OpcodeDecoder::maConditionalTestsTable[] =
{
    "t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"
};

OpcodeDecoder::OpcodeDecoder(uint16_t *binCode, int *binCodePosition, char *opcodeDestination)
: mpbBincode(binCode), mpnBincodePosition(binCodePosition), mszOpcodeDestination(opcodeDestination)
{
}

bool OpcodeDecoder::DecodeInstruction(uint16_t *binCode, int *binCodePosition, char *opcodeDestination)
{
    OpcodeDecoder decoder(binCode, binCodePosition, opcodeDestination);
    if (!decoder.Decode())
    {
        strcpy(opcodeDestination, "Unknown");
    }
    return strlen(opcodeDestination);
}

bool OpcodeDecoder::Decode()
{
    // Clear opcode litteral version
    mszOpcodeDestination[0] = 0;

    // Read opcode
    mnCurrentOpcode = ReadBinCode();
    
    // Iterate through opcode table
    int iOpCodeIndex;
    for (iOpCodeIndex = 0; maOpcodeTable[iOpCodeIndex].decoder != NULL; ++iOpCodeIndex)
    {
        opcodedesc desc = maOpcodeTable[iOpCodeIndex];
        if ((mnCurrentOpcode & desc.mask) == desc.maskedValue)
        {
            // The opcode matches the mask
            if ((this->*desc.decoder)())
            {
                // Instruction has been decoded
                break;
            }
        }
    }

    return maOpcodeTable[iOpCodeIndex].decoder != NULL;
}

void OpcodeDecoder::AppendRegisterName(OpcodeDecoder::RegisterType registerType, uint16_t registerNumber)
{
    sprintf(mszOpcodeDestination + strlen(mszOpcodeDestination), "%c%d", registerType == RegisterType::Data ? 'd' : 'a', registerNumber);
}

void OpcodeDecoder::AppendDataSizeMnemonic()
{
    switch (mnDataByteSize)
    {
        case 1: Append(".b"); break;
        case 2: Append(".w"); break;
        case 4: Append(".l"); break;
        default: return;
    }
    Append(" ");
}

void OpcodeDecoder::AppendComma()
{
    Append(",");
}

void OpcodeDecoder::AppendSpace()
{
    Append(" ");
}

void OpcodeDecoder::AppendFormat(const char *pszFormat, int value)
{
    sprintf(mszOpcodeDestination + strlen(mszOpcodeDestination), pszFormat, value);
}

void OpcodeDecoder::Append(const char *pszString)
{
    strcat(mszOpcodeDestination, pszString);
}

void OpcodeDecoder::AppendEffectiveAddress()
{
    AppendEffectiveAddress(mnCurrentOpcode);
}

void OpcodeDecoder::AppendEffectiveAddress(uint16_t eaword)
{
    uint16_t eamode = ((eaword >> 3) & 0b111 );
    uint16_t eareg = ((eaword >> 0) & 0b111 );

    switch (eamode)
    {
        case 0b000:
            AppendRegisterName(RegisterType::Data, eareg);
            break;
        case 0b001:
            AppendRegisterName(RegisterType::Address, eareg);
            break;
        case 0b010:
        case 0b011:
        case 0b100:
            if (eamode == 0b100) Append("-");
            Append("("); AppendRegisterName(RegisterType::Address, eareg); Append(")");
            if (eamode == 0b011) Append("+");
            break;
        case 0b101:
            AppendFormat("(%+d,", (int16_t)ReadBinCode()); AppendRegisterName(RegisterType::Address, eareg); Append(")");
            break;
        case 0b110:
            {
                uint16_t extension = ReadBinCode();
                RegisterType regType = (extension & 1 << 15) ? RegisterType::Address : RegisterType::Data;
                uint16_t regNum = (extension >> 12) & 0b111;
                uint16_t dataSize = 1 << (((extension >> 11) & 0b1) + 1);
                int16_t displacement = (int16_t)(extension & 0xff);
                AppendFormat("(%+d,",displacement);
                AppendRegisterName(RegisterType::Address, eareg);
                AppendComma();
                AppendRegisterName(regType, regNum);
                Append(dataSize == 2 ? "w" : "l");
                Append(")");
            }
            break;
        case 0b111:
            if (eareg & 0b100)
            {
                // #imm
                Append("#");
                AppendNumericValue();
            }
            else
            {
                // (xxx).W/L
                Append("(");
                if (eareg & 0b001)
                {
                    // (xxx).L
                    AppendNumericValue(4);
                    Append(").l");
                }
                else
                {
                    // (xxx).W
                    AppendNumericValue(2);
                    Append(").w");
                }
            }
            break;
    }
}

void OpcodeDecoder::AppendNumericValue(uint16_t byteSize)
{
    switch (byteSize)
    {
        case 1:
        case 2:
            sprintf(mszOpcodeDestination + strlen(mszOpcodeDestination), "$%x", mpbBincode[*mpnBincodePosition]);
            ++(*mpnBincodePosition);
            break;
        case 4:
            if (mpbBincode[*mpnBincodePosition] == 0)
            {
                // The first digit is 0, skip it to avoid showing a heading 0
                sprintf(mszOpcodeDestination + strlen(mszOpcodeDestination), "$%x", mpbBincode[*mpnBincodePosition + 1]);
            }
            else
            {
                sprintf(mszOpcodeDestination + strlen(mszOpcodeDestination), "$%x%04x", mpbBincode[*mpnBincodePosition], mpbBincode[*mpnBincodePosition + 1]);
            }
            (*mpnBincodePosition) += 2;
            break;
    }
}

void OpcodeDecoder::AppendNumericValue()
{
    AppendNumericValue(mnDataByteSize);
}

void OpcodeDecoder::AppendRegisterList(uint16_t bitmask, bool reverse)
{
    //printf("\n%d\n%04x\n\n", reverse, bitmask);

    bool bFirstReg = true;
    for (int nRegtype = 0; nRegtype < 2; ++nRegtype)
    {
        RegisterType eRegType = (nRegtype == 0 && !reverse || nRegtype == 1 && reverse) ? RegisterType::Data : RegisterType::Address;
        for (int nRegIndex = 0; nRegIndex < 8 ; ++nRegIndex)
        {
            int bitmaskIndex = reverse ? 7 - nRegIndex : nRegIndex;
            if (bitmask & (1 << (bitmaskIndex + nRegtype * 8)))
            {
                if (!bFirstReg)
                    Append("/");
                AppendRegisterName(eRegType, nRegIndex);
                bFirstReg = false;
            }
        }
    }
}

uint16_t OpcodeDecoder::ReadBinCode()
{
    uint16_t value = mpbBincode[*mpnBincodePosition];
    ++(*mpnBincodePosition);
    return value;
}

void OpcodeDecoder::RollbackBinCode()
{
    --(*mpnBincodePosition);
}

bool OpcodeDecoder::Opcode_Tool_Reg_Address_Variant(const char *opcode)
{
    mnDataByteSize = (1 << ((mnCurrentOpcode >> 6) & 0b11));
    if (mnDataByteSize > 4)
        return false;
    Append(opcode);
    AppendDataSizeMnemonic();
    if ((mnCurrentOpcode >> 8) & 0b1)
    {
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
        AppendComma();
        AppendEffectiveAddress();
    }
    else
    {
        AppendEffectiveAddress();
        AppendComma();
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    }
    return true;
}

bool OpcodeDecoder::Opcode_Tool_Immediate_Address(const char *opcode)
{
    uint16_t sizeCode = ((mnCurrentOpcode >> 6) & 0b11);
    if (sizeCode == 0b11)
        return false;
    mnDataByteSize = (1 << sizeCode);
    Append(opcode);
    AppendDataSizeMnemonic();
    Append("#");
    AppendNumericValue();
    AppendComma();
    AppendEffectiveAddress();

    return true;
}

bool OpcodeDecoder::Opcode_Tool_Branch(const char *opcode)
{
    int16_t displacement = (int8_t)(mnCurrentOpcode& 0xff);
    if (!displacement)
    {
        displacement = ReadBinCode();
    }
    Append(opcode);
    AppendFormat(" %+d", displacement);
    return true;
}

bool OpcodeDecoder::Opcode_Tool_BitChange(const char *opcode)
{
    if (((mnCurrentOpcode >> 8) & 0b1))
    {
        // Dynamic mode
        Append(opcode);
        AppendSpace();
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
        Append(",");
        AppendEffectiveAddress();
    }
    else
    {
        // Static mode
        if ((mnCurrentOpcode >> 9) & 0b111 != 0b100)
            return false;
        uint16_t bitNumber = ReadBinCode();
        if (bitNumber > 0x00ff)
        {
            RollbackBinCode();
            return false;
        }
        Append(opcode);
        AppendFormat(" #%d,", bitNumber);
        AppendEffectiveAddress();
    }
    return true;
}

bool OpcodeDecoder::Opcode_Tool_Cmp(const char *opcode)
{
    Append(opcode);
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    AppendComma();
    AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Tool_MulDiv(const char *opcode)
{
    Append(opcode);
    AppendSpace();
    mnDataByteSize = 2;
    AppendEffectiveAddress();
    AppendComma();
    AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Tool_Shift(const char *opcodebase)
{
    uint16_t sizeCode = ((mnCurrentOpcode >> 6) & 0b11);
    if (sizeCode == 0b11)
        return false;
    mnDataByteSize = (1 << sizeCode);
    Append(opcodebase);
    if ((mnCurrentOpcode >> 8)&0b1)
        Append("l");
    else    
        Append("r");
    AppendDataSizeMnemonic();
    if ((mnCurrentOpcode >> 5) & 0b1)
    {
        // Register
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    }
    else
    {
        // Direct value
        uint16_t value = (mnCurrentOpcode >> 9)&0b111;
        AppendFormat("#%x", value ? value : 8);
    }
    Append(",");
    AppendRegisterName(RegisterType::Data, mnCurrentOpcode & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Tool_SignExtension(const char *opcode)
{
    mnDataByteSize = (1 << ((mnCurrentOpcode >> 6) & 0b11));
    if (mnDataByteSize > 4)
        return false;
    Append(opcode);
    AppendDataSizeMnemonic();
    if ((mnCurrentOpcode >> 3) & 0b1)
    {
        // Memory to memory
        Append("-(");
        AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 0) & 0b111);
        Append("),-(");
        AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 9) & 0b111);
        Append(")");
    }
    else
    {
        // Data register to data register
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 0) & 0b111);
        Append(",");
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    }

    return true;
}

bool OpcodeDecoder::Opcode_Tool_OpeOperandWithSize(const char *opcode)
{
    mnDataByteSize = (1 << ((mnCurrentOpcode >> 6) & 0b11));
    if (mnDataByteSize > 4)
        return false;
    Append(opcode);
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Tool_LogicItoCCR(const char *opcode)
{
    uint16_t value = ReadBinCode();
    if (value > 0xff)
    {
        RollbackBinCode();
        return false;
    }
    Append(opcode);
    AppendFormat(" #%x,CCR", value);
    return true;
}

bool OpcodeDecoder::Opcode_Tools_AddASubA(const char *opcode)
{
    mnDataByteSize = (((mnCurrentOpcode >> 8) & 0b1) == 0 ? 2 : 4);
    Append(opcode);
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    AppendComma();
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 9) & 0b111);

    return true;
}

bool OpcodeDecoder::Opcode_AddSubQ(const char *opcode)
{
    uint16_t sizeCode = ((mnCurrentOpcode >> 6) & 0b11);
    if (sizeCode == 0b11)
        return false;
    mnDataByteSize = (1 << sizeCode);
    Append(opcode);
    AppendDataSizeMnemonic();
    uint16_t value = (mnCurrentOpcode >> 9)&0b111;
    AppendFormat("#%x", value ? value : 8);
    AppendComma();
    AppendEffectiveAddress();

    return true;
}

bool OpcodeDecoder::Opcode_Nop()
{
    Append("nop");
    return true;
}

bool OpcodeDecoder::Opcode_Abcd()
{
    return Opcode_Tool_SignExtension("abcd");
}

bool OpcodeDecoder::Opcode_Add()
{
    return Opcode_Tool_Reg_Address_Variant("add");
}

bool OpcodeDecoder::Opcode_AddA()
{
    return Opcode_Tools_AddASubA("adda");
}

bool OpcodeDecoder::Opcode_AddI()
{
    return Opcode_Tool_Immediate_Address("addi");
}

bool OpcodeDecoder::Opcode_AddQ()
{
    return Opcode_AddSubQ("addq");
}

bool OpcodeDecoder::Opcode_And()
{
    if ((mnCurrentOpcode >> 8) & 0b1 && ((mnCurrentOpcode >> 4) & 0b11) == 0)
        return false;
    return Opcode_Tool_Reg_Address_Variant("and");
}

bool OpcodeDecoder::Opcode_AndI()
{
    return Opcode_Tool_Immediate_Address("andi");
}

bool OpcodeDecoder::Opcode_AndItoCCR()
{
    return Opcode_Tool_LogicItoCCR("andi");
}

bool OpcodeDecoder::Opcode_ASL_ASR()
{
    return Opcode_Tool_Shift("as");
}

bool OpcodeDecoder::Opcode_Bcc()
{
    uint16_t condition = ((mnCurrentOpcode >> 8) & 0b1111);
    if (condition < 2)
        return false;
    int16_t displacement = (int8_t)(mnCurrentOpcode& 0xff);
    if (!displacement)
    {
        displacement = ReadBinCode();
    }
    Append("b");
    Append(maConditionalTestsTable[condition]);
    AppendFormat(" %+d", displacement);
    return true;
}

bool OpcodeDecoder::Opcode_Bchg()
{
    return Opcode_Tool_BitChange("bchg");
}

bool OpcodeDecoder::Opcode_Bclr()
{
    return Opcode_Tool_BitChange("bclr");
}

bool OpcodeDecoder::Opcode_Bra()
{
    return Opcode_Tool_Branch("bra");
}

bool OpcodeDecoder::Opcode_Bset()
{
    return Opcode_Tool_BitChange("bset");
}

bool OpcodeDecoder::Opcode_Bsr()
{
    return Opcode_Tool_Branch("bsr");
}

bool OpcodeDecoder::Opcode_Btst()
{
    return Opcode_Tool_BitChange("btst");
}

bool OpcodeDecoder::Opcode_Clr()
{
    mnDataByteSize = (1 << ((mnCurrentOpcode >> 6) & 0b11));
    if (mnDataByteSize > 4)
        return false;
    Append("clr");
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Cmp()
{
    mnDataByteSize = (1 << ((mnCurrentOpcode >> 6) & 0b11));
    if (mnDataByteSize > 4)
        return false;
    return Opcode_Tool_Cmp("cmp");
}

bool OpcodeDecoder::Opcode_CmpA()
{
    mnDataByteSize = (1 << (((mnCurrentOpcode >> 8) & 0b1) + 1));
    Append("cmpa");
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    AppendComma();
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 9) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_CmpI()
{
    return Opcode_Tool_Immediate_Address("cmpi");
}

bool OpcodeDecoder::Opcode_AddX()
{
    return Opcode_Tool_SignExtension("addx");
}

bool OpcodeDecoder::Opcode_Chk()
{
    mnDataByteSize = 2;
    return Opcode_Tool_Cmp("chk");
}

bool OpcodeDecoder::Opcode_CmpM()
{
    mnDataByteSize = (1 << ((mnCurrentOpcode >> 6) & 0b11));
    if (mnDataByteSize > 4)
        return false;
    Append("cmpm");
    AppendDataSizeMnemonic();
    Append("(");
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 2) & 0b111);
    Append(")+,(");
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 9) & 0b111);
    Append(")+");
    return true;
}

bool OpcodeDecoder::Opcode_DBcc()
{
    Append("db");
    uint16_t conditionIndex = (mnCurrentOpcode >> 8) & 0b1111;
    Append(conditionIndex == 1 ? "ra" : maConditionalTestsTable[conditionIndex]);
    AppendSpace();
    AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 0) & 0b111);
    AppendFormat(",%+d", (int16_t)ReadBinCode());
    return true;
}

bool OpcodeDecoder::Opcode_Divs()
{
    return Opcode_Tool_MulDiv("divs");
}

bool OpcodeDecoder::Opcode_Divu()
{
    return Opcode_Tool_MulDiv("divu");
}

bool OpcodeDecoder::Opcode_Eor()
{
    return Opcode_Tool_Reg_Address_Variant("eor");
}

bool OpcodeDecoder::Opcode_EorI()
{
    return Opcode_Tool_Immediate_Address("eori");
}

bool OpcodeDecoder::Opcode_EorItoCCR()
{
    uint16_t value = ReadBinCode();
    if (value > 0xff)
    {
        RollbackBinCode();
        return false;
    }
    AppendFormat("eori #%x,CCR", value);
    return true;
}

bool OpcodeDecoder::Opcode_Exg()
{
    RegisterType regSource,regTarget;
    switch ((mnCurrentOpcode >> 3) & 0b11111)
    {
    case 0b01000:
        regSource = RegisterType::Data;
        regTarget = RegisterType::Data;
        break;
    case 0b01001:
        regSource = RegisterType::Address;
        regTarget = RegisterType::Address;
        break;
    case 0b10001:
        regSource = RegisterType::Data;
        regTarget = RegisterType::Address;
        break;
    default:
        return false;
    }
    Append("exg ");
    AppendRegisterName(regSource, (mnCurrentOpcode >> 9) & 0b111);
    AppendComma();
    AppendRegisterName(regTarget, (mnCurrentOpcode >> 0) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Ext()
{
    Append("ext");
    mnDataByteSize = 1 << (((mnCurrentOpcode >> 6) & 0b1) + 1);
    AppendDataSizeMnemonic();
    AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 0) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Illegal()
{
    Append("illegal");
    return true;
}

bool OpcodeDecoder::Opcode_Jmp()
{
    Append("jmp ");
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Jsr()
{
    Append("jsr ");
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Lea()
{
    Append("lea ");
    AppendEffectiveAddress();
    AppendComma();
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 9) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Link()
{
    Append("link ");
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 0) & 0b111);
    Append(",#");
    AppendNumericValue(2);
    return true;
}

bool OpcodeDecoder::Opcode_LSL_LSR_reg()
{
    return Opcode_Tool_Shift("ls");
}

bool OpcodeDecoder::Opcode_LSL_LSR_mem()
{
    Append(mnCurrentOpcode & (1 << 8) ? "lsl " : "lsr ");
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Move()
{
    switch ((mnCurrentOpcode >> 12) & 0b11)
    {
        case 0b01: mnDataByteSize = 1; break;
        case 0b11: mnDataByteSize = 2; break;
        case 0b10: mnDataByteSize = 4; break;
        default: return false;
    }
    Append("move");
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    AppendComma();
    AppendEffectiveAddress((mnCurrentOpcode >> 9 & 0b111) | ((mnCurrentOpcode >> 6 & 0b111) << 3));
    return true;
}

bool OpcodeDecoder::Opcode_MoveA()
{
    Append("movea");
    mnDataByteSize = 1 << (((~(mnCurrentOpcode >> 12)) & 0b1) + 1);
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    AppendComma();
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 9) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_MoveToCCR()
{
    Append("move ");
    mnDataByteSize = 2;
    AppendEffectiveAddress();
    Append(",ccr");
    return true;
}

bool OpcodeDecoder::Opcode_MoveFromSR()
{
    Append("move sr,");
    mnDataByteSize = 2;
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_MoveM()
{
    bool bMemToReg = ((mnCurrentOpcode >> 10) & 0b1);
    // Need to eliminate invalid ea modes to avoid conflict with EXT opcode!
    if (bMemToReg)
    {
        switch ((mnCurrentOpcode >> 3) & 0b111)
        {
            case 0b010: case 0b011: case 0b101: case 0b110: case 0b111: break;
            default: return false;
        }
    }
    else
        switch ((mnCurrentOpcode >> 3) & 0b111)
        {
            case 0b010: case 0b100: case 0b101: case 0b110: case 0b111: break;
            default: return false;
        }
    Append("movem");
    mnDataByteSize = 1 << (((mnCurrentOpcode >> 6) & 0b1) + 1);
    AppendDataSizeMnemonic();
    bool bPredecrement = (((mnCurrentOpcode >> 3) & 0b111) == 0b100);
    uint16_t bitmask = ReadBinCode();
    if (bMemToReg)
    {
        AppendEffectiveAddress();
        AppendComma();
        AppendRegisterList(bitmask, bPredecrement);
    }
    else
    {
        AppendRegisterList(bitmask, bPredecrement);
        AppendComma();
        AppendEffectiveAddress();
    }
    return true;
}

bool OpcodeDecoder::Opcode_MoveP()
{
    Append("movep");
    bool bMemToReg = !((mnCurrentOpcode >> 7) & 0b1);
    mnDataByteSize = 1 << (((mnCurrentOpcode >> 6) & 0b1) + 1);
    AppendDataSizeMnemonic();
    if (bMemToReg)
    {
        AppendFormat("($%x,", ReadBinCode());
        AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 0) & 0b111);
        Append("),");
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    }
    else
    {
        AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
        AppendFormat(",($%x,", ReadBinCode());
        AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 0) & 0b111);
        Append(")");

    }
    return true;
}

bool OpcodeDecoder::Opcode_MoveQ()
{
    AppendFormat("moveq.l #$%x,", (mnCurrentOpcode >> 0) & 0xff);
    AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 9) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Muls()
{
    return Opcode_Tool_MulDiv("muls");
}

bool OpcodeDecoder::Opcode_Mulu()
{
    return Opcode_Tool_MulDiv("mulu");
}

bool OpcodeDecoder::Opcode_Nbcd()
{
    Append("nbcd.b ");
    mnDataByteSize = 1;
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Neg()
{
    return Opcode_Tool_OpeOperandWithSize("neg");
}

bool OpcodeDecoder::Opcode_Negx()
{
    return Opcode_Tool_OpeOperandWithSize("negx");
}

bool OpcodeDecoder::Opcode_Not()
{
    return Opcode_Tool_OpeOperandWithSize("not");
}

bool OpcodeDecoder::Opcode_Or()
{
    if ((mnCurrentOpcode >> 8) & 0b1 && ((mnCurrentOpcode >> 4) & 0b11) == 0)
        return false;
    return Opcode_Tool_Reg_Address_Variant("or");
}

bool OpcodeDecoder::Opcode_OrI()
{
    return Opcode_Tool_Immediate_Address("ori");
}

bool OpcodeDecoder::Opcode_OrItoCCR()
{
    return Opcode_Tool_LogicItoCCR("ori");
}

bool OpcodeDecoder::Opcode_Pea()
{
    Append("pea ");
    mnDataByteSize = 4;
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_ROL_ROR_reg()
{
    return Opcode_Tool_Shift("ro");
}

bool OpcodeDecoder::Opcode_ROL_ROR_mem()
{
    Append(mnCurrentOpcode & (1 << 8) ? "rol " : "ror ");
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_ROXL_ROXR_reg()
{
    return Opcode_Tool_Shift("rox");
}

bool OpcodeDecoder::Opcode_ROXL_ROXR_mem()
{
    Append(mnCurrentOpcode & (1 << 8) ? "roxl " : "roxr ");
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Rtr()
{
    Append("rtr");
    return true;
}

bool OpcodeDecoder::Opcode_Rts()
{
    Append("rts");
    return true;
}

bool OpcodeDecoder::Opcode_Sbcd()
{
    return Opcode_Tool_SignExtension("sbcd");
}

bool OpcodeDecoder::Opcode_Scc()
{
    if (((mnCurrentOpcode >> 3) & 0b111) == 0b001)
        return false;
    mnDataByteSize = 1;
    Append("scc");
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Sub()
{
    return Opcode_Tool_Reg_Address_Variant("sub");
}

bool OpcodeDecoder::Opcode_SubA()
{
    return Opcode_Tools_AddASubA("suba");
}

bool OpcodeDecoder::Opcode_SubI()
{
    return Opcode_Tool_Immediate_Address("subi");
}

bool OpcodeDecoder::Opcode_SubQ()
{
    return Opcode_AddSubQ("subq");
}

bool OpcodeDecoder::Opcode_SubX()
{
    return Opcode_Tool_Reg_Address_Variant("subx");
}

bool OpcodeDecoder::Opcode_Swap()
{
    Append("swap ");
    AppendRegisterName(RegisterType::Data, (mnCurrentOpcode >> 0) & 0b111);
    return true;
}

bool OpcodeDecoder::Opcode_Tas()
{
    mnDataByteSize = 1;
    Append("tas");
    AppendDataSizeMnemonic();
    AppendEffectiveAddress();
    return true;
}

bool OpcodeDecoder::Opcode_Trap()
{
    AppendFormat("trap #%d", (mnCurrentOpcode >> 0) & 0b1111);
    return true;
}

bool OpcodeDecoder::Opcode_TrapV()
{
    Append("trapv");
    return true;
}

bool OpcodeDecoder::Opcode_Tst()
{
    return Opcode_Tool_OpeOperandWithSize("tst");
}

bool OpcodeDecoder::Opcode_Unlk()
{
    Append("unlk ");
    AppendRegisterName(RegisterType::Address, (mnCurrentOpcode >> 0) & 0b111);
    return true;
}

const OpcodeDecoder::opcodedesc OpcodeDecoder::maOpcodeTable[] =
{
    {&OpcodeDecoder::Opcode_Unlk,           0b1111111111111000, 0b0100111001011000 },
    {&OpcodeDecoder::Opcode_Illegal,        0b1111111111111111, 0b0100101011111100 },
    {&OpcodeDecoder::Opcode_TrapV,          0b1111111111111111, 0b0100111001110110 },
    {&OpcodeDecoder::Opcode_Rts,            0b1111111111111111, 0b0100111001110101 },
    {&OpcodeDecoder::Opcode_Rtr,            0b1111111111111111, 0b0100111001110111 },
    {&OpcodeDecoder::Opcode_OrItoCCR,       0b1111111111111111, 0b0000000000111100 },
    {&OpcodeDecoder::Opcode_EorItoCCR,      0b1111111111111111, 0b0000101000111100 },
    {&OpcodeDecoder::Opcode_AndItoCCR,      0b1111111111111111, 0b0000001000111100 },
    {&OpcodeDecoder::Opcode_Tst,            0b1111111100000000, 0b0100101000000000 },
    {&OpcodeDecoder::Opcode_Trap,           0b1111111111110000, 0b0100111001000000 },
    {&OpcodeDecoder::Opcode_Tas,            0b1111111111000000, 0b0100101011000000 },
    {&OpcodeDecoder::Opcode_Swap,           0b1111111111111000, 0b0100100001000000 },
    {&OpcodeDecoder::Opcode_SubX,           0b1111000100110000, 0b1001000100000000 },
    {&OpcodeDecoder::Opcode_SubQ,           0b1111000100000000, 0b0101000100000000 },
    {&OpcodeDecoder::Opcode_SubI,           0b1111111100000000, 0b0000010000000000 },
    {&OpcodeDecoder::Opcode_SubA,           0b1111000011000000, 0b1001000011000000 },
    {&OpcodeDecoder::Opcode_Sub,            0b1111000000000000, 0b1001000000000000 },
    {&OpcodeDecoder::Opcode_Scc,            0b1111000011000000, 0b0101000011000000 },
    {&OpcodeDecoder::Opcode_Sbcd,           0b1111000111110000, 0b1000000100000000 },
    {&OpcodeDecoder::Opcode_ROXL_ROXR_reg,  0b1111000000011000, 0b1110000000010000 },
    {&OpcodeDecoder::Opcode_ROXL_ROXR_mem,  0b1111111011000000, 0b1110010011000000 },
    {&OpcodeDecoder::Opcode_ROL_ROR_reg,    0b1111000000011000, 0b1110000000011000 },
    {&OpcodeDecoder::Opcode_ROL_ROR_mem,    0b1111111011000000, 0b1110011011000000 },
    {&OpcodeDecoder::Opcode_Pea,            0b1111111111000000, 0b0100100001000000 },
    {&OpcodeDecoder::Opcode_OrI,            0b1111111100000000, 0b0000000000000000 },
    {&OpcodeDecoder::Opcode_Or,             0b1111000000000000, 0b1000000000000000 },
    {&OpcodeDecoder::Opcode_Not,            0b1111111100000000, 0b0100011000000000 },
    {&OpcodeDecoder::Opcode_Negx,           0b1111111100000000, 0b0100000000000000 },
    {&OpcodeDecoder::Opcode_Neg,            0b1111111100000000, 0b0100010000000000 },
    {&OpcodeDecoder::Opcode_Nbcd,           0b1111111111000000, 0b0100100000000000 },
    {&OpcodeDecoder::Opcode_Mulu,           0b1111000111000000, 0b1100000011000000 },
    {&OpcodeDecoder::Opcode_Muls,           0b1111000111000000, 0b1100000111000000 },
    {&OpcodeDecoder::Opcode_MoveQ,          0b1111000100000000, 0b0111000000000000 },
    {&OpcodeDecoder::Opcode_MoveP,          0b1111000100111000, 0b0000000100001000 },
    {&OpcodeDecoder::Opcode_MoveM,          0b1111101110000000, 0b0100100010000000 },
    {&OpcodeDecoder::Opcode_MoveFromSR,     0b1111111111000000, 0b0100000011000000 },
    {&OpcodeDecoder::Opcode_MoveToCCR,      0b1111111111000000, 0b0100010011000000 },
    {&OpcodeDecoder::Opcode_MoveA,          0b1110000111000000, 0b0010000001000000 },
    {&OpcodeDecoder::Opcode_LSL_LSR_reg,    0b1111000000011000, 0b1110000000001000 },
    {&OpcodeDecoder::Opcode_LSL_LSR_mem,    0b1111111011000000, 0b1110001011000000 },
    {&OpcodeDecoder::Opcode_Link,           0b1111111111111000, 0b0100111001010000 },
    {&OpcodeDecoder::Opcode_Lea,            0b1111000111000000, 0b0100000111000000 },
    {&OpcodeDecoder::Opcode_Jsr,            0b1111111111000000, 0b0100111010000000 },
    {&OpcodeDecoder::Opcode_Jmp,            0b1111111111000000, 0b0100111011000000 },
    {&OpcodeDecoder::Opcode_Ext,            0b1111111110111000, 0b0100100010000000 },
    {&OpcodeDecoder::Opcode_Exg,            0b1111000100000000, 0b1100000100000000 },
    {&OpcodeDecoder::Opcode_EorI,           0b1111111100000000, 0b0000101000000000 },
    {&OpcodeDecoder::Opcode_Eor,            0b1111000100000000, 0b1011000100000000 },
    {&OpcodeDecoder::Opcode_Divu,           0b1111000111000000, 0b1000000011000000 },
    {&OpcodeDecoder::Opcode_Divs,           0b1111000111000000, 0b1000000111000000 },
    {&OpcodeDecoder::Opcode_DBcc,           0b1111000011111000, 0b0101000011001000 },
    {&OpcodeDecoder::Opcode_CmpM,           0b1111000100111000, 0b1011000100001000 },
    {&OpcodeDecoder::Opcode_Chk,            0b1111000111000000, 0b0100000110000000 },
    {&OpcodeDecoder::Opcode_AddX,           0b1101000100110000, 0b1101000100000000 },
    {&OpcodeDecoder::Opcode_CmpI,           0b1111111100000000, 0b0000110000000000 },
    {&OpcodeDecoder::Opcode_CmpA,           0b1011000011000000, 0b1011000011000000 },
    {&OpcodeDecoder::Opcode_Cmp,            0b1111000100000000, 0b1011000000000000 },
    {&OpcodeDecoder::Opcode_Clr,            0b1111111100000000, 0b0100001000000000 },
    {&OpcodeDecoder::Opcode_Btst,           0b1111000011000000, 0b0000000000000000 },
    {&OpcodeDecoder::Opcode_Bsr,            0b1111111100000000, 0b0110000100000000 },
    {&OpcodeDecoder::Opcode_Bclr,           0b1111000011000000, 0b0000000010000000 },
    {&OpcodeDecoder::Opcode_Bset,           0b1111000011000000, 0b0000000011000000 },
    {&OpcodeDecoder::Opcode_Bchg,           0b1111000011000000, 0b0000000001000000 },
    {&OpcodeDecoder::Opcode_Nop,            0b0100111001110001, 0b0100111001110001 },
    {&OpcodeDecoder::Opcode_Bcc,            0b1111000000000000, 0b0110000000000000 },
    {&OpcodeDecoder::Opcode_Bra,            0b1111111100000000, 0b0110000000000000 },
    {&OpcodeDecoder::Opcode_AndI,           0b1111111100000000, 0b0000001000000000 },
    {&OpcodeDecoder::Opcode_And,            0b1111000000000000, 0b1100000000000000 },
    {&OpcodeDecoder::Opcode_AddQ,           0b1111000100000000, 0b0101000000000000 },
    {&OpcodeDecoder::Opcode_AddI,           0b1111111100000000, 0b0000011000000000 },
    {&OpcodeDecoder::Opcode_AddA,           0b1111000011000000, 0b1101000011000000 },
    {&OpcodeDecoder::Opcode_Add,            0b1111000000000000, 0b1101000000000000 },
    {&OpcodeDecoder::Opcode_ASL_ASR,        0b1111000000011000, 0b1110000000000000 },
    {&OpcodeDecoder::Opcode_Abcd,           0b1111000111110000, 0b1100000100000000 },
    {&OpcodeDecoder::Opcode_Move,           0b1100000000000000, 0b0000000000000000 },
    {NULL, 0, 0}
};
