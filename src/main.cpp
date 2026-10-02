#include "app.h"
#include "board.h"
#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  Serial.printf("\nTheAurora v1 / %s\n",board::name);
  app::networkBegin();
  app::uiBegin();
  app::dataBegin();
}
void loop() {
  app::networkLoop();
  app::dataLoop();
  app::uiLoop();
  delay(5);
}
