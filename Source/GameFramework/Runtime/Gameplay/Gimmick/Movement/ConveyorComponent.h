/**
 * @file ConveyorComponent.h
 * @brief 센서에 겹친 것을 면 속도로 옮기는 컨베이어 · 흐르는 물입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ConveyorComponent
     * @brief 컨베이어와 흐르는 물입니다. 같은 오브젝트의 센서에 겹친 것을 `_velocity`(월드, m/s)로 옮깁니다. 틱 쓰기 큐를 거쳐 다른 오브젝트의 위치를 세터로 씁니다.
     *        이동 몸이 스스로 움직이는 게임은 `getSurfaceVelocity` 를 읽어서 더하고 `_bMoveOccupants` 를 끕니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Conveyor", Tooltip = "Carries overlapping objects at a surface velocity" )
    class SW_GF_API ConveyorComponent : public Component
    {
    public:
        REFLECT_BODY();

        ConveyorComponent();
        virtual ~ConveyorComponent() override = default;

        void onTick( TickContext& context ) override;

        const float3& getSurfaceVelocity() const { return _velocity; }

    private:
        PROPERTY( Category = "Conveyor", DisplayName = "Velocity", Units = "m/s" )
        float3 _velocity;
        PROPERTY( Category = "Conveyor", DisplayName = "Move Occupants", Tooltip = "Move overlapping objects directly" )
        bool _bMoveOccupants;

        float32 _pendingSeconds; ///< 틱이 쌓고 틱 뒤 옮기기가 쓰는 시간
    };
} // namespace sw
