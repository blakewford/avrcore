#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#ifdef PROFILE
#include <chrono>
using namespace std::chrono;
#endif

int32_t cachedArgc = 0;
char argvStorage[1024];
char* cachedArgv[64];

//Platform Defines
#define ATMEGA32U4_FLASH_SIZE 32*1024
#define ATMEGA32U4_ENTRY 0xB00
#define ATMEGA32U4_PORTB_ADDRESS 0x25
#define IO_REG_START 0x20
#define SREG_ADDRESS 0x5F

//Globals
#define CLR 0
#define SET 1
#define IGNORE 2
struct status
{
    status()
    {
        C = IGNORE;
        Z = IGNORE;
        N = IGNORE;
        V = IGNORE;
        S = IGNORE;
        H = IGNORE;
        T = IGNORE;
        I = IGNORE;
    }
    int8_t C:3;
    int8_t Z:3;
    int8_t N:3;
    int8_t V:3;
    int8_t S:3;
    int8_t H:3;
    int8_t T:3;
    int8_t I:3;
};
uint8_t memory[ATMEGA32U4_FLASH_SIZE];
int32_t programStart = ATMEGA32U4_ENTRY;
int32_t stackPointer = programStart - 1;
uint16_t PC;
status SREG;

//API
void engineInit();
void loadProgram();
void execProgram();
void writeMemory(int32_t address, int32_t value);
void pushStatus(status& newStatus);

extern "C" int32_t workFlow()
{
    loadProgram();

#ifdef PROFILE
    microseconds startProfile = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
#endif

    execProgram();
    printf("Program Ended at Address 0x%X\n", PC);

#ifdef PROFILE
    microseconds endProfile = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
    printf("Execution Time in Microseconds %lld\n", (long long)(endProfile.count()-startProfile.count()));
#endif

    return 0;
}

int32_t main(int32_t argc, char** argv)
{
    cachedArgc = argc;
    char* storagePointer = argvStorage;
    while(argc--)
    {
        cachedArgv[argc] = storagePointer;
        int32_t length = strlen(argv[argc]);
        strcat(storagePointer, argv[argc]);
        storagePointer+=(length+1);
    }

    return workFlow();
}

void writeMemory(int32_t address, int32_t value)
{
    switch(address)
    {
        case ATMEGA32U4_PORTB_ADDRESS:
            printf("PortB 0x%X\n", value);
            break;
    }
    memory[address] = value;
}

void pushStatus(status& newStatus)
{
    if(newStatus.C != IGNORE)
    {
        SREG.C = newStatus.C;
    }
    if(newStatus.Z != IGNORE)
    {
        SREG.Z = newStatus.Z;
    }
    if(newStatus.N != IGNORE)
    {
        SREG.N = newStatus.N;
    }
    if(newStatus.V != IGNORE)
    {
        SREG.V = newStatus.V;
    }
    if(newStatus.S != IGNORE)
    {
        SREG.S = newStatus.S;
    }
    if(newStatus.H != IGNORE)
    {
        SREG.H = newStatus.H;
    }
    if(newStatus.T != IGNORE)
    {
         SREG.T = newStatus.T;
    }
    if(newStatus.I != IGNORE)
    {
         SREG.I = newStatus.I;
    }
}

void engineInit()
{
    SREG.C = CLR;
    SREG.Z = CLR;
    SREG.N = CLR;
    SREG.V = CLR;
    SREG.S = CLR;
    SREG.H = CLR;
    SREG.T = CLR;
    SREG.I = CLR;
}

int32_t getValueFromHex(uint8_t* buffer, int32_t size)
{
    int32_t value = 0;
    int32_t cursor = 0;
    while(size--)
    {
        int32_t shift = (1 << size*4);
        if(buffer[cursor] < ':')
        {
            value += (buffer[cursor++] - '0')*shift;
        }
        else
        {
            value += (buffer[cursor++] - 0x37)*shift;
        }
    }

    return value;
}

