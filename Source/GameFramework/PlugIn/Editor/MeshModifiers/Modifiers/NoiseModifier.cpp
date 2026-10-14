/**
 * @file NoiseModifier.cpp
 * @brief 결정적 3D 값 노이즈(정수 격자 해시 + 부드러운 삼선형 보간)로 세 축을 흔드는 모디파이어입니다.
 * @details 같은 시드 · 같은 위치면 어느 기계에서나 같은 값이다(해시는 32 비트 정수 연산만 쓴다). 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct NoiseModifierInternal
        {
            /** @brief 축마다 다른 노이즈를 얻으려고 더하는 어긋남입니다. */
            static constexpr float32 kAxisOffsetY = 31.416f;
            static constexpr float32 kAxisOffsetZ = 67.123f;

            static uint32 hashLattice( int32 x, int32 y, int32 z, uint32 seed )
            {
                uint32 hash = seed * 0x9E3779B1u;
                hash ^= static_cast<uint32>( x ) * 0x85EBCA6Bu;
                hash = ( hash << 13 ) | ( hash >> 19 );
                hash ^= static_cast<uint32>( y ) * 0xC2B2AE35u;
                hash = ( hash << 13 ) | ( hash >> 19 );
                hash ^= static_cast<uint32>( z ) * 0x27D4EB2Fu;
                // murmur3 마무리 섞기
                hash ^= hash >> 16;
                hash *= 0x85EBCA6Bu;
                hash ^= hash >> 13;
                hash *= 0xC2B2AE35u;
                hash ^= hash >> 16;
                return hash;
            }

            /** @brief 격자점 값 [-1, 1] 입니다. */
            static float32 getLatticeValue( int32 x, int32 y, int32 z, uint32 seed )
            {
                return static_cast<float32>( hashLattice( x, y, z, seed ) & 0xFFFFu ) / 32767.5f - 1.0f;
            }

            static float32 smooth( float32 t )
            {
                return t * t * ( 3.0f - 2.0f * t );
            }

            static float32 lerp( float32 from, float32 to, float32 t )
            {
                return from + ( to - from ) * t;
            }

            /** @brief 값 노이즈 [-1, 1] 입니다. */
            static float32 computeValueNoise( const float3& point, uint32 seed )
            {
                const float32 floorX = MathUtil::floor( point._x );
                const float32 floorY = MathUtil::floor( point._y );
                const float32 floorZ = MathUtil::floor( point._z );
                const int32   x      = static_cast<int32>( floorX );
                const int32   y      = static_cast<int32>( floorY );
                const int32   z      = static_cast<int32>( floorZ );
                const float32 tx     = smooth( point._x - floorX );
                const float32 ty     = smooth( point._y - floorY );
                const float32 tz     = smooth( point._z - floorZ );

                const float32 value00 = lerp( getLatticeValue( x, y, z, seed ), getLatticeValue( x + 1, y, z, seed ), tx );
                const float32 value10 = lerp( getLatticeValue( x, y + 1, z, seed ), getLatticeValue( x + 1, y + 1, z, seed ), tx );
                const float32 value01 = lerp( getLatticeValue( x, y, z + 1, seed ), getLatticeValue( x + 1, y, z + 1, seed ), tx );
                const float32 value11 = lerp( getLatticeValue( x, y + 1, z + 1, seed ), getLatticeValue( x + 1, y + 1, z + 1, seed ), tx );
                return lerp( lerp( value00, value10, ty ), lerp( value01, value11, ty ), tz );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 값 노이즈 흔들기입니다. */
    class NoiseModifier final : public MeshModifier
    {
    public:
        NoiseModifier()
            : MeshModifier()
            , _amplitudeParam{ addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 65535.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _strengthParam{ addParam( MeshModifierParamDef{ "Axis Strength", MeshModifierParamKind::Vector3, 0.0f, 10.0f, float3{ 1.0f, 1.0f, 1.0f } } ) }
            , _speedParam{ addParam( MeshModifierParamDef{ "Speed", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( amplitude < MathUtil::kEpsilon )
                return point._position;
            const uint32  seed     = static_cast<uint32>( getInt( _seedParam ) );
            const float32 drift    = getFloat( _speedParam ) * context.getTime();
            const float3  sample   = point._position * getFloat( _frequencyParam ) + float3{ drift, drift, drift };
            const float3& strength = getVector3( _strengthParam );
            const float3  offset{ NoiseModifierInternal::computeValueNoise( sample, seed ) * strength._x,
                                  NoiseModifierInternal::computeValueNoise( sample + float3{ NoiseModifierInternal::kAxisOffsetY, 0.0f, 0.0f }, seed ) * strength._y,
                                  NoiseModifierInternal::computeValueNoise( sample + float3{ 0.0f, 0.0f, NoiseModifierInternal::kAxisOffsetZ }, seed ) * strength._z };
            return point._position + offset * amplitude;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _frequencyParam;
        uint32 _seedParam;
        uint32 _strengthParam;
        uint32 _speedParam;
    };

    SW_MESH_MODIFIER( NoiseModifier, "noise", "Noise", "Noise" );
} // namespace sw::editor
