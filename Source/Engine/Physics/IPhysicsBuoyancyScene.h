/**
 * @file IPhysicsBuoyancyScene.h
 * @brief 물리 씬의 부력 쪽 인터페이스입니다(물 위에 뜨는 상자 · 배 · 떠내려가는 통). 3D 만 있습니다(차원 트레이트의 `kSupportsBuoyancy`).
 * @details 언리얼 Water 의 `BuoyancyComponent`, 유니티 수면 부력 스크립트의 자리다. 수면은 평면 하나(점 + 법선)이고, 잠긴 부피만큼 위로 밀고 잠긴 몫에 끌림을 건다
 *          (Jolt `Body::ApplyBuoyancyImpulse` — 셰이프의 잠긴 부피 · 부력 중심을 정확히 구한다). 고정 스텝마다 부른다(`BuoyancyVolumeComponent::prePhysicsStep`).
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 바디 하나에 거는 부력 한 번입니다. */
    struct PhysicsBuoyancyDesc
    {
        float3  _surfacePoint{};                    ///< 수면 위의 한 점(월드)
        float3  _surfaceNormal{ 0.0f, 1.0f, 0.0f }; ///< 수면 법선(위쪽, 정규화)
        float32 _buoyancy{ 1.1f };                  ///< 유체 밀도 ÷ 바디 밀도. 1 이면 떠 있지도 가라앉지도 않고, 클수록 높이 뜬다
        float32 _linearDrag{ 0.5f };                ///< 잠긴 몫의 선 끌림(1/초)
        float32 _angularDrag{ 0.05f };              ///< 잠긴 몫의 회전 끌림(1/초)
        float3  _fluidVelocity{ 0.0f, 0.0f, 0.0f }; ///< 유체의 흐름(월드, 미터/초) — 강물이 떠 있는 것을 끌고 간다
    };
} // namespace sw

namespace sw
{
    /** @brief 바디에 부력을 겁니다. */
    class IPhysicsBuoyancyScene
    {
    public:
        virtual ~IPhysicsBuoyancyScene() = default;

        /**
         * @brief 바디 하나에 고정 스텝 @p deltaSeconds 의 부력과 끌림을 겁니다. 움직이는(Dynamic) 바디만 받습니다.
         * @return 바디가 수면 아래에 조금이라도 잠겼으면 true 입니다. 무효 핸들 · 움직이지 않는 바디 · 잠기지 않은 바디는 false 입니다.
         */
        [[nodiscard]] virtual bool applyBuoyancy( PhysicsBodyHandle body, const PhysicsBuoyancyDesc& desc, float32 deltaSeconds ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 부력이 없는 차원(2D)의 씬이 `IPhysicsBuoyancyScene` 대신 상속하는 빈 자리입니다. 함수가 없으므로 2D 씬에 부르면 컴파일 오류입니다. */
    struct PhysicsNoBuoyancyScene
    {
    };
} // namespace sw
