// EXPECT-GEN: m._maxCallsPerSecond = 5u
// EXPECT-GEN: m._maxCallsPerSecond = static_cast<::uint32>( sw::fixture::kFixtureCallRate )
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    inline constexpr uint32 kFixtureCallRate = 3;

    REFLECT()
    struct RemoteCallRateType
    {
        REFLECT_BODY();

        FUNCTION( Server, MaxCallsPerSecond = 5 )
        void placeMarker( int32 markerID ) { (void)markerID; }

        FUNCTION( Server, MaxCallsPerSecond = sw::fixture::kFixtureCallRate )
        void wave( int32 waveID ) { (void)waveID; }
    };
} // namespace sw::fixture
