/* ========================================================================== */
/*                                                                            */
/*   CoreSynchronisation.h                                                    */
/*                                                                            */
/* ========================================================================== */

#ifndef CoreSynchronisation_H 

#define CoreSynchronisation_H 




#if defined(__LINUX__)
#include "CoreLinuxSynchronisation.h"

#elif defined(__WIN32__)
#include "CoreWinSynchronisation.h"

#else
#error "Core Error! Unsupported Platform."
#endif


#endif

