/**
 * @file PushBlockComponent.h
 * @brief 한 셀 단위로 미는 블록(젤다 · 소코반)입니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class PushBlockComponent
     * @brief 한 셀 단위로 미는 블록입니다(젤다, 소코반). `push( 방향 )` 은 방향을 가장 가까운 축으로 맞추고, 한 셀 앞이 비어 있으면(`WorldQuery` 광선으로 확인) `_moveTime` 동안 블록을 옮깁니다.
     * @details 같은 오브젝트의 `InteractableComponent`(Push)를 끝내면 끝낸 쪽에서 블록 방향으로 민다(완료 알림). 2D 는 `_bPlanar2D`(XY 축)를 켠다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Push Block", Tooltip = "Grid-snapped block pushed one cell at a time if the cell ahead is free" )
    class SW_GF_API PushBlockComponent : public Component
    {
    public:
        REFLECT_BODY();

        PushBlockComponent();
        virtual ~PushBlockComponent() override = default;

        /** @brief 같은 오브젝트의 상호작용 완료를 듣습니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 한 스텝(60 Hz)입니다. */
        void stepOnce();
        /** @brief @p direction 에서 가장 가까운 축으로 한 셀 밉니다. 움직이는 중이거나 막혀 있으면 false 입니다. */
        [[nodiscard]] bool push( const float3& direction );
        bool               isMoving() const { return _stepsLeft > 0; }

    private:
        /** @brief 상호작용(Push)을 끝낸 쪽에서 블록 방향으로 밉니다. */
        void handleUsed( const GameObject& user );

    private:
        Subscription _usedSubscription; ///< 상호작용 완료 구독(`handleUsed`)
        PROPERTY( Category = "Push", DisplayName = "Cell Size", Min = 0.01, Units = m )
        float32 _cellSize;
        PROPERTY( Category = "Push", DisplayName = "Move Time", Min = 0.0, Units = s )
        float32 _moveTime;
        PROPERTY( Category = "Push", DisplayName = "Planar 2D", Tooltip = "Push along X/Y instead of X/Z" )
        bool _bPlanar2D;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "From", Tooltip = "Runtime: cell the move started from", Units = m )
        float3 _from;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "To", Tooltip = "Runtime: cell the move ends at", Units = m )
        float3 _to;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "Steps Left", Tooltip = "Runtime" )
        int32 _stepsLeft;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "Move Steps", Tooltip = "Runtime" )
        int32 _moveSteps;

        FixedStepTimer _clock;
    };
} // namespace sw
