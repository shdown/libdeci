#include <stdbool.h>
#include "../deci.h"

bool WRAPPED_deci_sub(
    deci_UWORD *wa, deci_UWORD *wa_end,
    deci_UWORD *wb, deci_UWORD *wb_end)
{
    return deci_sub(wa, wa_end, wb, wb_end);
}
