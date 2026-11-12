// Arduino CI smoke sketch: compiles and links the whole VBAN SDK library
// (installed from library.properties) for the selected board.
// ESP32 cores define ESP_PLATFORM, so the lwIP socket layer is built too;
// boards without sockets (AVR, RP2040 without WiFi) get VBAN_ENABLE_NET=0.
#include <vban.h>
#include "vban_ci_smoke.h"

void setup()
{
    Serial.begin(115200);
    Serial.println(vban_ci_smoke() == 0 ? "vban smoke ok" : "vban smoke FAIL");
}

void loop() {}
