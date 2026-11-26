/**
 * @file SmartObjectWatches.cpp
 * @brief 스마트 오브젝트 슬롯의 자동화 감시 값(`SmartObject.FreeSlots` · `SmartObject.SlotOf`)과 개발 명령(`smartobject.claim` · `smartobject.release`)입니다.
 * @details 명령은 플레이어가 상호작용으로 슬롯을 차지하는 것과 같은 함수(`claimFreeSlot`)를 부른다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectComponent.h"

namespace sw
{
    namespace
    {
        struct SmartObjectWatchesInternal
        {
            /** @brief 감시 값 `SmartObject.FreeSlots` — 대상의 빈 슬롯 수입니다. */
            [[nodiscard]] static bool readFreeSlots( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SmartObjectComponent* pSmart = query.findTargetComponent<SmartObjectComponent>( "smart object", outValue );
                if ( pSmart == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSmart->countFreeSlots() ) );
                return true;
            }

            /** @brief 감시 값 `SmartObject.SlotOf` — 대상에서 `key` 이름의 오브젝트가 차지한 슬롯 번호입니다. 차지하지 않았으면 `none` 입니다. */
            [[nodiscard]] static bool readSlotOf( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SmartObjectComponent* pSmart = query.findTargetComponent<SmartObjectComponent>( "smart object", outValue );
                if ( pSmart == nullptr )
                    return false;
                const AutomationWatchQuery claimantQuery = AutomationWatchQuery::make( query._pManager, query._key, string_view{} );
                const GameObject*          pClaimant     = claimantQuery.findTargetObject( outValue );
                if ( pClaimant == nullptr )
                    return false;
                const optional<int32> slot = pSmart->findSlotOf( *pClaimant );
                if ( slot.has_value() )
                    outValue.setNumber( static_cast<float64>( *slot ) );
                else
                    outValue.setText( "none" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( smartObjectFreeSlots, "SmartObject.FreeSlots", "Free slots of the smart object on target", &SmartObjectWatchesInternal::readFreeSlots, true, false );
    SW_AUTOMATION_QUERY_WATCH( smartObjectSlotOf, "SmartObject.SlotOf", "Slot of the target's smart object held by the object named key; none without one", &SmartObjectWatchesInternal::readSlotOf, true, true );
} // namespace sw
