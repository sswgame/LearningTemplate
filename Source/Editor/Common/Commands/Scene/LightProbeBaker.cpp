#include "pch.h"

#include "Editor/Common/Commands/Scene/LightProbeBaker.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
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
            /// @brief 반경 · 거리의 하한 — 셰이더 `LightData::computeIncidence` 의 1e-4 와 같은 자리다.
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
    bool LightProbeBaker::collectInput( const Scene& scene, const LightProbeVolumeComponent& volume, unique_ptr<IPhysicsScene3D>& outOccluders,
                                        LightProbeBakeInput& outInput, string* pOutError )
    {
        outInput                          = LightProbeBakeInput{};
        const GameObjectManager* pObjects = scene.getObjectManager();
        // 플레이 씬이 아니라 정적 콜라이더만의 새 씬에 쏜다 — 편집 중에는 바디가 없고, 플레이 중이어도 움직이는 바디는 베이크에 들지 않는다.
        outOccluders = ( pObjects != nullptr ) ? pObjects->getSubsystem<ScenePhysics>().createStaticScene3D() : nullptr;
        if ( outOccluders == nullptr )
        {
            if ( pOutError != nullptr )
                *pOutError = "the physics service is not running - light probes are not baked without occlusion";
            return false;
        }
        outInput._pPhysics = outOccluders.get();
        // 하늘이 없으면 findSky 가 세기 0 으로 비운다 — 빈 방향은 0 이다(폴백 없음).
        (void)SkyComponent::findSky( *pObjects, outInput._sky );
        outInput._sampleCount  = volume.getBakeSampleCount();
        outInput._bounceAlbedo = volume.getBounceAlbedo();
        collectSceneLights( &scene, outInput._listLight );
        return true;
    }

    bool LightProbeBaker::bakeGrid( const LightProbeGridLayout& layout, const LightProbeBakeInput& input, LightProbeGrid& outGrid, string* pOutError )
    {
        // 한 번에 베이크도 나눠 베이크와 같은 길이다 — 결과가 같다(`LightProbeBakeJob`).
        outGrid.clear();
        LightProbeBakeJob job;
        if ( job.begin( layout, input, pOutError ) == false )
            return false;
        (void)job.step( job.getProbeCount() );
        return job.takeGrid( outGrid );
    }

    bool LightProbeBakeJob::begin( const LightProbeGridLayout& layout, const LightProbeBakeInput& input, string* pOutError )
    {
        using Internal           = LightProbeBakerInternal;
        *this                    = LightProbeBakeJob{};
        const bool bMissingInput = input._pPhysics == nullptr || input._sampleCount == 0 || layout.getProbeCount() == 0;
        if ( bMissingInput )
        {
            if ( pOutError != nullptr )
                *pOutError = "light probe bake needs a physics scene, at least one sample and at least one probe";
            return false;
        }
        _input = input;
        // 방향은 한 번만 만든다 — 모든 프로브가 같은 수열을 쓴다.
        _listDirection.reserve( input._sampleCount );
        DeterministicRandom32 random{ Internal::kSeed };
        for ( uint32 sampleIndex = 0; sampleIndex < input._sampleCount; ++sampleIndex )
        {
            _listDirection.push_back( SH9Color::makeUniformSphereDirection( random ) );
        }
        _weight       = 4.0f * MathUtil::kPi / static_cast<float32>( input._sampleCount );
        _grid._layout = layout;
        _grid._listProbe.assign( layout.getProbeCount(), SH9Color{} );
        _nextProbeIndex = 0;
        _bBegun         = true;
        return true;
    }

    bool LightProbeBakeJob::step( uint32 maxProbeCount )
    {
        using Internal = LightProbeBakerInternal;
        if ( _bBegun == false )
            return false;
        const IPhysicsQueryScene3D& physics  = *_input._pPhysics;
        const uint32                endIndex = MathUtil::min( getProbeCount(), _nextProbeIndex + maxProbeCount );
        for ( ; _nextProbeIndex < endIndex; ++_nextProbeIndex )
        {
            const float3 probePosition = _grid._layout.computeProbePosition( _nextProbeIndex );
            SH9Color&    probe         = _grid._listProbe[_nextProbeIndex];
            for ( const float3& direction : _listDirection )
            {
                PhysicsCastHit3D hit;
                float3           radiance{ 0.0f, 0.0f, 0.0f };
                if ( physics.raycast( probePosition, direction, Internal::kMaxRayDistance, PhysicsQueryFilter{}, hit ) )
                {
                    // 닿은 면이 튕겨 보내는 빛 — 반사율 × 그 면이 받는 직접광(조명 식과 같은 단위).
                    const float3 surface = hit._point + hit._normal * Internal::kSurfaceOffset;
                    radiance             = LightProbeBaker::computeDirectLight( _input, surface, hit._normal ) * _input._bounceAlbedo;
                }
                else
                {
                    radiance = _input._sky.evaluateRadiance( direction ); // 스카이가 없으면 세기 0 이라 0 이다
                }
                probe.addSample( direction, radiance, _weight );
            }
        }
        return isDone();
    }

    bool LightProbeBakeJob::takeGrid( LightProbeGrid& outGrid )
    {
        if ( isDone() == false )
            return false;
        outGrid = std::move( _grid );
        _grid.clear();
        _bBegun = false;
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
