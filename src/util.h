// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdlib.h>

inline void* quick_calloc(size_t count, size_t size)
{
    void* ptr = calloc(count, size);
    if (!ptr)
        abort();
    return ptr;
}

inline void* quick_malloc(size_t size)
{
    return quick_calloc(1, size);
}