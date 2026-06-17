#ifndef WORR_Q2AAS_COMPAT_H
#define WORR_Q2AAS_COMPAT_H

#if defined(_WIN32) && !defined(WIN32)
#define WIN32 1
#endif

#if defined(BSPC)
int COM_Compress(char *data_p);
#endif

#endif
