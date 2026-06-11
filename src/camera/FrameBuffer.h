#pragma once

#include <cstddef>

struct FrameBuffer
{
    void *start = nullptr;
    std::size_t length = 0;

    bool isMapped() const { return start != nullptr && length > 0; }
};
