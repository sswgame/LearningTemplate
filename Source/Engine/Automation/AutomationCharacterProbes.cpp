/**
 * @file AutomationCharacterProbes.cpp
 * @brief 캐릭터 기능(Engine/Character)의 자동화 탐침입니다. 탐침은 자동화 층(Automation)이 캐릭터 층을 읽는 방향이라 여기에 둡니다.
 */
#include "pch.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Character/PairedAnimation/PairedAnimationComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    namespace
    {
        /** @brief 탐침 `PairedAnimation.SessionState` — 대상 오브젝트의 `PairedAnimationComponent` 가 마지막으로 시작한 세션의 상태(0 Idle · 1 Playing · 2 Finished · 3 Aborted). */
        [[nodiscard]] bool readPairedSessionState( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
        {
            const GameObject*               pObject    = query.findTargetObject();
            const PairedAnimationComponent* pComponent = pObject != nullptr ? pObject->getComponent<PairedAnimationComponent>() : nullptr;
            if ( pComponent == nullptr )
            {
                outValue.setText( pObject == nullptr ? query.getTargetProblem() : query.describeMissing( "PairedAnimationComponent" ) );
                return false;
            }
            outValue.setNumber( static_cast<float64>( static_cast<uint32>( pComponent->getSessionState() ) ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_PROBE( pairedSessionState, "PairedAnimation.SessionState",
                               "State of the last session the target's PairedAnimationComponent started (0 idle, 1 playing, 2 finished, 3 aborted)",
                               &readPairedSessionState, true, false );
} // namespace sw
