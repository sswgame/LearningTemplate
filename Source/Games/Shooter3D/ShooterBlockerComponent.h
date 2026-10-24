/**
 * @file ShooterBlockerComponent.h
 * @brief 아레나의 막는 상자 하나 — 벽 · 엄폐물. 오브젝트 자리를 가운데로 한 축 상자(반 크기)이고, 몸 · 적 · 탄 · 래그돌이 이것에 막힙니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 막는 축 상자입니다(바닥 y = 0 부터 쌓인다). */
    struct ShooterArenaBox
    {
        float3 _min{};
        float3 _max{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterBlockerComponent
     * @brief 씬에 놓인 벽 · 엄폐물 상자입니다. 디렉터가 플레이 시작에 모아 몸 이동 판정에 쓰고, 플레이 시작에 같은 상자의 정적 강체를 만들어
     *        물리 질의(히트스캔 · 시야)와 래그돌 · 떨어진 무기가 같은 상자에 막히게 합니다(틱하지 않는다).
     * @details 모양(벽 큐브 · 나무 상자 더미)은 같은 오브젝트 · 자식 오브젝트의 메시이고, 충돌은 이 상자 그대로다 — 에디터에서 오브젝트를 옮기면 함께 옮겨진다.
     *          상자의 기준은 `_halfSize` 하나다. 강체는 그 값으로 만든 런타임 사본이라 씬에 저장하지 않는다(데이터에 강체를 따로 적으면 두 크기가 어긋난다).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Blocker", Tooltip = "Axis-aligned box that blocks the player, the enemies, the shots and the ragdolls" )
    class ShooterBlockerComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterBlockerComponent();
        virtual ~ShooterBlockerComponent() override = default;

        /** @brief 같은 상자의 정적 강체를 만듭니다(이미 있으면 모양과 자리만 맞춥니다). */
        void onBeginPlay() override;

        /** @brief 오브젝트의 월드 자리를 가운데로 한 상자입니다. */
        ShooterArenaBox computeBox() const;
        void            setHalfSize( const float3& halfSize ) { _halfSize = halfSize; }

    private:
        PROPERTY( Category = "Blocker", DisplayName = "Half Size", Tooltip = "Half extents of the box around the object position", Units = m )
        float3 _halfSize;
        PROPERTY( Category = "Blocker", DisplayName = "Body Layer", Tooltip = "Physics layer of the static body made from the box (physics settings)" )
        hashed_string _bodyLayer;
    };
} // namespace sw
