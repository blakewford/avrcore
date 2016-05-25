#include <thread>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#ifdef EMSCRIPTEN
#include "emscripten.h"
#endif

#ifdef PROFILE
#include <chrono>
using namespace std::chrono;
#endif

int32_t cachedArgc = 0;
char argvStorage[1024];
char* cachedArgv[64];

#define FLASH_SIZE 32*1024
#define DMA_START_ADDRESS FLASH_SIZE-0xA

//Platform Defines
#ifdef ATMEGA32U4
#define ATMEGA32U4_ENTRY 0xB00
#define ATMEGA32U4_TIMER_INTERRUPT_ADDRESS 0x5C
#define ATMEGA32U4_UCSR1A 0xC8
#define ATMEGA32U4_PORTE_ADDRESS 0x2E
#define ATMEGA32U4_PORTF_ADDRESS 0x31
#define ATMEGA32U4_PLLCSR_ADDRESS 0x49
#define ENTRY_ADDRESS ATMEGA32U4_ENTRY
#define TIMER_INTERRUPT_ADDRESS ATMEGA32U4_TIMER_INTERRUPT_ADDRESS
#define UCSRA_ADDRESS ATMEGA32U4_UCSR1A
#elif defined(ATMEGA328)
#define ATMEGA328_ENTRY 0x900
#define ATMEGA328_TIMER_INTERRUPT_ADDRESS 0x40
#define ATMEGA328_UCSR0A 0xC0
#define ENTRY_ADDRESS ATMEGA328_ENTRY
#define TIMER_INTERRUPT_ADDRESS ATMEGA328_TIMER_INTERRUPT_ADDRESS
#define UCSRA_ADDRESS ATMEGA328_UCSR0A
#else
#error "Unknown target platform"
#endif

//Common Registers
#define ADCSRA_ADDRESS 0x7A
#define ADCH_ADDRESS 0x79
#define ADCL_ADDRESS 0x78
#define SREG_ADDRESS 0x5F
#define SPH_ADDRESS  0x5E
#define SPL_ADDRESS  0x5D
#define SPMCSR_ADDRESS 0x57
#define SDR_ADDRESS 0x4E
#define SPSR_ADDRESS 0x4D
#define TCNT0_ADDRESS 0x46
#define TIFR0_ADDRESS 0x35
#define PORTB_ADDRESS 0x25
#define PORTC_ADDRESS 0x28
#define PORTD_ADDRESS 0x2B
#define IO_REG_START 0x20

//Status Bits
#define UDRE_BIT 1<<5
#define TOV0_BIT 1<<0
#define SIGRD_BIT 1<<5
#define SPMEN_BIT 1<<0
#define SPIF_BIT 1<<7
#define ADSC_BIT 1<<6

//Platform Specific Status Bits
#define ATMEGA32U4_PLLE_BIT 1<<1
#define ATMEGA32U4_PLOCK_BIT 1<<0

//Globals
#define INSTRUCTION_LIMIT 1024
#define MANUFACTURER_ID 0xBF
#define CLR 0
#define SET 1
#define IGNORE 2
struct status
{
    status()
    {
        clear();
    }
    void clear()
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
    int8_t I:3;
    int8_t T:3;
    int8_t H:3;
    int8_t S:3;
    int8_t V:3;
    int8_t N:3;
    int8_t Z:3;
    int8_t C:3;
};
bool branchEqual = false;
bool branchGreater = false;
uint8_t memory[FLASH_SIZE];
int32_t programStart = ENTRY_ADDRESS;
uint16_t PC;
status SREG;

//API
extern "C" void loadPartialProgram(uint8_t* binary);
extern "C" void engineInit();
extern "C" int32_t fetchN(int32_t n);

void loadProgram(uint8_t* binary);
void loadDefaultProgram();
void execProgram();
int32_t fetch();

uint8_t readMemory(int32_t address);
void writeMemory(int32_t address, int32_t value);
void pushStatus(status& newStatus);
void decrementStackPointer();
void resetFetchState()
{
    memory[ADCSRA_ADDRESS] &= ~ADSC_BIT;
    memory[SPSR_ADDRESS] |= SPIF_BIT;
    memory[UCSRA_ADDRESS] |= UDRE_BIT;
}

void platformPrint(const char* message)
{
#ifdef EMSCRIPTEN
    char buffer[1024];
    sprintf(buffer, "console.log('%s')", message);
    emscripten_run_script(buffer);
#else
    printf("%s\n", message);
#endif
}

#ifndef LIBRARY
size_t totalFetches = 0;
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
    FILE* executable = NULL;
#ifdef EMSCRIPTEN
    EM_ASM(var fs = require('fs'); fs.readFile(process.argv[process.argv.length-1], 'utf8', function(error, hex){fs.writeFileSync('scratch', hex)}););
    EM_ASM(FS.mkdir('/working'); FS.mount(NODEFS, { root: '.' }, '/working'););
    executable = fopen("/working/scratch","rb");
#else
    if(cachedArgc > 1) executable = fopen(cachedArgv[1],"rb");
