#include "pch.h"

#include "Engine/Renderer/Light/LightProbeBaker.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    namespace
    {
        struct LightProbeBakerInternal
        {
            /// @brief 방향 수열의 씨앗 — 모든 프로브가 같은 방향을 쏜다(결정적이고, 이웃 프로브의 잡음이 같은 모양이라 보간에서 튀지 않는다).
            static constexpr uint32 kSeed = 0x2545F491u;
            /// @brief 레이의 최대 길이(m) — 이보다 먼 면은 하늘로 본다.
            static constexpr float32 kMaxRayDistance = 1000.0f;
            /// @brief 닿은 면에서 그림자 레이를 띄우는 거리(m) — 그 면 자신에 닿지 않게 한다.
            static constexpr float32 kSurfaceOffset = 0.01f;
            /// @brief 반경 · 거리의 하한 — 셰이더 `swComputeLightIncidence` 의 1e-4 와 같은 자리다.
            static constexpr float32 kMinDistance = 1e-4f;

            /** @brief 라이트 원소의 종류입니다. */
            static uint32 loadLightType( const GPULight& light ) { return static_cast<uint32>( light._directionType._w + 0.5f ); }

            /** @brief 면에서 라이트 쪽으로 쏜 레이가 라이트보다 가까이에서 닿으면 가려진 것입니다. 방향광은 끝이 없다. */
            static bool isOccluded( const IPhysicsQueryScene3D& physics, const float3& origin, const float3& toLight, float32 distance )
            {
                const float32    maxDistance = ( distance < 0.0f ) ? kMaxRayDistance : MathUtil::max( distance - kSurfaceOffset, 0.0f );
                PhysicsCastHit3D hit;
                return maxDistance > 0.0f && physics.raycast( origin, toLight, maxDistance, PhysicsQueryFilter{}, hit );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LightProbeBaker::collectInput( const Scene& scene, const LightProbeVolumeComponent& volume, LightProbeBakeInput& outInput, string* pOutError )
    {
        outInput                             = LightProbeBakeInput{};
        const GameObjectManager*    pObjects = scene.getObjectManager();
        const IPhysicsQueryScene3D* pPhysics = ( pObjects != nullptr ) ? pObjects->getSubsystem<ScenePhysics>().findScene3D() : nullptr;
        if ( pPhysics == nullptr )
        {
            if ( pOutError != nullptr )
                *pOutError = "the scene has no 3D physics scene - light probes are not baked without occlusion";
            return false;
        }
        outInput._pPhysics = pPhysics;
        // 하늘(D3-7.4 스카이 컴포넌트)은 아직 없다 — 빈 방향은 0 이다(폴백 없음).
        outInput._pSky         = nullptr;
        outInput._sampleCount  = volume.getBakeSampleCount();
        outInput._bounceAlbedo = volume.getBounceAlbedo();
        collectSceneLights( &scene, outInput._listLight );
        return true;
    }

    bool LightProbeBaker::bakeGrid( const LightProbeGridLayout& layout, const LightProbeBakeInput& input, LightProbeGrid& outGrid, string* pOutError )
    {
        using Internal = LightProbeBakerInternal;
        outGrid.clear();
        const bool bMissingInput = input._pPhysics == nullptr || input._sampleCount == 0 || layout.getProbeCount() == 0;
        if ( bMissingInput )
        {
            if ( pOutError != nullptr )
                *pOutError = "light probe bake needs a physics scene, at least one sample and at least one probe";
            return false;
        }

        // 방향은 한 번만 만든다 — 모든 프로브가 같은 수열을 쓴다.
        vector<float3> listDirection;
        listDirection.reserve( input._sampleCount );
        uint32 state = Internal::kSeed;
        for ( uint32 sampleIndex = 0; sampleIndex < input._sampleCount; ++sampleIndex )
        {
            listDirection.push_back( SH9Color::makeUniformSphereDirection( state ) );
        }

        const IPhysicsQueryScene3D& physics = *input._pPhysics;
        const float32               weight  = 4.0f * MathUtil::kPi / static_cast<float32>( input._sampleCount );
        outGrid._layout                     = layout;
        outGrid._listProbe.assign( layout.getProbeCount(), SH9Color{} );
        for ( uint32 probeIndex = 0; probeIndex < layout.getProbeCount(); ++probeIndex )
        {
            const float3 probePosition = layout.computeProbePosition( probeIndex );
            SH9Color&    probe         = outGrid._listProbe[probeIndex];
            for ( const float3& direction : listDirection )
            {
                PhysicsCastHit3D hit;
                float3           radiance{ 0.0f, 0.0f, 0.0f };
                if ( physics.raycast( probePosition, direction, Internal::kMaxRayDistance, PhysicsQueryFilter{}, hit ) )
                {
                    // 닿은 면이 튕겨 보내는 빛 — 반사율 × 그 면이 받는 직접광(조명 식과 같은 단위).
                    const float3 surface = hit._point + hit._normal * Internal::kSurfaceOffset;
                    radiance             = computeDirectLight( input, surface, hit._normal ) * input._bounceAlbedo;
                }
                else if ( input._pSky != nullptr )
                {
                    radiance = input._pSky->evaluateRadiance( direction );
                }
                probe.addSample( direction, radiance, weight );
            }
        }
        return true;
    }

    float3 LightProbeBaker::computeDirectLight( const LightProbeBakeInput& input, const float3& position, const float3& normal )
    {
        using Internal = LightProbeBakerInternal;
        float3 result{ 0.0f, 0.0f, 0.0f };
        for ( const GPULight& light : input._listLight )
        {
            float3  toLight{};
            float32 attenuation = 0.0f;
            float32 distance    = 0.0f;
            if ( computeLightIncidence( light, position, toLight, attenuation, distance ) == false )
                continue;
            const float32 normalDotLight = MathUtil::max( normal.dot( toLight ), 0.0f );
            const float32 strength       = normalDotLight * attenuation * light._colorIntensity._w;
            if ( strength <= 0.0f )
                continue;
            if ( input._pPhysics != nullptr && Internal::isOccluded( *input._pPhysics, position, toLight, distance ) )
                continue;
            result += float3{ light._colorIntensity._x, light._colorIntensity._y, light._colorIntensity._z } * strength;
        }
        return result;
    }

    bool LightProbeBaker::computeLightIncidence( const GPULight& light, const float3& position, float3& outToLight, float32& outAttenuation, float32& outDistance )
    {
        using Internal         = LightProbeBakerInternal;
        const uint32 lightType = Internal::loadLightType( light );
        outToLight             = float3{ -light._directionType._x, -light._directionType._y, -light._directionType._z };
        outAttenuation         = 1.0f;
        outDistance            = -1.0f;
        const bool bLight3D    = lightType <= shaderslot::kLightTypeSpot;
        if ( bLight3D == false )
            return false;
        if ( lightType == shaderslot::kLightTypeDirectional )
            return true;

        // 점광과 스폿 — 반경에서 자른 역제곱(언리얼 InverseSquaredFalloff 와 같은 모양), 스폿은 원뿔을 곱한다.
        const float3  delta      = float3{ light._positionRadius._x, light._positionRadius._y, light._positionRadius._z } - position;
        const float32 distance   = delta.getLength();
        const float32 radius     = MathUtil::max( light._positionRadius._w, Internal::kMinDistance );
        outToLight               = delta * ( 1.0f / MathUtil::max( distance, Internal::kMinDistance ) );
        outDistance              = distance;
        const float32 normalized = MathUtil::saturate( 1.0f - distance / radius );
        outAttenuation           = normalized * normalized;
        if ( lightType == shaderslot::kLightTypeSpot )
        {
            const float3  lightForward{ light._directionType._x, light._directionType._y, light._directionType._z };
            const float32 cosAngle = lightForward.dot( outToLight * -1.0f );
            const float32 cosOuter = light._params._y;
            const float32 cosInner = light._params._z;
            const float32 cone     = MathUtil::saturate( ( cosAngle - cosOuter ) / MathUtil::max( cosInner - cosOuter, Internal::kMinDistance ) );
            outAttenuation *= cone * cone;
        }
        return true;
    }
} // namespace sw
