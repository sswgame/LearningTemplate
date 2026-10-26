#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Core/Math/SecureRandom.h"

#include <random>

namespace sw
{
    uint64 MathUtil::getRandomBits64()
    {
        thread_local std::mt19937_64 t_generator{ SecureRandom::nextUint64() };
        return t_generator();
    }
} // namespace sw
