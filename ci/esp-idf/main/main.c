#include <stdio.h>
#include "vban_ci_smoke.h"

void app_main(void)
{
    printf("vban smoke: %s\n", vban_ci_smoke() == 0 ? "ok" : "FAIL");
}
