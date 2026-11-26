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

            /** @brief 활성 씬의 이름 붙은 오브젝트에 붙은 스마트 오브젝트입니다. 없으면 nullptr 입니다. */
            [[nodiscard]] static SmartObjectComponent* findSmartObject( const string& objectName )
            {
                GameObject* pOwner = engine::findActiveObject( hashed_string( objectName ) );
                return pOwner != nullptr ? pOwner->getComponent<SmartObjectComponent>() : nullptr;
            }

            /** @brief 명령의 대상(`<claimant> <object>`)이 없다는 답입니다. */
            static string makeMissingReply( const vector<string>& listArgument )
            {
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "no object named '%#' or no smart object on '%#'", listArgument[0], listArgument[1] );
                return string( reply.view() );
            }

            /** @brief `smartobject.claim <claimant> <object> <tag>` — 태그에 맞는 빈 슬롯을 차지합니다. */
            static bool runClaim( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 3 )
                    return false;
                const GameObject*     pClaimant = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SmartObjectComponent* pSmart    = findSmartObject( listArgument[1] );
                if ( pClaimant == nullptr || pSmart == nullptr )
                {
                    outReply = makeMissingReply( listArgument );
                    return false;
                }
                TagContainer required;
                required.addTag( TagID::request( listArgument[2] ) );
                const optional<int32>                    slot = pSmart->claimFreeSlot( *pClaimant, required );
                StringBuilder<constant::buffer::kMax256> reply;
                if ( slot.has_value() )
                    reply.appendFormat( "%# claimed slot %# of %#", listArgument[0], *slot, listArgument[1] );
                else
                    reply.appendFormat( "no free slot of %# has the tag %#", listArgument[1], listArgument[2] ); // 거절도 명령의 답이다
                outReply = string( reply.view() );
                return true;
            }

            /** @brief `smartobject.release <claimant> <object>` — 차지한 슬롯을 비웁니다. */
            static bool runRelease( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                const GameObject*     pClaimant = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SmartObjectComponent* pSmart    = findSmartObject( listArgument[1] );
                if ( pClaimant == nullptr || pSmart == nullptr )
                {
                    outReply = makeMissingReply( listArgument );
                    return false;
                }
                outReply = pSmart->releaseSlot( *pClaimant ) ? "released" : "held no slot";
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( smartObjectFreeSlots, "SmartObject.FreeSlots", "Free slots of the smart object on target", &SmartObjectWatchesInternal::readFreeSlots, true, false );
    SW_AUTOMATION_QUERY_WATCH( smartObjectSlotOf, "SmartObject.SlotOf", "Slot of the target's smart object held by the object named key; none without one", &SmartObjectWatchesInternal::readSlotOf, true, true );
    SW_DEV_COMMAND( SmartObjectClaim, "smartobject.claim", "smartobject.claim <claimant> <object> <tag>", "Claim the first free slot with a tag on a named smart object", &SmartObjectWatchesInternal::runClaim );
    SW_DEV_COMMAND( SmartObjectRelease, "smartobject.release", "smartobject.release <claimant> <object>", "Release the slot an object holds on a named smart object", &SmartObjectWatchesInternal::runRelease );
} // namespace sw
