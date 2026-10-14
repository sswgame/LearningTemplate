/**
 * @file ZStretchNoiseModifier.cpp
 * @brief 한 축(기본 Z) 방향으로 늘이고 그 축 방향 노이즈를 더하는 모디파이어입니다.
 * @details 축 좌표' = 축 좌표 · (1 + 양) + 노이즈 · noise(위치 · 주파수, 시드). 기즈모 중심(원점)이 늘이기의 고정점이다. 양 · 노이즈가 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct ZStretchNoiseModifierInternal
        {
            static uint32 hashLattice( int32 x, int32 y, int32 z, uint32 seed )
            {
                uint32 hash = seed * 0x9E3779B1u;
                hash ^= static_cast<uint32>( x ) * 0x85EBCA6Bu;
                hash = ( hash << 13 ) | ( hash >> 19 );
                hash ^= static_cast<uint32>( y ) * 0xC2B2AE35u;
                hash = ( hash << 13 ) | ( hash >> 19 );
                hash ^= static_cast<uint32>( z ) * 0x27D4EB2Fu;
                hash ^= hash >> 16;
                hash *= 0x85EBCA6Bu;
                hash ^= hash >> 13;
                hash *= 0xC2B2AE35u;
                hash ^= hash >> 16;
                return hash;
            }

            static float32 getLatticeValue( int32 x, int32 y, int32 z, uint32 seed )
            {
                return static_cast<float32>( hashLattice( x, y, z, seed ) & 0xFFFFu ) / 32767.5f - 1.0f;
            }

            static float32 smooth( float32 t ) { return t * t * ( 3.0f - 2.0f * t ); }

            static float32 lerp( float32 from, float32 to, float32 t ) { return from + ( to - from ) * t; }

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
    /** @brief 축 늘이기 + 노이즈입니다. */
    class ZStretchNoiseModifier final : public MeshModifier
    {
    public:
        ZStretchNoiseModifier()
            : MeshModifier()
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -0.99f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Stretch along the axis; 0 keeps the original length" ) ) }
            , _noiseParam{ addParam( MeshModifierParamDef{ "Noise", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Noise strength added to the stretch; 0 disables noise" ) ) }
            , _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Noise features per meter" ) ) }
            , _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 65535.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Random seed for the noise pattern" ) ) }
            , _stretchAxisParam{ addParam( MeshModifierParamDef{ "Stretch Axis", MeshModifierParamKind::Axis, 0.0f, 2.0f, float3{ 2.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Axis the mesh is stretched along" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Stretches the mesh along an axis with a noisy variation."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            const float32 amount = getFloat( _amountParam );
            const float32 noise  = getFloat( _noiseParam );
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon && MathUtil::abs( noise ) < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis       = getAxisIndex( _stretchAxisParam );
            const uint32  seed       = static_cast<uint32>( getInt( _seedParam ) );
            const float32 coordinate = MeshModifierUtil::getComponent( point._position, axis );
            const float32 wobble     = ZStretchNoiseModifierInternal::computeValueNoise( point._position * getFloat( _frequencyParam ), seed );
            float3        result     = point._position;
            MeshModifierUtil::setComponent( result, axis, coordinate * ( 1.0f + amount ) + noise * wobble );
            return result;
        }

    private:
        uint32 _amountParam;
        uint32 _noiseParam;
        uint32 _frequencyParam;
        uint32 _seedParam;
        uint32 _stretchAxisParam;
    };

    SW_MESH_MODIFIER( ZStretchNoiseModifier, "zstretchnoise", "ZStretch Noise", "Noise" );
} // namespace sw::editor
