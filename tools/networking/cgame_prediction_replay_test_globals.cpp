/*
 * The engine and cgame are separate production modules and therefore each
 * owns a symbol named cl_predict with different linkage.  This headless
 * integration executable links both modules into one process, so retain the
 * cgame-owned C++ symbol here and configure it through an explicit C test
 * seam.
 */

#include "shared/shared.h"

#include <cstdint>

cvar_t *cl_predict{};

extern "C" {
std::uint64_t com_unscaledTimeUs{};
}

extern "C" void Worr_TestSetCGamePredictCvar(cvar_t *value)
{
    cl_predict = value;
}
