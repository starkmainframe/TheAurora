#pragma once
#if defined(BOARD_AMOLED_143) && !defined(BOARD_AMOLED_175)
#include "boards/board_amoled_143.h"
#elif defined(BOARD_AMOLED_175) && !defined(BOARD_AMOLED_143)
#include "boards/board_amoled_175.h"
#else
#error Select exactly one supported TheAurora board
#endif
