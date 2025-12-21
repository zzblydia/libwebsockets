#include <stdio.h>

#include "ws-utils-adapter.h"

void log2file(int level, const char *line)
{
    printf("log2file: level=%d, line=%s", level, line);
}