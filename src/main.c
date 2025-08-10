// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>

#include "git_hash.h"

int main()
{
    printf("Hello world\n%ld\n%s\n", __STDC_VERSION__, GIT_HASH);
    return 0;
}