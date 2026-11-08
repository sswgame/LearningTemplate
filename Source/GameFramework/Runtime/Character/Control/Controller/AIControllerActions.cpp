#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerActions.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/ConditionExpr.h"

#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    namespace
    {
        struct AIControllerActionsInternal
        {
            /** @brief 이보다 짧은 수평 방향은 방향이 없다고 봅니다(제자리에 겹친 두 위치). */
            static constexpr float32 kMinHorizontalLength = 1.0e-4f;

            /** @brief 동작이 쓸 조종자 · 블랙보드입니다. 둘 중 하나라도 없으면(조종자가 아닌 실행기) false 입니다. 중단 호출도 false 입니다(뒷정리할 것이 없다). */
            [[nodiscard]] static bool resolve( ActionContext& context, AIControllerComponent*& outController, const Blackboard*& outBlackboard )
            {
                outController = context._world.find<AIControllerSlot>();
                outBlackboard = context._world.find<BlackboardSlot>();
                return context._bAborted == SW_FALSE && outController != nullptr && outBlackboard != nullptr;
            }

            static ActionResult runMoveTo( ActionContext& context, const AIMoveToParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                const std::optional<float3> destination = pBlackboard->getVector( params._key );
                if ( destination.has_value() == false )
                    return ActionResult::Failed;
                // 쫓는 목표가 조금 움직일 때마다 경로를 다시 잡지 않는다(언리얼 MoveTo 의 관찰 허용 거리).
                const bool bCloseToCurrent = pController->hasDestination() && params._repathDistance > 0.0f &&
                                             float3::getHorizontalDistanceSquared( pController->getDestination(), *destination ) <= params._repathDistance * params._repathDistance;
                if ( bCloseToCurrent == false )
                    pController->moveTo( *destination );
                return ActionResult::Succeeded;
            }

            static ActionResult runMoveAway( ActionContext& context, const AIMoveAwayParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                const std::optional<float3> from = pBlackboard->getVector( params._from );
                const std::optional<float3> away = pBlackboard->getVector( params._away );
                if ( from.has_value() == false || away.has_value() == false )
                    return ActionResult::Failed;
                // 수평 방향만 본다. 두 자리가 겹치면 +Z 를 앞으로 본다(물러날 쪽이 정해지지 않으면 뒤 -Z 로 간다).
                const float3  toAway  = float3{ away->_x - from->_x, 0.0f, away->_z - from->_z };
                const float32 length  = toAway.getLength();
                const float3  forward = length < kMinHorizontalLength ? float3{ 0.0f, 0.0f, 1.0f } : float3{ toAway._x / length, 0.0f, toAway._z / length };
                pController->moveTo( *from - forward * params._distance );
                return ActionResult::Succeeded;
            }

            static ActionResult runArrive( ActionContext& context, const AIArriveParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                const std::optional<float3> spot = pBlackboard->getVector( params._key );
                const std::optional<float3> self = pBlackboard->getVector( params._self );
                if ( spot.has_value() == false || self.has_value() == false )
                    return ActionResult::Failed;
                if ( float3::getHorizontalDistance( *self, *spot ) <= params._tolerance )
                {
                    pController->stopMoving();
                    return ActionResult::Succeeded;
                }
                pController->moveTo( *spot );
                return ActionResult::Failed; // 아직 서지 않았다 — 뒤따르는 잎(도구 쓰기)을 돌리지 않는다
            }

            static ActionResult runStop( ActionContext& context, const VocabularyNoParams& /*params*/ )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                pController->stopMoving();
                return ActionResult::Succeeded;
            }

            static ActionResult runFocus( ActionContext& context, const AIFocusParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                const std::optional<float3> point = pBlackboard->getVector( params._key );
                if ( point.has_value() == false )
                    return ActionResult::Failed;
                pController->setFocus( *point + float3{ 0.0f, params._height, 0.0f } );
                return ActionResult::Succeeded;
            }

            static ActionResult runClearFocus( ActionContext& context, const VocabularyNoParams& /*params*/ )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                pController->clearFocus();
                return ActionResult::Succeeded;
            }

            static ActionResult runPress( ActionContext& context, const AIPressParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                pController->pressButton( params._button );
                return ActionResult::Succeeded;
            }

            static ActionResult runHold( ActionContext& context, const AIHoldParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                pController->holdButton( params._button, params._bHeld );
                return ActionResult::Succeeded;
            }

            static ActionResult runState( ActionContext& context, const AIStateParams& params )
            {
                AIControllerComponent* pController = nullptr;
                const Blackboard*      pBlackboard = nullptr;
                if ( resolve( context, pController, pBlackboard ) == false )
                    return ActionResult::Failed;
                pController->setStateName( params._state );
                return ActionResult::Succeeded;
            }

            /** @brief 조종자에 걸린 목적지가 있으면 참입니다. 조종자 칸이 없으면 거짓입니다. */
            static bool hasDestination( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const AIControllerComponent* pController = context.find<AIControllerSlot>();
                return pController != nullptr && pController->hasDestination();
            }
        };
    } // namespace

    // 행동 트리 데이터(`*.bt.xml`)의 `task` 이름이다. 표는 `AIControllerActions.h` 머리말에 있다.
    SW_GAME_ACTION( AIMoveTo, "ai.moveTo", AIMoveToParams, &AIControllerActionsInternal::runMoveTo );
    SW_GAME_ACTION( AIMoveAway, "ai.moveAway", AIMoveAwayParams, &AIControllerActionsInternal::runMoveAway );
    SW_GAME_ACTION( AIArrive, "ai.arrive", AIArriveParams, &AIControllerActionsInternal::runArrive );
    SW_GAME_ACTION( AIStop, "ai.stop", VocabularyNoParams, &AIControllerActionsInternal::runStop );
    SW_GAME_ACTION( AIFocus, "ai.focus", AIFocusParams, &AIControllerActionsInternal::runFocus );
    SW_GAME_ACTION( AIClearFocus, "ai.clearFocus", VocabularyNoParams, &AIControllerActionsInternal::runClearFocus );
    SW_GAME_ACTION( AIPress, "ai.press", AIPressParams, &AIControllerActionsInternal::runPress );
    SW_GAME_ACTION( AIHold, "ai.hold", AIHoldParams, &AIControllerActionsInternal::runHold );
    SW_GAME_ACTION( AIState, "ai.state", AIStateParams, &AIControllerActionsInternal::runState );
    SW_CONDITION( aiHasDestination, VocabularyNoParams, &AIControllerActionsInternal::hasDestination );
} // namespace sw
