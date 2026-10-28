/**
 * @file SessionStateProbes.cpp
 * @brief 판 상태(`SessionStateComponent`)의 자동화 탐침입니다. 시계 `Clock.Hour` · `Clock.Day`, 지갑 `Session.Money`, 퀘스트 일지 `Quest.Status` — 활성 씬의 첫 열린 판 상태를 읽습니다.
 * @details 시계를 감는 단계 `AdvanceGameTime` 은 영역을 넘는 단계라 `GameplayAutomationSteps.cpp` 에 있다. `Quest.Status` 가 여기 있는 것은 판 상태(층 2)가 퀘스트(층 1)를 보는 방향이기 때문이다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/Probe/AutomationProbe.h"

#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool readClockHour( const GameObjectManager* pManager, float64& outValue )
        {
            const SessionStateComponent* pState = SessionStateComponent::findFirstOpen( pManager );
            if ( pState == nullptr )
                return false;
            outValue = static_cast<float64>( pState->getClock().getHour() );
            return true;
        }

        [[nodiscard]] bool readClockDay( const GameObjectManager* pManager, float64& outValue )
        {
            const SessionStateComponent* pState = SessionStateComponent::findFirstOpen( pManager );
            if ( pState == nullptr )
                return false;
            outValue = static_cast<float64>( pState->getClock().getDay() );
            return true;
        }

        /** @brief 탐침 `Session.Money` 입니다. 활성 씬의 첫 판 상태 지갑의 기본 화폐 잔액입니다. */
        [[nodiscard]] bool readSessionMoney( const GameObjectManager* pManager, float64& outValue )
        {
            const SessionStateComponent* pState = SessionStateComponent::findFirstOpen( pManager );
            if ( pState == nullptr )
                return false;
            outValue = static_cast<float64>( pState->getWallet().getBalance( Wallet::getDefaultCurrency() ) );
            return true;
        }

        /** @brief 탐침 `Quest.Status` 입니다. 첫 판 상태의 퀘스트 일지에서 퀘스트 id `target` 의 상태(`NotStarted` · `Active` · `Completed` · `Failed`)입니다. */
        [[nodiscard]] bool readQuestStatus( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
        {
            const SessionStateComponent* pState = SessionStateComponent::findFirstOpen( query._pManager );
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
    } // namespace

    SW_AUTOMATION_PROBE( clockHour, "Clock.Hour", "Hour of day (0..24, fractional) of the active scene's game clock (SessionStateComponent)", &readClockHour );
    SW_AUTOMATION_PROBE( clockDay, "Clock.Day", "Day counted from 0 of the active scene's game clock (SessionStateComponent)", &readClockDay );
    SW_AUTOMATION_PROBE( sessionMoney, "Session.Money", "Default-currency balance of the active scene's shared wallet (SessionStateComponent)", &readSessionMoney );
    SW_AUTOMATION_QUERY_PROBE( questStatus, "Quest.Status", "NotStarted, Active, Completed or Failed for quest id target in the active scene's quest log", &readQuestStatus, true,
                               false );
} // namespace sw
