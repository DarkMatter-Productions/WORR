#include "../cg_local.h"
#include "ui_cgame_access.h"
extern bool CG_IsActiveMultiplayerSession(void);

namespace ui {
bool CgameIsInGame()
{
	return cgi.CL_FrameValid();
}

bool CgameIsActiveMultiplayerSession()
{
	return CG_IsActiveMultiplayerSession();
}

const char *CgameConfigString(int index)
{
	return cgi.get_configString(index);
}
} // namespace ui
