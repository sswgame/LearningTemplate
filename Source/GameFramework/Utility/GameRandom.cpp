#include "pch.h"

#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    GameRandom::GameRandom( uint32 seed )
        : _state{ seed != 0 ? seed : kDefaultSeed }
    {
    }

    void GameRandom::setSeed( uint32 seed )
    {
        _state = seed != 0 ? seed : kDefaultSeed;
    }

    uint32 GameRandom::nextUint()
    {
        uint32 value = _state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        _state = value;
        return value;
    }

    float32 GameRandom::nextFloat()
    {
        return static_cast<float32>( nextUint() >> 8 ) * ( 1.0f / 16777216.0f );
    }

    float32 GameRandom::nextRange( float32 minValue, float32 maxValue )
    {
        return minValue + ( maxValue - minValue ) * nextFloat();
    }

    int32 GameRandom::nextInt( int32 minValue, int32 maxValue )
    {
        if ( maxValue <= minValue )
            return minValue;
        const uint32 span = static_cast<uint32>( maxValue - minValue ) + 1u;
        return minValue + static_cast<int32>( static_cast<uint64>( nextUint() ) * span >> 32 ); // 나머지 연산의 치우침 없이
    }
} // namespace sw
