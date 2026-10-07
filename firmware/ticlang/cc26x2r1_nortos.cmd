--stack_size=2048
--heap_size=8192

--retain "*(.resetVecs)"
--entry_point resetISR
--args 0x8
--diag_suppress=10063,16011,16012
--diag_remark=10068

#define FLASH_BASE 0x00000000
#define FLASH_SIZE 0x00058000
#define RAM_BASE   0x20000000
#define RAM_SIZE   0x00014000
#define GPRAM_BASE 0x11000000
#define GPRAM_SIZE 0x00002000

MEMORY
{
    FLASH (RX)  : origin = FLASH_BASE, length = FLASH_SIZE
    SRAM (RWX)  : origin = RAM_BASE, length = RAM_SIZE
    GPRAM (RWX) : origin = GPRAM_BASE, length = GPRAM_SIZE
    LOG_DATA (R) : origin = 0x90000000, length = 0x00040000
    LOG_PTR (R)  : origin = 0x94000008, length = 0x00040000
}

SECTIONS
{
    .resetVecs   : > FLASH_BASE
    .text        : > FLASH
    .TI.ramfunc  : {} load = FLASH, run = SRAM, table(BINIT)
    .const       : > FLASH
    .constdata   : > FLASH
    .rodata      : > FLASH
    .binit       : > FLASH
    .cinit       : > FLASH
    .pinit       : > FLASH
    .init_array  : > FLASH
    .emb_text    : > FLASH
    .ccfg        : > FLASH (HIGH)

    .ramVecs     : > SRAM, type = NOLOAD, ALIGN(256)
    .data        : > SRAM
    .bss         : > SRAM
    .sysmem      : > SRAM
    .stack       : > SRAM (HIGH)
    .nonretenvar : > SRAM
    .gpram       : > GPRAM
    .log_data    : > LOG_DATA, type = COPY
    .log_ptr     : { *(.log_ptr*) } > LOG_PTR align 4, type = COPY
}
