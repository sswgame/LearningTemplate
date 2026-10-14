/**
 * @file CrumpleModifier.cpp
 * @brief 결정적 값 노이즈로 표면을 구겨뜨리는 모디파이어입니다.
 * @details 정점마다 노이즈 벡터(세 축) 와 법선 방향 노이즈(한 축)를 구해 `Normal Blend` 비율로 섞고 진폭을 곱해 더한다.
 *          노이즈는 정수 격자 해시 + 부드러운 삼선형 보간이라 같은 시드 · 같은 위치면 어느 기계에서나 같다. 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct CrumpleModifierInternal
        {
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
    /** @brief 노이즈 구김입니다. */
    class CrumpleModifier final : public MeshModifier
    {
    public:
        CrumpleModifier()
            : MeshModifier()
            , _amplitudeParam{ addParam( MeshModifierParamDef{ "Amplitude", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Shape", "Crumple depth; 0 leaves the mesh unchanged", "m" ) ) }
            , _frequencyParam{ addParam( MeshModifierParamDef{ "Frequency", MeshModifierParamKind::Float, 0.001f, 100.0f, float3{ 2.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Number of wrinkles per meter" ) ) }
            , _seedParam{ addParam( MeshModifierParamDef{ "Seed", MeshModifierParamKind::Int, 0.0f, 65535.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Noise", "Random seed for the wrinkle pattern" ) ) }
            , _normalBlendParam{ addParam( MeshModifierParamDef{ "Normal Blend", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.5f, 0.0f, 0.0f } }.withLayout( "Shape", "0 pushes along noise directions, 1 pushes along vertex normals" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Crumples the surface with noise along the normals like crushed paper."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            const float32 amplitude = getFloat( _amplitudeParam );
            if ( amplitude < MathUtil::kEpsilon )
                return point._position;
            const uint32  seed   = static_cast<uint32>( getInt( _seedParam ) );
            const float3  sample = point._position * getFloat( _frequencyParam );
            const float3  noiseVector{ CrumpleModifierInternal::computeValueNoise( sample, seed ),
                                CrumpleModifierInternal::computeValueNoise( sample + float3{ CrumpleModifierInternal::kAxisOffsetY, 0.0f, 0.0f }, seed ),
                                CrumpleModifierInternal::computeValueNoise( sample + float3{ 0.0f, 0.0f, CrumpleModifierInternal::kAxisOffsetZ }, seed ) };
            const float3  along  = point._normal * noiseVector._x;
            const float32 blend  = MathUtil::saturate( getFloat( _normalBlendParam ) );
            const float3  offset = noiseVector * ( 1.0f - blend ) + along * blend;
            return point._position + offset * amplitude;
        }

    private:
        uint32 _amplitudeParam;
        uint32 _frequencyParam;
        uint32 _seedParam;
        uint32 _normalBlendParam;
    };

    SW_MESH_MODIFIER( CrumpleModifier, "crumple", "Crumple", "Noise" );
} // namespace sw::editor
