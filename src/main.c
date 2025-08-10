// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>

#include "flex_version.h"

int main()
{
    printf("Hello world\n%ld\n%s\n%d.%d.%d\n", __STDC_VERSION__, GIT_HASH, MAJOR, MINOR, PATCH);
    return 0;
}