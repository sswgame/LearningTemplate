// EXPECT-ERROR: value is not a whole number of at least 1
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct ZeroCallRateType
    {
        REFLECT_BODY();

        FUNCTION( Server, MaxCallsPerSecond = 0 )
        void placeMarker( int32 markerID ) { (void)markerID; }
    };
} // namespace sw::fixture
