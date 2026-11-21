/**
 * @file ClimbZoneComponent.h
 * @brief 사다리 · 밧줄 구역입니다.
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
     * @brief 사다리와 밧줄 구역입니다. 같은 오브젝트의 센서에 겹친 것은 `_axis` 를 따라 오를 수 있습니다. 이동 몸이 `findClimbZone` 으로 묻습니다. `_bSwing` 이 켜져 있으면 밧줄입니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Climb Zone", Tooltip = "Ladder or rope volume: overlapping objects may climb along the axis" )
    class SW_GF_API ClimbZoneComponent : public Component
    {
    public:
        REFLECT_BODY();

        ClimbZoneComponent();
        virtual ~ClimbZoneComponent() override = default;

        /** @brief 씬의 오르기 구역 목록(`ComponentRegistry`)에 등록합니다. `findClimbZone` 이 씬 전체를 순회하지 않고 이 목록만 봅니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief @p object 가 이 구역 안에 있으면 true 입니다. */
        bool contains( GameObjectHandle object ) const;
        /** @brief @p object 가 들어 있는 구역입니다. 등록된 구역에서 찾으며, 없으면 nullptr 입니다. */
        static const ClimbZoneComponent* findClimbZone( const GameObjectManager& manager, GameObjectHandle object );

        const float3& getAxis() const { return _axis; }
        float32       getClimbSpeed() const { return _climbSpeed; }
        bool          isSwing() const { return _bSwing; }

    private:
        PROPERTY( Category = "Climb", DisplayName = "Axis", Tooltip = "Climb direction (world)" )
        float3 _axis;
        PROPERTY( Category = "Climb", DisplayName = "Climb Speed", Min = 0.0, Units = "m/s" )
        float32 _climbSpeed;
        PROPERTY( Category = "Climb", DisplayName = "Swing", Tooltip = "Rope: can swing and jump off sideways" )
        bool _bSwing;
    };
} // namespace sw
