/**
 * @file LightProbeBaker.h
 * @brief 라이트 프로브 볼륨을 CPU 로 굽습니다(IndirectLighting.md P2). 프로브마다 균일 구 방향으로 레이를 쏘아 들어오는 복사휘도를 SH9 로 투영합니다.
 * @details 프로브가 담는 것은 **간접광**입니다 — 레이가 닿은 면이 직접광을 받아 튕긴 빛(한 번 튐)과, 아무것도 닿지 않은 방향의 하늘입니다.
 *          직접광 자체는 담지 않습니다. 조명 식이 라이트를 이미 직접 계산하므로 프로브에 또 담으면 두 번 비춘다(유니티 · 언리얼의 간접광 베이크와 같은 나눔).
 *          닿은 면의 밝기는 조명 식(`lighting.hlsli` `swShadeLights`)과 같은 식이다 — 반사율 × Σ 세기 × 감쇠 × max(0, n·l) × 그림자.
 *          그림자와 가림은 물리 레이캐스트로 봅니다(물리 셰이프가 없는 메시는 빛을 가리지 않는다). 면의 반사율은 볼륨이 정한 값 하나입니다
 *          (`LightProbeVolumeComponent::getBounceAlbedo` — 물리 씬은 머티리얼을 모른다).
 *          하늘은 스카이 컴포넌트(D3-7.4)의 SH 를 받습니다. 스카이가 없으면 0 입니다(폴백 없음 — 하늘이 없는 씬의 빈 방향은 어둡다).
 *          같은 씬 · 같은 볼륨이면 결과가 같습니다(방향 수열이 결정적이다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/SphericalHarmonics.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Physics/IPhysicsQueryScene.h"

namespace sw
{
    class LightProbeVolumeComponent;
    class Scene;

    /**
     * @struct LightProbeBakeInput
     * @brief 굽기 한 번의 입력입니다. 씬에서 모으는 것은 `LightProbeBaker::collectInput` 이 합니다(테스트는 직접 채운다).
     */
    struct LightProbeBakeInput
    {
        const IPhysicsQueryScene3D* _pPhysics{ nullptr };  ///< 레이를 쏠 물리 씬(필수)
        const SH9Color*             _pSky{ nullptr };      ///< 하늘의 복사휘도. 없으면 하늘은 0 이다
        vector<GPULight>            _listLight;            ///< 씬 라이트(`collectSceneLights` 와 같은 원소)
        uint32                      _sampleCount{ 0 };     ///< 프로브마다 레이 수(0 이면 굽지 않는다)
        float32                     _bounceAlbedo{ 0.0f }; ///< 레이가 닿은 면의 반사율
    };
} // namespace sw

namespace sw
{
    /** @brief 라이트 프로브 볼륨의 CPU 베이크입니다(전부 static). */
    class SW_API LightProbeBaker
    {
    public:
        /**
         * @brief 씬과 볼륨에서 굽기 입력을 모읍니다 — 라이트(`collectSceneLights`), 물리 씬, 볼륨의 레이 수 · 반사율.
         * @return 3D 물리 씬이 없으면 false 이고 @p pOutError 에 이유를 적습니다(가림 없이 굽지 않는다).
         */
        [[nodiscard]] static bool collectInput( const Scene& scene, const LightProbeVolumeComponent& volume, LightProbeBakeInput& outInput, string* pOutError = nullptr );

        /**
         * @brief 격자 @p layout 의 프로브마다 간접광을 투영해 @p outGrid 를 채웁니다.
         * @return 입력이 모자라면(물리 씬이 없거나 레이 수가 0) false 이고 @p outGrid 는 비어 있습니다.
         */
        [[nodiscard]] static bool bakeGrid( const LightProbeGridLayout& layout, const LightProbeBakeInput& input, LightProbeGrid& outGrid, string* pOutError = nullptr );

        /**
         * @brief 위치 @p position · 노멀 @p normal 인 면이 라이트에서 직접 받는 빛(세기 × 감쇠 × max(0, n·l), 가려진 빛은 빼고)입니다.
         * @details 조명 식(`swShadeLights`)의 반사율 앞 항과 같은 식이다. 가림은 면에서 라이트 쪽으로 쏜 레이가 라이트보다 가까이에서 닿았는지로 본다.
         */
        static float3 computeDirectLight( const LightProbeBakeInput& input, const float3& position, const float3& normal );

        /**
         * @brief 3D 라이트 하나가 이 위치에 닿는 방향과 감쇠입니다. 2D 라이트 · 2D 그림자 세그먼트면 false 입니다.
         * @details 셰이더 `swComputeLightIncidence`(lighting.hlsli)와 같은 식이다 — 반경에서 자른 역제곱, 스폿은 원뿔을 곱한다.
         * @param outDistance 라이트까지의 거리(방향광은 음수 — 끝이 없다)
         */
        static bool computeLightIncidence( const GPULight& light, const float3& position, float3& outToLight, float32& outAttenuation, float32& outDistance );
    };
} // namespace sw
