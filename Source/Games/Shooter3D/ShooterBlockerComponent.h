/**
 * @file ShooterBlockerComponent.h
 * @brief 아레나의 막는 상자 하나 — 벽 · 엄폐물. 오브젝트 자리를 가운데로 한 축 상자(반 크기)이고, 몸 · 적 · 탄이 이것에 막힙니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

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
     * @brief 씬에 놓인 벽 · 엄폐물 상자입니다. 디렉터가 플레이 시작에 모아 충돌 · 광선 판정에 씁니다(틱하지 않는다).
     * @details 모양(벽 큐브 · 나무 상자 더미)은 같은 오브젝트 · 자식 오브젝트의 메시이고, 충돌은 이 상자 그대로다 — 에디터에서 오브젝트를 옮기면 함께 옮겨진다.
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Blocker", Tooltip = "Axis-aligned box that blocks the player, the enemies and the shots" )
    class ShooterBlockerComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterBlockerComponent();
        virtual ~ShooterBlockerComponent() override = default;

        /** @brief 오브젝트의 월드 자리를 가운데로 한 상자입니다. */
        ShooterArenaBox computeBox() const;
        void            setHalfSize( const float3& halfSize ) { _halfSize = halfSize; }

    private:
        PROPERTY( Category = "Blocker", DisplayName = "Half Size", Tooltip = "Half extents of the box around the object position", Meta = "Units=m" )
        float3 _halfSize;
    };
} // namespace sw