void loadProgram()
{
    FILE* executable = NULL;
    if(cachedArgc > 1) executable = fopen(cachedArgv[1],"rb");
    if(executable)
    {
        fseek(executable, 0, SEEK_END);
        int32_t size = ftell(executable);
        rewind(executable);
        uint8_t* binary = (uint8_t*)malloc(size);
        fread(binary, 1, size, executable);
        fclose(executable);

        int32_t fileCursor = 0;
        int32_t addressCursor = ATMEGA32U4_ENTRY;
        while(true)
        {
            assert(binary[fileCursor++] == ':');
            int32_t byteCount = getValueFromHex(&binary[fileCursor], 2);
            int32_t address = getValueFromHex(&binary[fileCursor+=2], 4);
            int32_t recordType = getValueFromHex(&binary[fileCursor+=4], 2);
            if(recordType == 0x00)
            {
                while(byteCount)
                {
                    int32_t instr = getValueFromHex(&binary[fileCursor+=2], 2);
                    memory[addressCursor++] = getValueFromHex(&binary[fileCursor+=2], 2);
                    memory[addressCursor++] = instr;
                    byteCount-=2;
                }
                while(binary[++fileCursor] != ':')
                ;
            }
            else if(recordType == 0x01)
            {
                break;
            }
            else
            {
                assert(false);
            }
        }
        free(binary);
    }
    else
    {
        printf("Fall back to default internal test program.\n");
    // 0:   0c 94 56 00     jmp     0xac    ; 0xac <__ctors_end>
        memory[0xB00] = 0x94;
        memory[0xB01] = 0x0C;
        memory[0xB02] = 0x00;
        memory[0xB03] = 0x56;
    //ac:   11 24           eor     r1, r1
        memory[0xBAC] = 0x24;
        memory[0xBAD] = 0x11;
    //ae:   1f be           out     0x3f, r1        ; 63
        memory[0xBAE] = 0xBE;
        memory[0xBAF] = 0x1F;
    //b0:   cf ef           ldi     r28, 0xFF       ; 255
    //b2:   da e0           ldi     r29, 0x0A       ; 10
        memory[0xBB0] = 0xEF;
        memory[0xBB1] = 0xCF;
        memory[0xBB2] = 0xE0;
        memory[0xBB3] = 0xDA;
    //b4:   de bf           out     0x3e, r29       ; 62
    //b6:   cd bf           out     0x3d, r28       ; 61
        memory[0xBB4] = 0xBF;
        memory[0xBB5] = 0xDE;
        memory[0xBB6] = 0xBF;
        memory[0xBB7] = 0xCD;
    //b8:   0e 94 62 00     call    0xc4    ; 0xc4 <main>
        memory[0xBB8] = 0x94;
        memory[0xBB9] = 0x0E;
        memory[0xBBA] = 0x00;
        memory[0xBBB] = 0x62;
    //c4:   cf 93           push    r28
    //c6:   df 93           push    r29
        memory[0xBC4] = 0x93;
        memory[0xBC5] = 0xCF;
        memory[0xBC6] = 0x93;
        memory[0xBC7] = 0xDF;
    //c8:   cd b7           in      r28, 0x3d       ; 61
    //ca:   de b7           in      r29, 0x3e       ; 62
        memory[0xBC8] = 0xB7;
        memory[0xBC9] = 0xCD;
        memory[0xBCA] = 0xB7;
        memory[0xBCB] = 0xDE;
    //cc:   84 e2           ldi     r24, 0x24       ; 36
    //ce:   90 e0           ldi     r25, 0x00       ; 0
    //d0:   28 e0           ldi     r18, 0x08       ; 8
        memory[0xBCC] = 0xE2;
        memory[0xBCD] = 0x84;
        memory[0xBCE] = 0xE0;
        memory[0xBCF] = 0x90;
        memory[0xBD0] = 0xE0;
        memory[0xBD1] = 0x28;
    //d2:   fc 01           movw    r30, r24
        memory[0xBD2] = 0x01;
        memory[0xBD3] = 0xFC;
    //d4:   20 83           st      Z, r18
        memory[0xBD4] = 0x83;
        memory[0xBD5] = 0x20;
    //d6:   85 e2           ldi     r24, 0x25       ; 37
        memory[0xBD6] = 0xE2;
        memory[0xBD7] = 0x85;
    //d8:   90 e0           ldi     r25, 0x00       ; 0
        memory[0xBD8] = 0xE0;
        memory[0xBD9] = 0x90;
    //da:   21 e0           ldi     r18, 0x01       ; 1
        memory[0xBDA] = 0xE0;
        memory[0xBDB] = 0x21;
    //dc:   fc 01           movw    r30, r24
        memory[0xBDC] = 0x01;
        memory[0xBDD] = 0xFC;
    //de:   20 83           st      Z, r18
        memory[0xBDE] = 0x83;
        memory[0xBDF] = 0x20;
    //e0:   98 95           break
        memory[0xBE0] = 0x95;
        memory[0xBE1] = 0x98;
    }
}

int8_t generateVStatus(uint8_t firstOp, uint8_t secondOp)
{
    bool firstMSB = (firstOp & 0x80) > 0;
    bool secondMSB = (secondOp & 0x80) > 0;
    if(firstMSB && secondMSB)
    {
        return ((firstOp + secondOp) & 0x80) > 0 ? CLR: SET;
    }
    else if(!firstMSB && !secondMSB)
    {
        return ((firstOp + secondOp) & 0x80) == 0 ? CLR: SET;
    }

    return CLR;
}

int8_t generateHStatus(uint8_t firstOp, uint8_t secondOp)
{
    return (((firstOp & 0xF) + (secondOp & 0xF)) & 0x10) == 0x10 ? SET: CLR;
}

