#ifndef MEMLEAKER_H__
#define MEMLEAKER_H__

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
#define MEMLEAK_OUTPUT() _CrtDumpMemoryLeaks()
#else
#define MEMLEAK_OUTPUT()
#endif


#endif
