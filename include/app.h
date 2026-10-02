#pragma once
#include "aurora/model.h"
#include <Arduino.h>
namespace app {
extern aurora::Settings settings;
extern aurora::Grid grid;
extern aurora::KpHistory kp;
extern bool fetchFailed, cachedOnly;
extern uint32_t generation;
void networkBegin();
void networkLoop();
bool setupActive();
String networkStatus();
void requestRefresh();
void dataBegin();
void dataLoop();
void uiBegin();
void uiLoop();
}
