/**
 * @file AppearanceWatches.cpp
 * @brief 외형의 자동화 감시 값(`Appearance.Preset` · `Appearance.MergedSections` · `Appearance.SlotItem`)입니다.
 * @details 시나리오만 읽는 감시 값이라 컴포넌트 파일이 아니라 이 파일에 둔다(`VehicleWatches` · `PawnWatches` 와 같은 자리). 컴포넌트의 공개 읽기만 쓴다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw
{
    namespace
    {
        struct AppearanceWatchesInternal
        {
            /** @brief 감시 값 대상 오브젝트의 외형 컴포넌트입니다. 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const CharacterAppearanceComponent* findWatchTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pObject = query.findTargetObject();
                if ( pObject == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return nullptr;
                }
                const CharacterAppearanceComponent* pAppearance = pObject->getComponent<CharacterAppearanceComponent>();
                if ( pAppearance == nullptr )
                    outValue.setText( string( "'" ) + pObject->getName().c_str() + "' has no CharacterAppearanceComponent" );
                return pAppearance;
            }

            /** @brief 조립한 외형의 프리셋 id 입니다. 아직 조립하지 않았으면 빈 글입니다. */
            [[nodiscard]] static bool readPreset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setText( pAppearance->getResolvedAppearance()._presetID.c_str() );
                return true;
            }

            /** @brief 합친 메시의 구간 수입니다(몸 구간 + 합친 부품). 합치지 않았으면 0 입니다. */
            [[nodiscard]] static bool readMergedSections( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pAppearance->getMergedSectionCount() ) );
                return true;
            }

            /** @brief 조립한 외형에서 슬롯(@p query 의 key)에 입은 아이템 id 입니다. 빈 슬롯이면 `(empty)` 입니다. */
            [[nodiscard]] static bool readSlotItem( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findWatchTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const hashed_string slot( query._key );
                for ( const ResolvedPart& part : pAppearance->getResolvedAppearance()._listPart )
                {
                    if ( part._owner == slot && part._itemID.empty() == false )
                    {
                        outValue.setText( part._itemID.c_str() );
                        return true;
                    }
                }
                outValue.setText( "(empty)" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( appearancePreset, "Appearance.Preset", "Preset id the object's CharacterAppearanceComponent last assembled",
                               &AppearanceWatchesInternal::readPreset, true, false );
    SW_AUTOMATION_QUERY_WATCH( appearanceMergedSections, "Appearance.MergedSections",
                               "Sections of the merged body mesh (the body's own plus one per merged part); 0 when the parts are not merged",
                               &AppearanceWatchesInternal::readMergedSections, true, false );
    SW_AUTOMATION_QUERY_WATCH( appearanceSlotItem, "Appearance.SlotItem", "Item the assembled appearance wears in the slot named by key ('(empty)' when the slot is empty)",
                               &AppearanceWatchesInternal::readSlotItem, true, true );
} // namespace sw
