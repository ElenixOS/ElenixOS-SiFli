#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "jerryscript-port.h"

double jerry_port_current_time(void)
{
    return 0.0;
}

int32_t jerry_port_local_tza(double unix_ms)
{
    (void)unix_ms;
    return 480;
}

void jerry_port_fatal(jerry_fatal_code_t code)
{
    printf("[JerryScript] Fatal error code: %d\n", code);
    switch (code)
    {
    case JERRY_FATAL_OUT_OF_MEMORY:
        printf("  Out of memory\n");
        break;
    case JERRY_FATAL_REF_COUNT_LIMIT:
        printf("  Reference count limit reached\n");
        break;
    case JERRY_FATAL_DISABLED_BYTE_CODE:
        printf("  Executed disabled instruction\n");
        break;
    case JERRY_FATAL_UNTERMINATED_GC_LOOPS:
        printf("  GC loop limit reached\n");
        break;
    case JERRY_FATAL_FAILED_ASSERTION:
        printf("  Assertion failed\n");
        break;
    default:
        printf("  Unknown error\n");
        break;
    }

    if (code != 0 && code != JERRY_FATAL_OUT_OF_MEMORY)
    {
        abort();
    }
    exit((int)code);
}
