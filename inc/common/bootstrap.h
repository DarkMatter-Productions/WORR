#pragma once

#include "shared/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*com_bootstrap_ready_callback_t)(void *userdata);

q_exported void Com_SetBootstrapReadyCallback(com_bootstrap_ready_callback_t callback, void *userdata);
void Com_BootstrapSignalReady(void);
q_exported int WORR_EngineMain(int argc, char **argv);

#ifdef __cplusplus
}
#endif
