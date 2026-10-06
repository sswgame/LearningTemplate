#include "pch.h"

#include "GameFramework/Base/Utility/ValueNoise.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Utility/GameRandom.h"

namespace sw
{
    namespace
    {
        struct ValueNoiseInternal
        {
            static constexpr float32 smoothstep( float32 t ) { return t * t * ( 3.0f - 2.0f * t ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 ValueNoise::hashLattice( int32 x, int32 z, uint32 seed )
    {
        return GameHash::toUnitFloat( GameHash::hashCoord( x, z, seed ) );
    }

    float32 ValueNoise::sampleValue( float32 x, float32 z, uint32 seed )
    {
        const float32 floorX  = MathUtil::floor( x );
        const float32 floorZ  = MathUtil::floor( z );
        const int32   cellX   = static_cast<int32>( floorX );
        const int32   cellZ   = static_cast<int32>( floorZ );
        const float32 tx      = ValueNoiseInternal::smoothstep( x - floorX );
        const float32 tz      = ValueNoiseInternal::smoothstep( z - floorZ );
        const float32 v00     = hashLattice( cellX, cellZ, seed );
        const float32 v10     = hashLattice( cellX + 1, cellZ, seed );
        const float32 v01     = hashLattice( cellX, cellZ + 1, seed );
        const float32 v11     = hashLattice( cellX + 1, cellZ + 1, seed );
        const float32 rowNear = v00 + ( v10 - v00 ) * tx;
        const float32 rowFar  = v01 + ( v11 - v01 ) * tx;
        return rowNear + ( rowFar - rowNear ) * tz;
    }

    float32 ValueNoise::sampleFractal( float32 x, float32 z, uint32 seed, int32 octaveCount )
    {
        float32 sum       = 0.0f;
        float32 weight    = 0.0f;
        float32 amplitude = 1.0f;
        float32 frequency = 1.0f;
        for ( int32 octave = 0; octave < MathUtil::max( 1, octaveCount ); ++octave )
        {
            sum += amplitude * sampleValue( x * frequency, z * frequency, seed + static_cast<uint32>( octave ) * 7919u );
            weight += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        return sum / weight;
    }
} // namespace sw
