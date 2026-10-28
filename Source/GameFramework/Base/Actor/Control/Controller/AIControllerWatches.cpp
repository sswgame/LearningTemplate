/**
 * @file AIControllerWatchs.cpp
 * @brief AI 조종자의 자동화 감시 값입니다. `AI.State`(`AIControllerComponent::setStateName`) · `AI.BehaviorTask` · `AI.MoveStatus` · `AI.DistanceToDestination` · `AI.Blackboard`(key).
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Navigation/INavMover.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/Base/Actor/AI/Blackboard.h"
#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 질의 대상 오브젝트의 AI 조종자입니다. 없으면 이유를 적고 nullptr 입니다. */
        const AIControllerComponent* findTargetAI( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject*            pTarget = query.findTargetObject();
            const AIControllerComponent* pAI     = pTarget != nullptr ? pTarget->getComponent<AIControllerComponent>() : nullptr;
            if ( pAI == nullptr )
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "AI controller" ) );
            return pAI;
        }

        /** @brief 감시 값 `AI.State` 입니다. AI 조종자의 판단 상태 이름(`setStateName`)이고, 없으면 `none` 입니다. */
        [[nodiscard]] bool readAIState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const AIControllerComponent* pAI = findTargetAI( query, outValue );
            if ( pAI == nullptr )
                return false;
            outValue.setText( pAI->getStateName().empty() ? string_view{ "none" } : string_view{ pAI->getStateName().c_str() } );
            return true;
        }

        /** @brief 감시 값 `AI.BehaviorTask` 입니다. 행동 트리 실행기가 도는 잎 노드 이름입니다(실행기가 없으면 값이 없다). */
        [[nodiscard]] bool readAIBehaviorTask( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const AIControllerComponent* pAI = findTargetAI( query, outValue );
            if ( pAI == nullptr )
                return false;
            if ( pAI->getBehaviorTreeRunner() == nullptr )
            {
                outValue.setText( "'" + string( query._target ) + "' runs no behavior tree" );
                return false;
            }
            const utf8* pLeaf = pAI->getBehaviorTreeRunner()->getActiveLeafName();
            outValue.setText( pLeaf != nullptr && pLeaf[0] != '\0' ? string_view{ pLeaf } : string_view{ "none" } );
            return true;
        }

        /** @brief 감시 값 `AI.MoveStatus` 입니다. `Idle` · `Moving` · `Arrived` · `Failed` 입니다. */
        [[nodiscard]] bool readAIMoveStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const AIControllerComponent* pAI = findTargetAI( query, outValue );
            if ( pAI == nullptr )
                return false;
            switch ( pAI->getMoveStatus() )
            {
                case NavMoveStatus::Idle:
                {
                    outValue.setText( "Idle" );
                    break;
                }
                case NavMoveStatus::Moving:
                {
                    outValue.setText( "Moving" );
                    break;
                }
                case NavMoveStatus::Arrived:
                {
                    outValue.setText( "Arrived" );
                    break;
                }
                case NavMoveStatus::Failed:
                {
                    outValue.setText( "Failed" );
                    break;
                }
            }
            return true;
        }

        /** @brief 감시 값 `AI.DistanceToDestination` 입니다. 조종하는 폰에서 목적지까지 거리(m)이고, 목적지가 없으면 0 입니다. */
        [[nodiscard]] bool readAIDistanceToDestination( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const AIControllerComponent* pAI = findTargetAI( query, outValue );
            if ( pAI == nullptr )
                return false;
            const Component*      pPawn  = query._pManager != nullptr ? query._pManager->resolveComponent( pAI->getPawn() ) : nullptr;
            const SceneComponent* pScene = pPawn != nullptr ? pPawn->getOwner()->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
            {
                outValue.setText( "'" + string( query._target ) + "' controls no pawn" );
                return false;
            }
            outValue.setNumber( pAI->hasDestination() ? static_cast<float64>( float3::getDistance( pScene->getWorldPosition(), pAI->getDestination() ) ) : 0.0 );
            return true;
        }

        /** @brief 감시 값 `AI.Blackboard` 입니다. 키 `key` 의 수치(실수 · 정수 · 참거짓은 0/1 · 오브젝트는 0/1)이고, 벡터는 길이입니다. 없는 키는 값이 없습니다. */
        [[nodiscard]] bool readAIBlackboard( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const AIControllerComponent* pAI = findTargetAI( query, outValue );
            if ( pAI == nullptr )
                return false;
            const BlackboardValue* pValue = pAI->getBlackboard().findValue( hashed_string( query._key ) );
            if ( pValue == nullptr || pValue->_type == BlackboardValueType::None )
            {
                outValue.setText( "blackboard key '" + string( query._key ) + "' is not set" );
                return false;
            }
            outValue.setNumber( pValue->_type == BlackboardValueType::Vector ? static_cast<float64>( pValue->_vector.getLength() ) : static_cast<float64>( pValue->computeNumeric() ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( aiState, "AI.State", "State name the AI controller on target reports (setStateName), none when unset", &readAIState, true, false );
    SW_AUTOMATION_QUERY_WATCH( aiBehaviorTask, "AI.BehaviorTask", "Active leaf of the behavior tree the AI controller on target runs", &readAIBehaviorTask, true, false );
    SW_AUTOMATION_QUERY_WATCH( aiMoveStatus, "AI.MoveStatus", "Idle, Moving, Arrived or Failed for the AI controller on target", &readAIMoveStatus, true, false );
    SW_AUTOMATION_QUERY_WATCH( aiDistanceToDestination, "AI.DistanceToDestination", "Metres from the controlled pawn to the AI destination (0 without one)",
                               &readAIDistanceToDestination, true, false );
    SW_AUTOMATION_QUERY_WATCH( aiBlackboard, "AI.Blackboard", "Numeric value of blackboard key on the AI controller on target (vector: length)", &readAIBlackboard, true, true );
} // namespace sw
