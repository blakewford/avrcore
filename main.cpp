#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#ifdef PROFILE
#include <chrono>
using namespace std::chrono;
#endif

//Platform Defines
#define ATMEGA32U4_FLASH_SIZE 32*1024
#define ATMEGA32U4_ENTRY 0xB00
#define ATMEGA32U4_PORTB 0x25

//Globals
uint8_t memory[ATMEGA32U4_FLASH_SIZE];
int32_t programStart = ATMEGA32U4_ENTRY;
int32_t stackPointer = programStart - 1;
uint16_t PC;

//API
void loadProgram();
void execProgram();
void writeMemory(int32_t address, int32_t value);

extern "C" int32_t workFlow()
{
#ifdef PROFILE
    microseconds startProfile = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
#endif

    loadProgram();
    execProgram();
    printf("Program Ended at Address 0x%X\n", PC);

#ifdef PROFILE
    microseconds endProfile = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
    printf("Execution Time in Microseconds %lld\n", (long long)(endProfile.count()-startProfile.count()));
#endif

    return 0;
}

int32_t main()
{
    return workFlow();
}

void writeMemory(int32_t address, int32_t value)
{
    if(address == ATMEGA32U4_PORTB)
    {
      printf("PortB 0x%X\n", value);
    }
    memory[address] = value;
}

void loadProgram()
{
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

void execProgram()
{
    PC = programStart;
    while((PC < ATMEGA32U4_FLASH_SIZE) && (memory[PC] != 0x95) && (memory[PC+1] != 0x98)) //break
    {
        switch(memory[PC])
        {
            case 0x1: //movw
               memory[((memory[PC+1] & 0xF0) >> 4)*2] = memory[(memory[PC+1] & 0xF)*2];
               memory[(((memory[PC+1] & 0xF0) >> 4)*2)+1] = memory[((memory[PC+1] & 0xF)*2)+1];
               PC+=2;
               break;
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27: //eor
               memory[(memory[PC+1] & 0xF0) >> 4] = memory[(memory[PC+1] & 0xF0) >> 4]^memory[memory[PC+1] & 0xF];
               PC+=2;
               break;
            case 0x82:
            case 0x83:
                if((memory[PC+1] & 0xF) == 0x0) //st (std) z
                {
                    writeMemory((memory[31] << 8) | memory[30], memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)]);
                    PC+=2;
                }
                break;
            case 0x92:
            case 0x93:
               if((memory[PC+1] & 0xF) == 0xF) //push
               {
                   memory[stackPointer] = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                   stackPointer--;
                   PC+=2;
               }
               break;
            case 0x94:
            case 0x95:
                switch(memory[PC+1] & 0x0F)
                {
                    case 0xC:
                    case 0xD: //jmp
                        PC+=(memory[PC+2] << 8 | memory[PC+3])*2;
                        break;
                    case 0xE:
                    case 0xF: //call
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
                writeMemory(((((memory[PC] & 0x07) >> 1) << 4) | (memory[PC+1] & 0x0F)) + 0x20, memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)]);
                PC+=2;
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
                PC+=2;
                break;

            default:
                assert(0);
                break;
        }
    }
}