void execProgram()
{
    PC = programStart;
    while((PC < ATMEGA32U4_FLASH_SIZE) && (memory[PC] != 0x95) && (memory[PC+1] != 0x98)) //break
    {
        uint8_t result;
        status newStatus;
        switch(memory[PC])
        {
            case 0x1: //movw
               memory[((memory[PC+1] & 0xF0) >> 4)*2] = memory[(memory[PC+1] & 0xF)*2];
               memory[(((memory[PC+1] & 0xF0) >> 4)*2)+1] = memory[((memory[PC+1] & 0xF)*2)+1];
               // No SREG Updates
               PC+=2;
               break;
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27: //eor
               memory[(memory[PC+1] & 0xF0) >> 4] = memory[(memory[PC+1] & 0xF0) >> 4]^memory[memory[PC+1] & 0xF];
               result = memory[(memory[PC+1] & 0xF0) >> 4];
               newStatus.V = CLR;
               newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
               newStatus.Z = result == 0x00 ? SET: CLR;
               newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
               PC+=2;
               break;
            case 0x30:
            case 0x31:
            case 0x32:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3A:
            case 0x3B:
            case 0x3C:
            case 0x3D:
            case 0x3E:
            case 0x3F: //cpi
                result = memory[16+(memory[PC+1] >> 4)] - (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF));
                newStatus.V = generateVStatus(memory[16+(memory[PC+1] >> 4)], (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF)));
                newStatus.H = generateHStatus(memory[16+(memory[PC+1] >> 4)], (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF)));
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0 ? SET: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                newStatus.C = abs((((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF))) > abs(memory[16+(memory[PC+1] >> 4)]);
                PC+=2;
                break;
            case 0x82:
            case 0x83:
                if((memory[PC+1] & 0xF) == 0x0) //st (std) z
                {
                    writeMemory((memory[31] << 8) | memory[30], memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)]);
                    // No SREG Updates
                    PC+=2;
                }
                break;
            case 0x92:
            case 0x93:
               if((memory[PC+1] & 0xF) == 0xF) //push
               {
                   memory[stackPointer] = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                   stackPointer--;
                   // No SREG Updates
                   PC+=2;
               }
               break;
            case 0x94:
            case 0x95:
                switch(memory[PC+1] & 0x0F)
                {
                    case 0xC:
                    case 0xD: //jmp
                        // No SREG Updates
                        PC+=(memory[PC+2] << 8 | memory[PC+3])*2;
                        break;
                    case 0xE:
                    case 0xF: //call
                        // No SREG Updates
                        PC = programStart + (((memory[PC] & 0x1) << 21) | ((memory[PC+1] & 0xF0) << 17) | ((memory[PC+1] & 0x1) << 16)
                         | (memory[PC+2] << 8) | memory[PC+3])*2;
                        break;
                }
                break;
            case 0xB0:
            case 0xB1:
            case 0xB2:
            case 0xB3:
            case 0xB4:
            case 0xB5:
            case 0xB6:
            case 0xB7: //in
                memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)]
                 = memory[(((memory[PC] & 0x07) >> 1) << 4) | (memory[PC+1] & 0x0F)];
                // No SREG Updates
                PC+=2;
                break;
            case 0xB8:
            case 0xB9:
            case 0xBA:
            case 0xBB:
            case 0xBC:
            case 0xBD:
            case 0xBE:
            case 0xBF: //out
                writeMemory(((((memory[PC] & 0x07) >> 1) << 4) | (memory[PC+1] & 0x0F)) + IO_REG_START, memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)]);
                // No SREG Updates
                PC+=2;
                break;
            case 0xC0:
            case 0xC1:
            case 0xC2:
            case 0xC3:
            case 0xC4:
            case 0xC5:
            case 0xC6:
            case 0xC7:
            case 0xC8:
            case 0xC9:
            case 0xCA:
            case 0xCB:
            case 0xCC:
            case 0xCD:
            case 0xCE:
            case 0xCF: //rjmp
                result = ((memory[PC] & 0xF) << 8) | memory[PC+1];
                PC+=2;
                PC = (0x800 == (result & 0x800)) ? PC - (0x1000 - (2*(result^0x800))) : PC + (2*result);
                // No SREG Updates
                break;
            case 0xE0:
            case 0xE1:
            case 0xE2:
            case 0xE3:
            case 0xE4:
            case 0xE5:
            case 0xE6:
            case 0xE7:
            case 0xE8:
            case 0xE9:
            case 0xEA:
            case 0xEB:
            case 0xEC:
            case 0xED:
            case 0xEE:
            case 0xEF: //ldi
                memory[16 + ((memory[PC+1] & 0xF0) >> 4)] = ((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF);
                // No SREG Updates
                PC+=2;
                break;

            default:
                printf("Instruction not implemented at address 0x%X\n", PC);
                assert(0);
                break;
        }
        pushStatus(newStatus);
    }
}
