#pragma once
#include "../../types.h"

bool        isDown(int pin);
ButtonEvent readButtonEvent(int pin);
bool        handleIdleRec();
int         handleIdleRecEx();   // 0=nothing, 1=short note (started), 2=bot double-press
bool        readPwrDouble(bool* singleOut = nullptr);   // non-blocking PWR classifier