#endif
    if(executable)
    {
        fseek(executable, 0, SEEK_END);
        int32_t size = ftell(executable);
        rewind(executable);
        uint8_t* binary = (uint8_t*)malloc(size);
        size_t read = fread(binary, 1, size, executable);
        if(read != size) return -1;
        fclose(executable);
        loadProgram(binary);
#ifdef EMSCRIPTEN
        EM_ASM(FS.unlink('/working/scratch'););
#endif
    }
    else
    {
        loadDefaultProgram();
    }

#ifdef PROFILE
    microseconds startProfile = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
#endif

    engineInit();
    execProgram();

#ifdef PROFILE
    microseconds endProfile = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch());
    char buffer[256];
    memset(buffer, '\0', 256);
    long long profileTime = (long long)(endProfile.count()-startProfile.count());
    sprintf(buffer, "%s 0x%X %i %lld %lld", argv[2], PC, (memory[25] << 8 | memory[24]), profileTime, (profileTime*1000)/totalFetches);
    platformPrint(buffer);
#endif

    return 0;
}

#endif

uint8_t readMemory(int32_t address)
{
    if(address == ADCH_ADDRESS)
    {
        return 0;
    }
    if(address == ADCL_ADDRESS)
    {
        return 9;
    }
    if(address == TIFR0_ADDRESS)
    {
        return TOV0_BIT;
    }
    return memory[address];
}

