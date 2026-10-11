#include "pch.h"

#include "Core/Math/MathUtil.h"

#include <random>

namespace sw
{
    uint64 MathUtil::getRandomBits64()
    {
        thread_local std::mt19937_64 t_generator{ ( static_cast<uint64>( std::random_device{}() ) << 32 ) ^ std::random_device{}() };
        return t_generator();
    }
} // namespace sw
