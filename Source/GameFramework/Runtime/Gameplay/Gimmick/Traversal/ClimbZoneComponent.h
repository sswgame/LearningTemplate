/**
 * @file ClimbZoneComponent.h
 * @brief 사다리 · 밧줄 · 등반 벽 구역입니다.
 * @details 선언은 이동기(캐릭터 층)가 읽는 자리라 캐릭터 층이고(`GameFrameworkLayers.toml`), 구현(.cpp)은 기믹 센서를 보는 Gameplay 층이다 — `MovementConstraint` 와 같은 나눔.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ClimbZoneComponent
     * @brief 사다리 · 밧줄 · 등반 벽 구역입니다. 같은 오브젝트의 센서에 겹친 것은 `_axis` 를 따라 오를 수 있습니다. 캐릭터 이동기(`CharacterMovementComponent`)가 `findClimbZone` 으로 묻습니다.
     * @details 오르는 쪽이 보는 벽 방향은 이 오브젝트의 앞(+Z)이다(`computeFacing`). 옆 속도(`_lateralSpeed`)가 있으면 등반 벽이고, `_bSwing` 이면 밧줄이다(가는 쪽으로 뛰어내린다).
     *          틱 중에 바뀌는 칸이 없어(겹침 목록은 게임 스레드의 겹침 사건만 바꾼다) 이동기가 병렬 틱에서 읽는다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Climb Zone", Tooltip = "Ladder, rope or climbable wall volume: overlapping characters may climb along the axis" )
    class SW_GF_API ClimbZoneComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr bool kTickReadSafe = true;

        ClimbZoneComponent();
        virtual ~ClimbZoneComponent() override = default;

        /** @brief 씬의 오르기 구역 목록(`ComponentRegistry`)에 등록합니다. `findClimbZone` 이 씬 전체를 순회하지 않고 이 목록만 봅니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 오르는 축이 있는지 봅니다(0 이면 오류). */
        void onBeginPlay() override;
        /** @brief @p object 가 이 구역 안에 있으면 true 입니다. */
        bool contains( GameObjectHandle object ) const;
        /** @brief @p object 가 들어 있는 구역입니다. 등록된 구역에서 찾으며, 없으면 nullptr 입니다. */
        static const ClimbZoneComponent* findClimbZone( const GameObjectManager& manager, GameObjectHandle object );

        /** @brief 오르는 축(월드, 길이 1)입니다. 저작 값이 0 이면 0 이고(오르지 못한다) 시작할 때 오류를 남깁니다. */
        float3 computeAxis() const;
        /** @brief 오르는 쪽이 보는 벽 방향(월드, 수평, 길이 1)입니다 — 이 오브젝트의 앞(+Z). */
        float3  computeFacing() const;
        float32 getClimbSpeed() const { return _climbSpeed; }
        float32 getLateralSpeed() const { return _lateralSpeed; }
        bool    isSwing() const { return _bSwing; }

    private:
        PROPERTY( Category = "Climb", DisplayName = "Axis", Tooltip = "Climb direction (world)" )
        float3 _axis;
        PROPERTY( Category = "Climb", DisplayName = "Climb Speed", Min = 0.0, Units = "m/s" )
        float32 _climbSpeed;
        PROPERTY( Category = "Climb", DisplayName = "Lateral Speed", Min = 0.0, Tooltip = "Sideways climb speed (climbable wall); 0 = ladder or rope", Units = "m/s" )
        float32 _lateralSpeed;
        PROPERTY( Category = "Climb", DisplayName = "Swing", Tooltip = "Rope: can swing and jump off sideways" )
        bool _bSwing;
    };
} // namespace sw