void writeMemory(int32_t address, int32_t value)
{
    char buffer[256];
    memory[address] = value;
    switch(address)
    {
        case PORTB_ADDRESS:
        case PORTC_ADDRESS:
        case PORTD_ADDRESS:
#ifdef ATMEGA32U4
        case ATMEGA32U4_PORTE_ADDRESS:
        case ATMEGA32U4_PORTF_ADDRESS:
#endif
#ifdef LIBRARY
            sprintf(buffer, "writePort(%i, %i)", (address-PORTB_ADDRESS)/3, value);
            emscripten_run_script(buffer);
#else
            sprintf(buffer, "Port %i 0x%X", (address-PORTB_ADDRESS)/3, value);
            platformPrint(buffer);
#endif
            break;
        case SPMCSR_ADDRESS:
            if(value == (SIGRD_BIT|SPMEN_BIT))
            {
                 memory[((memory[31] << 8) | memory[30]) + programStart + 1] = MANUFACTURER_ID;
            }
            break;
        case SDR_ADDRESS:
#ifdef LIBRARY
            sprintf(buffer, "writeSPI(%i)", value);
            emscripten_run_script(buffer);
#else
            sprintf(buffer, "SPI Transmit %i", value);
            platformPrint(buffer);
#endif
            break;
#ifdef ATMEGA32U4
        case ATMEGA32U4_PLLCSR_ADDRESS:
            memory[ATMEGA32U4_PLLCSR_ADDRESS] = value;
            memory[ATMEGA32U4_PLLCSR_ADDRESS] = (value & ATMEGA32U4_PLLE_BIT) > 0 ? memory[ATMEGA32U4_PLLCSR_ADDRESS] | ATMEGA32U4_PLOCK_BIT : memory[ATMEGA32U4_PLLCSR_ADDRESS] & ~ATMEGA32U4_PLOCK_BIT;
            break;
#endif
    }
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
    SREG.clear();

    PC = programStart;
    int32_t SP = programStart - 1;
    memory[SPH_ADDRESS] = (SP & 0xFF00) >> 8;
    memory[SPL_ADDRESS] = (SP & 0xFF);
    resetFetchState();
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

void loadDefaultProgram()
{
    platformPrint("Fall back to default internal test program.");
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

int32_t currentAddressCursor = ENTRY_ADDRESS;
void loadPartialProgram(uint8_t* binary)
{
    int32_t lineCursor = 0;
    assert(binary[lineCursor++] == ':');
    int32_t byteCount = getValueFromHex(&binary[lineCursor], 2);
    int32_t address = getValueFromHex(&binary[lineCursor+=2], 4);
    int32_t recordType = getValueFromHex(&binary[lineCursor+=4], 2);
    if(recordType == 0x00)
    {
        while(byteCount)
        {
            int32_t instr = getValueFromHex(&binary[lineCursor+=2], 2);
            memory[currentAddressCursor++] = getValueFromHex(&binary[lineCursor+=2], 2);
            memory[currentAddressCursor++] = instr;
            byteCount-=2;
        }
    }
    else if(recordType == 0x01)
    {
        return;
    }
    else
    {
        assert(false);
    }
}

void loadProgram(uint8_t* binary)
{
    int32_t fileCursor = 0;
    int32_t addressCursor = ENTRY_ADDRESS;
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
    while(fetchN(1))
        ;
}

void callTOV0Interrupt()
{
  writeMemory((memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS], PC >> 8);
  decrementStackPointer();
  writeMemory((memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS], PC & 255);
  decrementStackPointer();
  PC = TIMER_INTERRUPT_ADDRESS + programStart;
}

int32_t trackedFetches = 0;
int32_t fetchN(int32_t n)
{
    bool success = true;
    bool timed = false;
    for(int i = 0; i < n/INSTRUCTION_LIMIT; i++)
    {
        timed = true;
        callTOV0Interrupt();
    }
    if(!timed)
    {
        trackedFetches += n;
    }
    if(trackedFetches/INSTRUCTION_LIMIT)
    {
        trackedFetches = 0;
        callTOV0Interrupt();
    }
    while(success && n)
    {
        success = fetch();
        n--;
    }
#ifdef LIBRARY
    EM_ASM("refreshUI();");
#endif

    return success;
}

bool longOpcode(uint16_t programCounter)
{
    uint16_t opcode0 = memory[programCounter];
    uint16_t opcode1 = memory[programCounter+1];

    switch(opcode0)
    {
        case 0x90:
        case 0x91:
            if((opcode1 & 0xF) == 0) //lds
            {
                return true;
            }
            break;
        case 0x92:
        case 0x93:
            if((opcode1 & 0xF) == 0) //sts
            {
                return true;
            }
            break;
        case 0x94:
        case 0x95:
            if((opcode1 & 0xF) > 0xB) //jmp / call
            {
                return true;
            }
            break;
    }

    return false;
}

void incrementStackPointer()
{
    int32_t SP = (memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS];
    SP++;
    memory[SPH_ADDRESS] = (SP & 0xFF00) >> 8;
    memory[SPL_ADDRESS] = (SP & 0xFF);
}

void decrementStackPointer()
{
    int32_t SP = (memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS];
    SP--;
    memory[SPH_ADDRESS] = (SP & 0xFF00) >> 8;
    memory[SPL_ADDRESS] = (SP & 0xFF);
}

void handleUnimplemented()
{
    char buffer[1024];
    sprintf(buffer, "Instruction not implemented at address 0x%X", PC);
    platformPrint(buffer);
    assert(0);
}

uint16_t result;
status newStatus;
int32_t fetch()
{
        if((PC >= FLASH_SIZE) || ((memory[PC] == 0x95) && (memory[PC+1] == 0x98))) //break
            return false;

        totalFetches++;

        result = 0;
        newStatus.clear();

#ifndef EMSCRIPTEN
        system_clock::time_point syncPoint = system_clock::now() + nanoseconds(60);
#endif
        switch(memory[PC])
        {
            case 0x0:
                if(memory[PC+1] == 0x00) //nop
                {
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x1: //movw
                memory[((memory[PC+1] & 0xF0) >> 4)*2] = memory[(memory[PC+1] & 0xF)*2];
                memory[(((memory[PC+1] & 0xF0) >> 4)*2)+1] = memory[((memory[PC+1] & 0xF)*2)+1];
                // No SREG Updates
                PC+=2;
                break;
            case 0x2: //muls
            case 0x3: //mulsu
                result = (int)memory[((memory[PC+1] & 0xF0) >> 4)*2]*(int)memory[(memory[PC+1] & 0xF)*2];
                memory[0] = result & 0xFF;
                memory[1] = result >> 8;
                // No SREG Updates
                PC+=2;
                break;
            case 0x4:
            case 0x5:
            case 0x6:
            case 0x7: //cpc
                result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] - memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                result -= SREG.C == SET ? 1: 0;
                newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                newStatus.V = generateVStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)] + (SREG.C == SET ? 1: 0));
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? newStatus.Z: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                newStatus.C = abs(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)] + (SREG.C == SET ? 1: 0)) > abs(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) ? SET: CLR;
                branchGreater = (int8_t)(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) > (int8_t)(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                branchEqual = (int8_t)(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) == (int8_t)(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] - memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                if((result == 0x80 && SREG.C == SET) || (result == 0xFF80 && memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] == 0) || (result == 0xFFFF && SREG.C == CLR))
                {
                    newStatus.V = SET;
                    newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                }
                PC+=2;
                break;
            case 0x8:
            case 0x9:
            case 0xA:
            case 0xB: //sbc
                result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] - memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                result -= SREG.C == SET ? 1: 0;
                newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                newStatus.V = generateVStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? newStatus.Z: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                newStatus.C = abs(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)] + (SREG.C == SET ? 1: 0)) > abs(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) ? SET: CLR;
                memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result & 0xFF;
                PC+=2;
                break;
            case 0xC:
            case 0xD:
            case 0xE:
            case 0xF: //add
                result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] + memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                newStatus.V = generateVStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? SET: CLR;
                newStatus.C = result > 0xFF ? SET: CLR;
                memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result & 0xFF;
                PC+=2;
                break;
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13: //cpse
                if(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] == memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)])
                {
                    PC+=2;
                    if(longOpcode(PC))
                    {
                        PC+=2;
                    }
                }
                // No SREG Updates
                PC+=2;
                break;
            case 0x14:
            case 0x15:
            case 0x16:
            case 0x17: //cp
               result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] - memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.V = generateVStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
               newStatus.Z = result == 0x00 ? SET: CLR;
               newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
               newStatus.C = abs(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]) > abs(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) ? SET: CLR;
               branchGreater = (int8_t)(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) > (int8_t)(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               branchEqual = (int8_t)(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) == (int8_t)(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               PC+=2;
               break;
            case 0x18:
            case 0x19:
            case 0x1A:
            case 0x1B: //sub
               result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] - memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.V = generateVStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
               newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
               newStatus.Z = result == 0x00 ? SET: CLR;
               newStatus.C = abs(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]) > abs(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) ? SET: CLR;
               memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result & 0xFF;
               branchGreater = (int8_t)(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) > (int8_t)(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               branchEqual = (int8_t)(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)]) == (int8_t)(memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               PC+=2;
               break;
            case 0x1C:
            case 0x1D:
            case 0x1E:
            case 0x1F: //adc
               result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] + memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               result += SREG.C == SET ? 1: 0;
               newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.V = generateVStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
               newStatus.Z = result == 0x00 ? SET: CLR;
               newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
               newStatus.C = result > 0xFF ? SET: CLR;
               memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result & 0xFF;
               PC+=2;
               break;
            case 0x20:
            case 0x21:
            case 0x22:
            case 0x23: //and
               result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] & memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
               newStatus.V = CLR;
               newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
               newStatus.Z = result == 0x00 ? SET: CLR;
               newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
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
            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2B: //or
               result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] | memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
               newStatus.V = CLR;
               newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
               newStatus.Z = result == 0x00 ? SET: CLR;
               newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
               PC+=2;
               break;
            case 0x2C:
            case 0x2D:
            case 0x2E:
            case 0x2F: //mov
               memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)];
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
                newStatus.Z = result == 0x00 ? SET: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                newStatus.C = abs((((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF))) > abs(memory[16+(memory[PC+1] >> 4)]) ? SET: CLR;
                branchGreater = memory[16+(memory[PC+1] >> 4)] > (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF));
                branchEqual = memory[16+(memory[PC+1] >> 4)] == (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF));
                PC+=2;
                break;
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x43:
            case 0x44:
            case 0x45:
            case 0x46:
            case 0x47:
            case 0x48:
            case 0x49:
            case 0x4A:
            case 0x4B:
            case 0x4C:
            case 0x4D:
            case 0x4E:
            case 0x4F: //sbci
                result = ((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF);
                newStatus.H = generateHStatus(memory[16+((memory[PC+1] & 0xF0) >> 4)], result);
                newStatus.V = generateVStatus(memory[16+((memory[PC+1] & 0xF0) >> 4)], result);
                result += SREG.C;
                newStatus.C = abs(result) > abs(memory[16+((memory[PC+1] & 0xF0) >> 4)]) ? SET: CLR;
                memory[16+((memory[PC+1] & 0xF0) >> 4)] -= result;
                result = memory[16+((memory[PC+1] & 0xF0) >> 4)];
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? newStatus.Z: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                PC+=2;
                break;
            case 0x50:
            case 0x51:
            case 0x52:
            case 0x53:
            case 0x54:
            case 0x55:
            case 0x56:
            case 0x57:
            case 0x58:
            case 0x59:
            case 0x5A:
            case 0x5B:
            case 0x5C:
            case 0x5D:
            case 0x5E:
            case 0x5F: //subi
                result = ((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF);
                newStatus.H = generateHStatus(memory[16+((memory[PC+1] & 0xF0) >> 4)], result);
                newStatus.V = generateVStatus(memory[16+((memory[PC+1] & 0xF0) >> 4)], result);
                newStatus.C = abs(result) > abs(memory[16+((memory[PC+1] & 0xF0) >> 4)]) ? SET: CLR;
                memory[16+((memory[PC+1] & 0xF0) >> 4)] -= result;
                result = memory[16+((memory[PC+1] & 0xF0) >> 4)];
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? SET: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                branchGreater = memory[16+(memory[PC+1] >> 4)] > (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF));
                branchEqual = memory[16+(memory[PC+1] >> 4)] == (((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF));
                PC+=2;
                break;
            case 0x60:
            case 0x61:
            case 0x62:
            case 0x63:
            case 0x64:
            case 0x65:
            case 0x66:
            case 0x67:
            case 0x68:
            case 0x69:
            case 0x6A:
            case 0x6B:
            case 0x6C:
            case 0x6D:
            case 0x6E:
            case 0x6F: //ori
                result = ((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF);
                memory[16+((memory[PC+1] & 0xF0) >> 4)] |= result;
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? SET: CLR;
                newStatus.V = CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                PC+=2;
                break;
            case 0x70:
            case 0x71:
            case 0x72:
            case 0x73:
            case 0x74:
            case 0x75:
            case 0x76:
            case 0x77:
            case 0x78:
            case 0x79:
            case 0x7A:
            case 0x7B:
            case 0x7C:
            case 0x7D:
            case 0x7E:
            case 0x7F: //andi
                result = ((memory[PC] & 0xF) << 4) | (memory[PC+1] & 0xF);
                memory[16+((memory[PC+1] & 0xF0) >> 4)] &= result;
                newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                newStatus.Z = result == 0x00 ? SET: CLR;
                newStatus.V = CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                PC+=2;
                break;
            case 0x80:
            case 0x81:
                if((memory[PC+1] & 0xF) >= 0x8) //ld (ldd) y
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) < 0x8) //ld (ldd) z
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x82:
            case 0x83:
                if((memory[PC+1] & 0xF) >= 0x8) //st (std) y
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) < 0x8) //st (std) z
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x84:
            case 0x85:
            case 0x8C:
            case 0x8D:
                if((memory[PC+1] & 0xF) >= 0x8) //ld (ldd) y
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) < 0x8) //ld (ldd) z
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x86:
            case 0x87:
                if((memory[PC+1] & 0xF) >= 0x8) //st (std) y
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) < 0x8) //st (std) z
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x88:
            case 0x89:
                if((memory[PC+1] & 0xF) >= 0x8) //ld (ldd) y
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) < 0x8) //ld (ldd) z
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x8A:
            case 0x8B:
            case 0x8E:
            case 0x8F:
                if((memory[PC+1] & 0xF) >= 0x8) //st (std) y
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) < 0x8) //st (std) z
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x90:
            case 0x91:
                if((memory[PC+1] & 0xF) == 0x0) //lds
                {
                   memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)] = readMemory(((memory[PC+2] << 8) | memory[PC+3]));
                   // No SREG Updates
                   PC+=4;
                   break;
                }
                if((memory[PC+1] & 0xF) == 0x1) //ld z+
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    if(memory[30] < 0xFF)
                    {
                        memory[30] = memory[30]+1;
                    }
                    else
                    {
                        memory[31] = memory[31]+1;
                        memory[30] = 0x00;
                    }
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) == 0x4) //lpm (rd, z)
                {
                    result = ((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4);
                    memory[result] = readMemory(2*(((memory[31] << 8) | memory[30]) >> 1) + programStart + ((((memory[31] << 8) | memory[30]) & 0x1) == 0 ? 1: 0));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) == 0x5) //lpm (rd, z+)
                {
                    result = ((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4);
                    memory[result] = readMemory(2*(((memory[31] << 8) | memory[30]) >> 1) + programStart + ((((memory[31] << 8) | memory[30]) & 0x1) == 0 ? 1: 0));
                    // No SREG Updates
                    if(memory[30] < 0xFF)
                    {
                        memory[30] = memory[30]+1;
                    }
                    else
                    {
                        memory[31] = memory[31]+1;
                        memory[30] = 0x00;
                    }
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) == 0xC) //ld x
                {
                    result = ((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4);
                    memory[result] = readMemory((memory[27] << 8) | memory[26]);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) == 0xD) //ld x+
                {
                    result = ((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4);
                    memory[result] = readMemory((memory[27] << 8) | memory[26]);
                    // No SREG Updates
                    if(memory[26] < 0xFF)
                    {
                        memory[26] = memory[26]+1;
                    }
                    else
                    {
                        memory[27] = memory[27]+1;
                        memory[26] = 0x00;
                    }
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) == 0xF) //pop
                {
                    result = ((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4);
                    incrementStackPointer();
                    memory[result] = memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]];
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0x92:
            case 0x93:
               if((memory[PC+1] & 0xF) == 0x0) //sts
               {
                  writeMemory(((memory[PC+2] << 8) | memory[PC+3]), memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)]);
                  // No SREG Updates
                  PC+=4;
                  break;
               }
               if((memory[PC+1] & 0xF) == 0x1) //st (std) z+
               {
                   result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                   writeMemory((memory[31] << 8) | memory[30], result);
                   // No SREG Updates
                   if(memory[30] < 0xFF)
                   {
                       memory[30] = memory[30]+1;
                   }
                   else
                   {
                       memory[31] = memory[31]+1;
                       memory[30] = 0x00;
                   }
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0x2) //st (std) -z
               {
                   result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                   if(memory[30] == 0x00)
                   {
                       memory[30] = 0xFF;
                       memory[31] = memory[31]-1;
                   }
                   else
                   {
                       memory[30] = memory[30]-1;
                   }
                   writeMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                   // No SREG Updates
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0x9) //st (std) y+
               {
                   result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                   writeMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                   // No SREG Updates
                   if(memory[28] < 0xFF)
                   {
                       memory[28] = memory[28]+1;
                   }
                   else
                   {
                       memory[29] = memory[29]+1;
                       memory[28] = 0x00;
                   }
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0xA) //st (std) y-
               {
                   if(memory[28] == 0x00)
                   {
                       memory[29] = memory[29]-1;
                       memory[28] = 0xFF;
                   }
                   else
                   {
                       memory[28] = memory[28]-1;
                   }
                   result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                   writeMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                   // No SREG Updates
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0xF) //push
               {
                   memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                   decrementStackPointer();
                   // No SREG Updates
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0xC) //st x
               {
                   result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                   writeMemory((memory[27] << 8) | memory[26], result);
                   // No SREG Updates
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0xD) //st x+
               {
                   result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                   writeMemory((memory[27] << 8) | memory[26], result);
                   // No SREG Updates
                   if(memory[26] < 0xFF)
                   {
                       memory[26] = memory[26]+1;
                   }
                   else
                   {
                       memory[27] = memory[27]+1;
                       memory[26] = 0x00;
                   }
                   PC+=2;
                   break;
               }
               if((memory[PC+1] & 0xF) == 0xE) //st -x
               {
                   result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                   if(memory[26] == 0x00)
                   {
                       memory[26] = 0xFF;
                       memory[27] = memory[27]-1;
                   }
                   else
                   {
                       memory[26] = memory[26]-1;
                   }
                   writeMemory((memory[27] << 8) | memory[26], result);
                   // No SREG Updates
                   PC+=2;
                   break;
               }
               handleUnimplemented();
            case 0x94:
            case 0x95:
                if((memory[PC] == 0x94) && (memory[PC+1] == 0x08)) //sec
                {
                    newStatus.C = SET;
                    PC+=2;
                    break;
                }
                if((memory[PC] == 0x94) && (memory[PC+1] == 0x09)) //ijmp
                {
                    result = (2*((memory[31] << 8) | memory[30])) + programStart;
                    // No SREG Updates
                    PC = result;
                    break;
                }
                if((memory[PC] == 0x94) && (memory[PC+1] == 0x68)) //set
                {
                    newStatus.T = SET;
                    PC+=2;
                    break;
                }
                if((memory[PC] == 0x94) && (memory[PC+1] == 0x78)) //sei
                {
                    newStatus.I = SET;
                    PC+=2;
                    break;
                }
                if((memory[PC] == 0x94) && (memory[PC+1] == 0xE8)) //clt
                {
                    newStatus.T = CLR;
                    PC+=2;
                    break;
                }
                if((memory[PC] == 0x94) && (memory[PC+1] == 0xF8)) //cli
                {
                    newStatus.I = CLR;
                    PC+=2;
                    break;
                }
                if((memory[PC+1] == 0x88) || (memory[PC+1] == 0xA8)) //sleep || wdr
                {
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC] == 0x95) && (memory[PC+1] == 0x8)) //ret
                {
                    incrementStackPointer();
                    result = (memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS];
                    // No SREG Updates
                    incrementStackPointer();
                    PC = ((memory[result] << 8) | (memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]]));
                    break;
                }
                if((memory[PC] == 0x95) && (memory[PC+1] == 0x9)) //icall
                {
                    result = (((memory[31] << 8) | memory[30])*2)+programStart;
                    PC += 2;
                    memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = (PC & 0xFF);
                    decrementStackPointer();
                    memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = (PC & 0xFF00) >> 8;
                    decrementStackPointer();
                    // No SREG Updates
                    PC = result;
                    break;
                }
                if((memory[PC] == 0x95) && (memory[PC+1] == 0x18)) //reti
                {
                    incrementStackPointer();
                    result = (memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS];
                    incrementStackPointer();
                    newStatus.I = SET;
                    PC = (memory[result] | ((memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]]) << 8));
                    break;
                }
                switch(memory[PC+1] & 0x0F)
                {
                    case 0x0: //com
                        result = ~memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
                        newStatus.V = CLR;
                        newStatus.C = SET;
                        newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                        newStatus.Z = result == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        PC+=2;
                        break;
                    case 0x1: //neg
                        if(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] != 0x80)
                        {
                            result = (~memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] + 1) & 0xFF;
                            memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
                        }
                        newStatus.H = generateHStatus(memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)], result);
                        newStatus.V = result == 0x80 ? SET: CLR;
                        newStatus.C = result == 0x00 ? CLR: SET;
                        newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                        newStatus.Z = result == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        PC+=2;
                        break;
                    case 0x2: //swap
                        result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] << 4;
                        result |= (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] >> 4);
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
                        PC+=2;
                        break;
                    case 0x3: //inc
                        result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                        newStatus.V = result == 0x7F ? SET: CLR;
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = ++result;
                        newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                        newStatus.Z = (result & 0xFF) == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        PC+=2;
                        break;
                    case 0x5: //asr
                        result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                        newStatus.C = (result & 0x1) > 0 ? SET: CLR;
                        result = ((result >> 1) | (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] & 0x80));
                        newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                        newStatus.V = ((newStatus.N ^ newStatus.C) > 0) ? SET: CLR;
                        newStatus.Z = result == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
                        PC+=2;
                        break;
                    case 0x6: //lsr
                        result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                        newStatus.C = (result & 0x1) > 0 ? SET: CLR;
                        result = (result >> 1);
                        newStatus.N = CLR;
                        newStatus.V = ((newStatus.N ^ newStatus.C) > 0) ? SET: CLR;
                        newStatus.Z = result == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
                        PC+=2;
                        break;
                    case 0x7: //ror
                        result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                        newStatus.C = (result & 0x1) > 0 ? SET: CLR;
                        result = ((result >> 1) | (newStatus.C << 7));
                        newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                        newStatus.V = ((newStatus.N ^ newStatus.C) > 0) ? SET: CLR;
                        newStatus.Z = result == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = result;
                        PC+=2;
                        break;
                    case 0xA: //dec
                        result = memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)];
                        newStatus.V = result == 0x80 ? SET: CLR;
                        memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] = --result;
                        newStatus.N = ((result & 0x80) > 0) ? SET: CLR;
                        newStatus.Z = result == 0x00 ? SET: CLR;
                        newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                        PC+=2;
                        break;
                    case 0xC:
                    case 0xD: //jmp
                        // No SREG Updates
                        result  = (memory[PC] & 0x1) << 21;
                        result += (memory[PC+1] >> 4) << 17;
                        result += (memory[PC+1] & 0x1) << 16;
                        result += (memory[PC+2] << 8 | memory[PC+3]);
                        PC = programStart + (result*2);
                        break;
                    case 0xE:
                    case 0xF: //call
                        result = programStart + (((memory[PC] & 0x1) << 21) | ((memory[PC+1] & 0xF0) << 17) | ((memory[PC+1] & 0x1) << 16)
                         | (memory[PC+2] << 8) | memory[PC+3])*2;
                        PC += 4;
                        memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = (PC & 0xFF);
                        decrementStackPointer();
                        memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = (PC & 0xFF00) >> 8;
                        decrementStackPointer();
                        // No SREG Updates
                        PC = result;
                        break;
                    default:
                        handleUnimplemented();
                        break;
                }
                break;
            case 0x96: //adiw
            case 0x97: //sbiw
                result = ((memory[PC+1] & 0x30) >> 4);
                switch(result)
                {
                    case 0:
                        result = 24;
                        break;
                    case 1:
                        result = 26;
                        break;
                    case 2:
                        result = 28;
                        break;
                    case 3:
                        result = 30;
                        break;
                    default:
                        handleUnimplemented();
                }
                result = (memory[result+1] << 8) | memory[result];
                newStatus.V = generateVStatus(result, (((memory[PC+1] & 0xC0) >> 0x2) | (memory[PC+1] & 0xF)));
                newStatus.C = abs((((memory[PC+1] & 0xC0) >> 0x2) | (memory[PC+1] & 0xF))) > abs(result) ? SET: CLR;
                if(memory[PC] == 0x96)
                {
                    result = result + (((memory[PC+1] & 0xC0) >> 0x2) | (memory[PC+1] & 0xF));
                }
                if(memory[PC] == 0x97)
                {
                    result = result - (((memory[PC+1] & 0xC0) >> 0x2) | (memory[PC+1] & 0xF));
                }
                newStatus.N = ((result & 0x8000) > 0) ? SET: CLR;
                newStatus.Z = result == 0x0000 ? SET: CLR;
                newStatus.S = ((newStatus.N ^ newStatus.V) > 0) ? SET: CLR;
                switch((memory[PC+1] & 0x30) >> 4)
                {
                    case 0:
                        memory[24] = result & 0xFF;
                        memory[25] = (result & 0xFF00) >> 8;
                        break;
                    case 1:
                        memory[26] = result & 0xFF;
                        memory[27] = (result & 0xFF00) >> 8;
                        break;
                    case 2:
                        memory[28] = result & 0xFF;
                        memory[29] = (result & 0xFF00) >> 8;
                        break;
                    case 3:
                        memory[30] = result & 0xFF;
                        memory[31] = (result & 0xFF00) >> 8;
                        break;
                    default:
                        handleUnimplemented();
                }
                PC+=2;
                break;
            case 0x98: //cbi
                result = (1 << (memory[PC+1] & 0x7));
                memory[(memory[PC+1] >> 0x3) + IO_REG_START] &= ~result;
                // No SREG Updates
                PC+=2;
                break;
            case 0x9A: //sbi
                result = (1 << (memory[PC+1] & 0x7));
                memory[(memory[PC+1] >> 0x3) + IO_REG_START] |= result;
                // No SREG Updates
                PC+=2;
                break;
            case 0x9B: //sbis
                result = memory[(memory[PC+1] >> 0x3) + IO_REG_START];
                if((result & (1 << (memory[PC+1] & 0x7))) > 0)
                {
                    PC+=2;
                    if(longOpcode(PC))
                    {
                        PC+=2;
                    }
                }
                // No SREG Updates
                PC+=2;
                break;
            case 0x9C:
            case 0x9D:
            case 0x9E:
            case 0x9F: //mul
               result = (memory[((memory[PC] & 0x1) << 4) | (memory[PC+1] >> 4)] * memory[(((memory[PC] & 0x2) >> 1) << 4) | (memory[PC+1] & 0xF)]);
               newStatus.Z = result == 0x0000 ? SET: CLR;
               newStatus.C = ((result & 0x8000) > 0) ? SET: CLR;
               memory[1] = result >> 8;
               memory[0] = result & 0xFF;
               PC+=2;
               break;
            case 0xA0:
            case 0xA1:
            case 0xA4:
            case 0xA5:
            case 0xA8:
            case 0xA9:
            case 0xAC:
            case 0xAD:
                if((memory[PC+1] & 0xF) < 0x8) //ld (ldd) z
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) >= 0x8) //ld (ldd) y
                {
                    result = ((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4);
                    memory[result] = readMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)));
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0xA2:
            case 0xA3:
            case 0xA6:
            case 0xA7:
            case 0xAA:
            case 0xAB:
            case 0xAE:
            case 0xAF:
                if((memory[PC+1] & 0xF) < 0x8) //st (std) z
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[31] << 8) | memory[30]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1)), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((memory[PC+1] & 0xF) >= 0x8) //st (std) y
                {
                    result = memory[((memory[PC] & 0x1) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    writeMemory(((memory[29] << 8) | memory[28]) + (((memory[PC] & 0xC) << 1) | (memory[PC+1] & 0x7) | (((memory[PC] >> 1) & 0x10) << 1 )), result);
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0xB0:
            case 0xB1:
            case 0xB2:
            case 0xB3:
            case 0xB4:
            case 0xB5:
            case 0xB6:
            case 0xB7: //in
                memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)] = readMemory(((((memory[PC] & 0x07) >> 1) << 4) | (memory[PC+1] & 0x0F)) + IO_REG_START);
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
                if((memory[PC] == 0xCF) && (memory[PC+1] == 0xFF))
                {
                    //Program Exit
                    return false;
                }
                result = ((memory[PC] & 0xF) << 8) | memory[PC+1];
                PC+=2;
                PC = (0x800 == (result & 0x800)) ? PC - (0x1000 - (2*(result^0x800))) : PC + (2*result);
                // No SREG Updates
                break;
            case 0xD0:
            case 0xD1:
            case 0xD2:
            case 0xD3:
            case 0xD4:
            case 0xD5:
            case 0xD6:
            case 0xD7:
            case 0xD8:
            case 0xD9:
            case 0xDA:
            case 0xDB:
            case 0xDC:
            case 0xDD:
            case 0xDE:
            case 0xDF: //rcall
                result = ((memory[PC] & 0xF) << 8) | memory[PC+1];
                PC+=2;
                memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = (PC & 0xFF);
                decrementStackPointer();
                memory[(memory[SPH_ADDRESS] << 8) | memory[SPL_ADDRESS]] = (PC & 0xFF00) >> 8;
                decrementStackPointer();
                // No SREG Updates
                if(0x800 == (result & 0x800))
                {
                    PC -= (0x1000 - 2*(result^0x800));
                }
                else
                {
                    PC += (2*result);
                }
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
            case 0xF0:
            case 0xF1:
            case 0xF2:
            case 0xF3:
                if((((memory[PC] & 0x0C) >> 2) == 0x0) && ((memory[PC+1] & 0x7) == 0x0)) //brcs
                {
                    if(SREG.C == SET)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x0) && ((memory[PC+1] & 0x7) == 0x1)) //breq
                {
                    if(SREG.Z == SET)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x0) && ((memory[PC+1] & 0x7) == 0x2)) //brmi
                {
                    if(SREG.N == SET)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x0) && ((memory[PC+1] & 0x7) == 0x4)) //brlt
                {
                    if(SREG.S == SET && !branchGreater)
                    {
                        branchEqual = false;
                        branchGreater = false;
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x0) && ((memory[PC+1] & 0x7) == 0x6)) //brts
                {
                    if(SREG.T == SET)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0xF4:
            case 0xF5:
            case 0xF6:
            case 0xF7:
                if((((memory[PC] & 0x0C) >> 2) == 0x1) && ((memory[PC+1] & 0x7) == 0x2)) //brpl
                {
                    if(SREG.N == CLR)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x1) && ((memory[PC+1] & 0x7) == 0x0)) //brcc
                {
                    if(SREG.C == CLR)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x1) && ((memory[PC+1] & 0x7) == 0x1)) //brne
                {
                    if(SREG.Z == CLR)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x1) && ((memory[PC+1] & 0x7) == 0x4)) //brge
                {
                    if(SREG.S == CLR)
//                    if(SREG.S == CLR && (branchGreater || branchEqual))
                    {
                        branchEqual = false;
                        branchGreater = false;
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                if((((memory[PC] & 0x0C) >> 2) == 0x1) && ((memory[PC+1] & 0x7) == 0x6)) //brtc
                {
                    if(SREG.T == CLR)
                    {
                        result = ((memory[PC] & 0x3) << 5) | (memory[PC+1] >> 3);
                        PC = (0x40 < result) ? (PC - (2*(0x80 - result))) : (PC + (2*result));
                    }
                    // No SREG Updates
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0xF8:
            case 0xF9: //bld
                if((memory[PC+1] & 0xF) < 0x8)
                {
                    memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)] &= SREG.T ? (1 << (memory[PC+1] & 0x7)): ~(1 << (memory[PC+1] & 0x7));
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0xFA:
            case 0xFB: //bst
                result = memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                newStatus.T = (result & (1 << (memory[PC+1] & 0x7))) > 0 ? SET: CLR;
                PC+=2;
                break;
            case 0xFC:
            case 0xFD: //sbrc
                if((memory[PC+1] & 0xF) < 0x8)
                {
                    result = memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    if((result & (1 << (memory[PC+1] & 0xF))) == 0)
                    {
                        PC+=2;
                        if(longOpcode(PC))
                        {
                            PC+=2;
                        }
                    }
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            case 0xFE:
            case 0xFF: //sbrs
                if((memory[PC+1] & 0xF) < 0x8)
                {
                    result = memory[((memory[PC] & 0x01) << 4) | ((memory[PC+1] & 0xF0) >> 4)];
                    if((result & (1 << (memory[PC+1] & 0xF))) > 0)
                    {
                        PC+=2;
                        if(longOpcode(PC))
                        {
                            PC+=2;
                        }
                    }
                    PC+=2;
                    break;
                }
                handleUnimplemented();
            default:
                handleUnimplemented();
                break;
        }
        pushStatus(newStatus);
        resetFetchState();
#ifdef EMSCRIPTEN
        std::this_thread::yield();
#else
        std::this_thread::sleep_until(syncPoint);
#endif
        return true;
}
