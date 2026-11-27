/* COkeDataBase: one OKE design (hardware + software), 0x1EC0 bytes.
 * Stored raw in the pack and in memory, so this struct is also the file format.
 * Layout from docs/program-format.md; fields not understood yet stay byte arrays. */
#ifndef OKEDB_H
#define OKEDB_H

#include <stdint.h>
#include <string.h>

#include "pgchip.h"

#define OKE_RECORD_SIZE 0x1EC0
#define OKE_HW_CPU_PART 0x288 /* s32 in the hardware block: CPU part id (1-7) */

typedef struct SoftwareDataBase {            /* COkeDataBase + 0xFE0 */
    PgChipCommand main[PGCHIP_MAIN_CAP * PGCHIP_MAIN_CAP];   /* 0x000 */
    PgChipCommand sub1[PGCHIP_SUB_CAP * PGCHIP_SUB_CAP];     /* 0x708 */
    PgChipCommand sub2[PGCHIP_SUB_CAP * PGCHIP_SUB_CAP];     /* 0x890 */
    int32_t width[PGCHIP_PROGRAM_MAX];                        /* 0xA18; 0 = no sub-program */
    int32_t start[PGCHIP_PROGRAM_MAX];                        /* 0xA24; compact y*width+x */
    char counter_name[8][6];                                  /* 0xA30; tagCOUNTER */
} SoftwareDataBase;

typedef struct OkeDataBase {
    uint8_t header[0x30];         /* 0x0000 */
    uint8_t hardware[0xFB0];      /* 0x0030 */
    SoftwareDataBase software;    /* 0x0FE0 */
    uint8_t comments[0x460];      /* 0x1A40: 10 tag lists + 10 texts */
    int32_t color_block[7];       /* 0x1EA0: word 6 = colour index */
    uint32_t tail;                /* 0x1EBC */
} OkeDataBase;

_Static_assert(sizeof(SoftwareDataBase) == 0xA60, "SoftwareDataBase layout");
_Static_assert(__builtin_offsetof(SoftwareDataBase, width) == 0xA18, "SoftwareDataBase layout");
_Static_assert(__builtin_offsetof(OkeDataBase, software) == 0xFE0, "COkeDataBase layout");
_Static_assert(__builtin_offsetof(OkeDataBase, color_block) == 0x1EA0, "COkeDataBase layout");
_Static_assert(sizeof(OkeDataBase) == OKE_RECORD_SIZE, "COkeDataBase layout");

static inline int32_t oke_cpu_part(const OkeDataBase *oke)
{
    int32_t v;
    memcpy(&v, oke->hardware + OKE_HW_CPU_PART, sizeof v);
    return v;
}

static inline const PgChipCommand *oke_program_chips(const OkeDataBase *oke, int program)
{
    return program == 0 ? oke->software.main : program == 1 ? oke->software.sub1
                                                            : oke->software.sub2;
}

#endif
