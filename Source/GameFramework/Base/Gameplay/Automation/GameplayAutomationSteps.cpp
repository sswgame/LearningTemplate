/**
 * @file GameplayAutomationSteps.cpp
 * @brief 자동화 시나리오의 게임 로직 단계와 탐침입니다. 등록은 이 .cpp 의 정적 등록자가 합니다(Shipping 은 GameFramework 를 통째로 링크한다 — `sw_linkWholeArchive`).
 * @details - 게임 시간: `AdvanceGameTime`(minutes · hours · toHour) — 활성 씬의 판 상태(`SessionStateComponent`) 시계를 감는다. 탐침 `Clock.Hour` · `Clock.Day`.
 *          - 판 상태: `Session.Money`(공유 지갑의 기본 화폐) · `Quest.Status`(퀘스트 id target).
 *          - 오브젝트 대상(`target` · `rider` · `vehicle`)은 `AutomationTarget` 규칙으로 고른다 — 같은 이름이 여럿이면 `Name[N]`.
 *          - NPC 일정: `Schedule.Activity` · `Schedule.Phase` · `Schedule.DistanceToPlace` · `Schedule.DistanceToTarget` · `Schedule.MinuteOfDay` — 살아 있는 일정 시스템에서 NPC id 로 찾는다.
 *          - AI: `AI.State`(`AIControllerComponent::setStateName`) · `AI.BehaviorTask` · `AI.MoveStatus` · `AI.DistanceToDestination` · `AI.Blackboard`(key).
 *          - 빙의 · 탑승: 단계 `Mount` · `Dismount`(`MountUtil`), 탐침 `Pawn.Controller` · `Pawn.Seat`. 빙의 단계 `Possess` 는 조종 층(`ControlAutomationSteps`)에 있다.
 *          게임은 탐침을 따로 등록하지 않고 이 기반(판 상태 · 일정 시스템 · AI 조종자 · 폰)을 쓰기만 하면 된다. 이 폴더는 기반의 맨 위 층이라 모든 기반을 볼 수 있다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Navigation/INavMover.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/Base/Actor/AI/Blackboard.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"

namespace sw
{
    namespace
    {
        struct GameplayAutomationStepsInternal
        {
            static bool checkAttributeNames( const AutomationStep& step, std::initializer_list<string_view> listAllowed, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                return true;
            }

            /** @brief 속성 @p name 을 음이 아닌 수로 읽습니다. 없으면 true 이고 @p outbPresent 가 false 입니다. */
            [[nodiscard]] static bool readNonNegative( const AutomationStep& step, string_view name, float32& outValue, bool& outbPresent, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                outbPresent          = pValue != nullptr;
                outValue             = 0.0f;
                if ( pValue == nullptr )
                    return true;
                if ( StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), outValue ) && outValue >= 0.0f )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a non-negative number, got '" + *pValue + "'";
                return false;
            }

            /** @brief 활성 씬에서 열린 판 상태를 오브젝트 순서로 모읍니다(판 상태는 컴포넌트 레지스트리에 등록하지 않는다). */
            static void collectSessionStates( const GameObjectManager* pManager, vector<SessionStateComponent*>& outListState )
            {
                outListState.clear();
                if ( pManager == nullptr )
                    return;
                pManager->forEachGameObject( [&outListState]( GameObject* pObject )
                {
                    SessionStateComponent* pState = pObject->getComponent<SessionStateComponent>();
                    if ( pState != nullptr && pState->isInitialized() )
                        outListState.push_back( pState );
                } );
            }

            /** @brief `AdvanceGameTime` 은 `minutes` · `hours` · `toHour`(다음 그 시각, 0..24) 중 정확히 하나를 받습니다. */
            static bool validateAdvanceGameTime( const AutomationStep& step, string& outError )
            {
                float32    value    = 0.0f;
                bool       bMinutes = false;
                bool       bHours   = false;
                bool       bToHour  = false;
                const bool bRead    = checkAttributeNames( step, { "minutes", "hours", "toHour" }, outError ) && readNonNegative( step, "minutes", value, bMinutes, outError ) &&
                                   readNonNegative( step, "hours", value, bHours, outError ) && readNonNegative( step, "toHour", value, bToHour, outError );
                if ( bRead == false )
                    return false;
                const uint32 count = ( bMinutes ? 1u : 0u ) + ( bHours ? 1u : 0u ) + ( bToHour ? 1u : 0u );
                if ( count == 1 )
                    return true;
                outError = step.describe() + ": needs exactly one of minutes · hours · toHour";
                return false;
            }

            /**
             * @brief 활성 씬의 판 상태마다 시계를 감는다. 지나간 시 · 날 경계는 알림으로 남아 다음 틱의 디렉터 · 일정이 받는다(잠 · 빨리 감기와 같은 길).
             * @details 씬 틱 뒤에 돌므로 같은 프레임의 틱은 감기 전 시각을 봤다. 탐침은 감은 시각을 바로 보지만 몸(일정을 따르는 오브젝트)은 다음 틱에 옮겨진다.
             */
            static bool runAdvanceGameTime( AutomationRunner& runner, const AutomationStep& step )
            {
                GameObjectManager* pManager = runner.findActiveObjectManager();
                uint32             count    = 0;
                string             error;
                float32            minutes  = 0.0f;
                float32            hours    = 0.0f;
                float32            toHour   = 0.0f;
                bool               bMinutes = false;
                bool               bHours   = false;
                bool               bToHour  = false;
                (void)readNonNegative( step, "minutes", minutes, bMinutes, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
                (void)readNonNegative( step, "hours", hours, bHours, error );       // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
                (void)readNonNegative( step, "toHour", toHour, bToHour, error );    // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
                vector<SessionStateComponent*> listState;
                collectSessionStates( pManager, listState );
                for ( SessionStateComponent* pState : listState )
                {
                    WorldClock& clock = pState->getClock();
                    if ( bToHour )
                        clock.advanceToHour( toHour );
                    else
                        clock.advanceMinutes( bHours ? hours * static_cast<float32>( constant::kSecondsPerHour / constant::kSecondsPerMinute ) : minutes );
                    ++count;
                }
                if ( count == 0 )
                    runner.recordFailure( step, "the active scene has no open SessionStateComponent (game clock)" );
                return true;
            }

            /** @brief 활성 씬의 첫 열린 판 상태입니다. 없으면 nullptr 입니다. */
            static const SessionStateComponent* findSessionState( const GameObjectManager* pManager )
            {
                vector<SessionStateComponent*> listState;
                collectSessionStates( pManager, listState );
                return listState.empty() ? nullptr : listState.front();
            }

            [[nodiscard]] static bool readClockHour( const GameObjectManager* pManager, float64& outValue )
            {
                const SessionStateComponent* pState = findSessionState( pManager );
                if ( pState == nullptr )
                    return false;
                outValue = static_cast<float64>( pState->getClock().getHour() );
                return true;
            }

            [[nodiscard]] static bool readClockDay( const GameObjectManager* pManager, float64& outValue )
            {
                const SessionStateComponent* pState = findSessionState( pManager );
                if ( pState == nullptr )
                    return false;
                outValue = static_cast<float64>( pState->getClock().getDay() );
                return true;
            }

            /** @brief 탐침 `Session.Money` 입니다. 활성 씬의 첫 판 상태 지갑의 기본 화폐 잔액입니다. */
            [[nodiscard]] static bool readSessionMoney( const GameObjectManager* pManager, float64& outValue )
            {
                const SessionStateComponent* pState = findSessionState( pManager );
                if ( pState == nullptr )
                    return false;
                outValue = static_cast<float64>( pState->getWallet().getBalance( Wallet::getDefaultCurrency() ) );
                return true;
            }

            /** @brief 탐침 `Quest.Status` 입니다. 첫 판 상태의 퀘스트 일지에서 퀘스트 id `target` 의 상태(`NotStarted` · `Active` · `Completed` · `Failed`)입니다. */
            [[nodiscard]] static bool readQuestStatus( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const SessionStateComponent* pState = findSessionState( query._pManager );
                if ( pState == nullptr )
                {
                    outValue.setText( "the active scene has no open SessionStateComponent (quest log)" );
                    return false;
                }
                switch ( pState->getQuestLog().getStatus( hashed_string( query._target ) ) )
                {
                    case QuestStatus::NotStarted:
                    {
                        outValue.setText( "NotStarted" );
                        break;
                    }
                    case QuestStatus::Active:
                    {
                        outValue.setText( "Active" );
                        break;
                    }
                    case QuestStatus::Completed:
                    {
                        outValue.setText( "Completed" );
                        break;
                    }
                    case QuestStatus::Failed:
                    {
                        outValue.setText( "Failed" );
                        break;
                    }
                }
                return true;
            }

            /** @brief 살아 있는 일정 시스템에서 NPC id @p npcID 를 찾습니다. 없으면 이유를 적고 false 입니다. */
            [[nodiscard]] static bool findNpc( string_view npcID, const ScheduleSystem*& outpSystem, int32& outNpcIndex, AutomationProbeValue& outValue )
            {
                vector<const ScheduleSystem*> listSystem;
                ScheduleSystem::collectLiveSystems( listSystem );
                const hashed_string id( npcID );
                for ( const ScheduleSystem* pSystem : listSystem )
                {
                    const int32 npcIndex = pSystem->findNpcIndex( id );
                    if ( npcIndex >= 0 )
                    {
                        outpSystem  = pSystem;
                        outNpcIndex = npcIndex;
                        return true;
                    }
                }
                outValue.setText( listSystem.empty() ? string( "no schedule system is running" ) : "no schedule NPC '" + string( npcID ) + "'" );
                return false;
            }

            /** @brief 탐침 `Schedule.Activity` 입니다. NPC 가 지금(가는 중이면 가서 할) 블록의 활동 이름이고, 없으면 `none` 입니다. */
            [[nodiscard]] static bool readScheduleActivity( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const ScheduleSystem* pSystem  = nullptr;
                int32                 npcIndex = -1;
                if ( findNpc( query._target, pSystem, npcIndex, outValue ) == false )
                    return false;
                const ScheduleNpcView view = pSystem->getNpcView( npcIndex );
                outValue.setText( view._activity.empty() ? string_view{ "none" } : string_view{ view._activity.c_str() } );
                return true;
            }

            /** @brief 탐침 `Schedule.Phase` 입니다. `Traveling` · `Performing` · `Interrupted` 입니다. */
            [[nodiscard]] static bool readSchedulePhase( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const ScheduleSystem* pSystem  = nullptr;
                int32                 npcIndex = -1;
                if ( findNpc( query._target, pSystem, npcIndex, outValue ) == false )
                    return false;
                switch ( pSystem->getNpcView( npcIndex )._phase )
                {
                    case ScheduleNpcPhase::Traveling:
                    {
                        outValue.setText( "Traveling" );
                        break;
                    }
                    case ScheduleNpcPhase::Performing:
                    {
                        outValue.setText( "Performing" );
                        break;
                    }
                    case ScheduleNpcPhase::Interrupted:
                    {
                        outValue.setText( "Interrupted" );
                        break;
                    }
                }
                return true;
            }

            /** @brief 탐침 `Schedule.DistanceToPlace` 입니다. NPC 의 일정상 위치에서 카탈로그 장소 `key` 까지의 지면 거리(m)입니다. */
            [[nodiscard]] static bool readScheduleDistanceToPlace( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const ScheduleSystem* pSystem  = nullptr;
                int32                 npcIndex = -1;
                if ( findNpc( query._target, pSystem, npcIndex, outValue ) == false )
                    return false;
                const SchedulePlaceDef* pPlace = pSystem->getCatalog() != nullptr ? pSystem->getCatalog()->findPlace( hashed_string( query._key ) ) : nullptr;
                if ( pPlace == nullptr )
                {
                    outValue.setText( "no schedule place '" + string( query._key ) + "'" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( float3::getHorizontalDistance( pSystem->getNpcView( npcIndex )._location._position, pPlace->_position ) ) );
                return true;
            }

            /** @brief 탐침 `Schedule.DistanceToTarget` 입니다. NPC 의 일정상 위치에서 지금 블록의 목표 위치까지 지면 거리(m)이며, 닿았으면 0 입니다. */
            [[nodiscard]] static bool readScheduleDistanceToTarget( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const ScheduleSystem* pSystem  = nullptr;
                int32                 npcIndex = -1;
                if ( findNpc( query._target, pSystem, npcIndex, outValue ) == false )
                    return false;
                const ScheduleNpcView view = pSystem->getNpcView( npcIndex );
                outValue.setNumber( static_cast<float64>( float3::getHorizontalDistance( view._location._position, view._target._position ) ) );
                return true;
            }

            /** @brief 탐침 `Schedule.MinuteOfDay` 입니다. 첫 일정 시스템의 그날 분(0..1439)입니다. */
            [[nodiscard]] static bool readScheduleMinuteOfDay( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                vector<const ScheduleSystem*> listSystem;
                ScheduleSystem::collectLiveSystems( listSystem );
                if ( listSystem.empty() )
                    return false;
                outValue = static_cast<float64>( listSystem.front()->getMinute() % kScheduleMinutesPerDay );
                return true;
            }

            static const string* requireAttribute( const AutomationStep& step, string_view name, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    outError = step.describe() + ": needs " + string( name ) + "=\"…\"";
                return pValue;
            }

            /** @brief 대상 고르기 규칙(`AutomationTarget` — `Name` · `Name[N]`)으로 찾은 오브젝트입니다. 없으면 실패를 적고 nullptr 입니다. */
            static GameObject* findObject( AutomationRunner& runner, const AutomationStep& step, const string& objectName )
            {
                AutomationTargetLookup lookup = AutomationTargetLookup::NotGiven;
                string                 problem;
                GameObject*            pObject = AutomationTarget::find( runner.findActiveObjectManager(), objectName, lookup, problem );
                if ( pObject == nullptr )
                    runner.recordFailure( step, problem );
                return pObject;
            }

            static bool validateMountStep( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "rider", "vehicle" }, outError ) && requireAttribute( step, "rider", outError ) != nullptr &&
                       requireAttribute( step, "vehicle", outError ) != nullptr;
            }

            static bool validateDismountStep( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "rider" }, outError ) && requireAttribute( step, "rider", outError ) != nullptr;
            }

            static const utf8* toText( MountResult result )
            {
                switch ( result )
                {
                    case MountResult::Mounted:
                        return "Mounted";
                    case MountResult::SeatTaken:
                        return "SeatTaken";
                    case MountResult::TooFar:
                        return "TooFar";
                    case MountResult::NoSocket:
                        return "NoSocket";
                    case MountResult::NotPossessed:
                        return "NotPossessed";
                    case MountResult::NoVehiclePawn:
                        return "NoVehiclePawn";
                }
                return "Unknown";
            }

            /** @brief 이름의 오브젝트의 폰입니다. 없으면 실패를 적고 nullptr 입니다. */
            static PawnComponent* findPawn( AutomationRunner& runner, const AutomationStep& step, const string& objectName )
            {
                GameObject*    pObject = findObject( runner, step, objectName );
                PawnComponent* pPawn   = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
                if ( pObject != nullptr && pPawn == nullptr )
                    runner.recordFailure( step, "object '" + objectName + "' has no pawn" );
                return pPawn;
            }

            /** @brief 탑승자 폰을 탈것 오브젝트의 빈 좌석(운전석 먼저)에 태운다. 탈 수 없으면 그 까닭(`TooFar` 등)을 실패로 적는다. */
            static bool runMountStep( AutomationRunner& runner, const AutomationStep& step )
            {
                const string&  riderName   = *step.findAttribute( "rider" );
                const string&  vehicleName = *step.findAttribute( "vehicle" );
                PawnComponent* pRider      = findPawn( runner, step, riderName );
                GameObject*    pVehicle    = pRider != nullptr ? findObject( runner, step, vehicleName ) : nullptr;
                if ( pVehicle == nullptr )
                    return true;
                VehicleSeatComponent* pSeat = nullptr;
                pVehicle->forEachComponentOfType<VehicleSeatComponent>( [&pSeat]( VehicleSeatComponent* pCandidate )
                {
                    const bool bFree = pCandidate->getOccupant().isValid() == false;
                    if ( bFree && ( pSeat == nullptr || pCandidate->isDriverSeat() ) )
                        pSeat = pCandidate;
                } );
                if ( pSeat == nullptr )
                {
                    runner.recordFailure( step, "vehicle '" + vehicleName + "' has no free seat" );
                    return true;
                }
                const MountResult result = MountUtil::mount( *pRider, *pSeat );
                if ( result != MountResult::Mounted )
                    runner.recordFailure( step, "'" + riderName + "' could not mount '" + vehicleName + "': " + toText( result ) );
                return true;
            }

            static bool runDismountStep( AutomationRunner& runner, const AutomationStep& step )
            {
                const string&  riderName = *step.findAttribute( "rider" );
                PawnComponent* pRider    = findPawn( runner, step, riderName );
                if ( pRider != nullptr && MountUtil::dismount( *pRider, false ) == false )
                    runner.recordFailure( step, "'" + riderName + "' is not seated" );
                return true;
            }

            /** @brief 질의 대상 오브젝트의 컴포넌트입니다. 없으면 이유를 적고 nullptr 입니다. */
            template <typename TComponent>
            static const TComponent* findTargetComponent( const AutomationProbeQuery& query, const utf8* pWhat, AutomationProbeValue& outValue )
            {
                const GameObject* pTarget    = query.findTargetObject();
                const TComponent* pComponent = pTarget != nullptr ? pTarget->getComponent<TComponent>() : nullptr;
                if ( pComponent == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( pWhat ) );
                return pComponent;
            }

            /** @brief 탐침 `AI.State` 입니다. AI 조종자의 판단 상태 이름(`setStateName`)이고, 없으면 `none` 입니다. */
            [[nodiscard]] static bool readAIState( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const AIControllerComponent* pAI = findTargetComponent<AIControllerComponent>( query, "AI controller", outValue );
                if ( pAI == nullptr )
                    return false;
                outValue.setText( pAI->getStateName().empty() ? string_view{ "none" } : string_view{ pAI->getStateName().c_str() } );
                return true;
            }

            /** @brief 탐침 `AI.BehaviorTask` 입니다. 행동 트리 실행기가 도는 잎 노드 이름입니다(실행기가 없으면 값이 없다). */
            [[nodiscard]] static bool readAIBehaviorTask( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const AIControllerComponent* pAI = findTargetComponent<AIControllerComponent>( query, "AI controller", outValue );
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

            /** @brief 탐침 `AI.MoveStatus` 입니다. `Idle` · `Moving` · `Arrived` · `Failed` 입니다. */
            [[nodiscard]] static bool readAIMoveStatus( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const AIControllerComponent* pAI = findTargetComponent<AIControllerComponent>( query, "AI controller", outValue );
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

            /** @brief 탐침 `AI.DistanceToDestination` 입니다. 조종하는 폰에서 목적지까지 거리(m)이고, 목적지가 없으면 0 입니다. */
            [[nodiscard]] static bool readAIDistanceToDestination( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const AIControllerComponent* pAI = findTargetComponent<AIControllerComponent>( query, "AI controller", outValue );
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

            /** @brief 탐침 `AI.Blackboard` 입니다. 키 `key` 의 수치(실수 · 정수 · 참거짓은 0/1 · 오브젝트는 0/1)이고, 벡터는 길이입니다. 없는 키는 값이 없습니다. */
            [[nodiscard]] static bool readAIBlackboard( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const AIControllerComponent* pAI = findTargetComponent<AIControllerComponent>( query, "AI controller", outValue );
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

            /** @brief 탐침 `Pawn.Controller` 입니다. 폰을 쥔 조종자 오브젝트의 이름이고, 아무도 쥐지 않았으면 `none` 입니다. */
            [[nodiscard]] static bool readPawnController( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const PawnComponent* pPawn = findTargetComponent<PawnComponent>( query, "pawn", outValue );
                if ( pPawn == nullptr )
                    return false;
                const Component* pController = query._pManager != nullptr ? query._pManager->resolveComponent( pPawn->getController() ) : nullptr;
                outValue.setText( pController != nullptr ? string_view{ pController->getOwner()->getName().c_str() } : string_view{ "none" } );
                return true;
            }

            /** @brief 탐침 `Pawn.Seat` 입니다. 폰이 앉은 좌석의 탈것 오브젝트 이름이고, 앉지 않았으면 `none` 입니다. */
            [[nodiscard]] static bool readPawnSeat( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const PawnComponent* pPawn = findTargetComponent<PawnComponent>( query, "pawn", outValue );
                if ( pPawn == nullptr )
                    return false;
                const VehicleSeatComponent* pSeat = MountUtil::findSeatOf( *pPawn );
                outValue.setText( pSeat != nullptr ? string_view{ pSeat->getOwner()->getName().c_str() } : string_view{ "none" } );
                return true;
            }
        };
    } // namespace

    // 시간 감기는 씬 틱 뒤에 돈다 — 감은 시각의 알림은 다음 틱의 디렉터 · 일정이 받는다.
    SW_AUTOMATION_STEP( worldAdvanceGameTime, "AdvanceGameTime", &GameplayAutomationStepsInternal::runAdvanceGameTime, &GameplayAutomationStepsInternal::validateAdvanceGameTime, false );

    SW_AUTOMATION_PROBE( clockHour, "Clock.Hour", "Hour of day (0..24, fractional) of the active scene's game clock (SessionStateComponent)",
                         &GameplayAutomationStepsInternal::readClockHour );
    SW_AUTOMATION_PROBE( clockDay, "Clock.Day", "Day counted from 0 of the active scene's game clock (SessionStateComponent)", &GameplayAutomationStepsInternal::readClockDay );
    SW_AUTOMATION_PROBE( sessionMoney, "Session.Money", "Default-currency balance of the active scene's shared wallet (SessionStateComponent)",
                         &GameplayAutomationStepsInternal::readSessionMoney );
    SW_AUTOMATION_QUERY_PROBE( questStatus, "Quest.Status", "NotStarted, Active, Completed or Failed for quest id target in the active scene's quest log",
                               &GameplayAutomationStepsInternal::readQuestStatus, true, false );
    SW_AUTOMATION_PROBE( scheduleMinuteOfDay, "Schedule.MinuteOfDay", "Minute of day (0..1439) of the first live schedule system",
                         &GameplayAutomationStepsInternal::readScheduleMinuteOfDay );
    SW_AUTOMATION_QUERY_PROBE( scheduleActivity, "Schedule.Activity", "Activity of the schedule NPC with id target (none between blocks)",
                               &GameplayAutomationStepsInternal::readScheduleActivity, true, false );
    SW_AUTOMATION_QUERY_PROBE( schedulePhase, "Schedule.Phase", "Traveling, Performing or Interrupted for the schedule NPC with id target",
                               &GameplayAutomationStepsInternal::readSchedulePhase, true, false );
    SW_AUTOMATION_QUERY_PROBE( scheduleDistanceToPlace, "Schedule.DistanceToPlace", "Ground distance (m) from the schedule NPC target to the catalog place key",
                               &GameplayAutomationStepsInternal::readScheduleDistanceToPlace, true, true );
    SW_AUTOMATION_QUERY_PROBE( scheduleDistanceToTarget, "Schedule.DistanceToTarget", "Ground distance (m) from the schedule NPC target to its current block's spot (0 once there)",
                               &GameplayAutomationStepsInternal::readScheduleDistanceToTarget, true, false );

    // 탈것 단계는 씬 틱 뒤(틱 밖)에 돈다 — `MountUtil` 은 틱 안에서 부르지 않는다.
    SW_AUTOMATION_STEP( controlMount, "Mount", &GameplayAutomationStepsInternal::runMountStep, &GameplayAutomationStepsInternal::validateMountStep, false );
    SW_AUTOMATION_STEP( controlDismount, "Dismount", &GameplayAutomationStepsInternal::runDismountStep, &GameplayAutomationStepsInternal::validateDismountStep, false );

    SW_AUTOMATION_QUERY_PROBE( aiState, "AI.State", "State name the AI controller on target reports (setStateName), none when unset",
                               &GameplayAutomationStepsInternal::readAIState, true, false );
    SW_AUTOMATION_QUERY_PROBE( aiBehaviorTask, "AI.BehaviorTask", "Active leaf of the behavior tree the AI controller on target runs",
                               &GameplayAutomationStepsInternal::readAIBehaviorTask, true, false );
    SW_AUTOMATION_QUERY_PROBE( aiMoveStatus, "AI.MoveStatus", "Idle, Moving, Arrived or Failed for the AI controller on target", &GameplayAutomationStepsInternal::readAIMoveStatus,
                               true, false );
    SW_AUTOMATION_QUERY_PROBE( aiDistanceToDestination, "AI.DistanceToDestination", "Metres from the controlled pawn to the AI destination (0 without one)",
                               &GameplayAutomationStepsInternal::readAIDistanceToDestination, true, false );
    SW_AUTOMATION_QUERY_PROBE( aiBlackboard, "AI.Blackboard", "Numeric value of blackboard key on the AI controller on target (vector: length)",
                               &GameplayAutomationStepsInternal::readAIBlackboard, true, true );
    SW_AUTOMATION_QUERY_PROBE( pawnController, "Pawn.Controller", "Name of the object whose controller possesses the pawn on target, none when free",
                               &GameplayAutomationStepsInternal::readPawnController, true, false );
    SW_AUTOMATION_QUERY_PROBE( pawnSeat, "Pawn.Seat", "Name of the vehicle the pawn on target sits in, none when on foot", &GameplayAutomationStepsInternal::readPawnSeat, true,
                               false );
} // namespace sw
