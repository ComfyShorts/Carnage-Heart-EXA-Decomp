/* PSP shim: DMA. The GE lives in ge.c and the display in display.c. */
#include <string.h>

#include "psp_imports.h"
#include "psp_shim.h"

/* ---- sceDmac (1 import) ---- */

int32_t sceDmacMemcpy(void *dst, const void *src, unsigned int size)
{
    memmove(dst, src, size);
    PSP_TRACE(0, "%p, %p, %u", dst, src, size);
    return 0;
}
