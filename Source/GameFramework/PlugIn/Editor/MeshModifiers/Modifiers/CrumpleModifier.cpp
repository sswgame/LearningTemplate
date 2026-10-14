/**
 * @file CrumpleModifier.cpp
 * @brief 결정적 값 노이즈로 표면을 구겨뜨리는 모디파이어입니다.
 * @details 정점마다 노이즈 벡터(세 축) 와 법선 방향 노이즈(한 축)를 구해 `Normal Blend` 비율로 섞고 진폭을 곱해 더한다.
 *          노이즈는 정수 격자 해시 + 부드러운 삼선형 보간이라 같은 시드 · 같은 위치면 어느 기계에서나 같다. 진폭이 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierNoise.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct CrumpleModifierInternal
        {
            static constexpr float32 kAxisOffsetY = 31.416f;
            static constexpr float32 kAxisOffsetZ = 67.123f;
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
            const float3  noiseVector{ MeshModifierNoise::valueNoise3D( sample, seed ),
                                MeshModifierNoise::valueNoise3D( sample + float3{ CrumpleModifierInternal::kAxisOffsetY, 0.0f, 0.0f }, seed ),
                                MeshModifierNoise::valueNoise3D( sample + float3{ 0.0f, 0.0f, CrumpleModifierInternal::kAxisOffsetZ }, seed ) };
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
