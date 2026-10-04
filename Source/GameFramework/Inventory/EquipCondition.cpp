#include "pch.h"

#include "GameFramework/Inventory/EquipCondition.h"

#include "Core/String/StringUtil.h"

#include "GameFramework/Inventory/Equipment.h"
#include "GameFramework/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct EquipConditionInternal
        {
            /** @brief 깊이 우선 탐색 상태입니다 — 0 안 봄, 1 보는 중(스택), 2 끝남. */
            static constexpr uint8 kVisitNone   = 0;
            static constexpr uint8 kVisitActive = 1;
            static constexpr uint8 kVisitDone   = 2;

            /** @brief 조건 있는 아이템 @p fromIndex 의 조건을 채울 수 있는 조건 있는 아이템들입니다. */
            static void collectEdges( const vector<ItemDef>& listItem, size_t fromIndex, const IEquipSetLookup* pSetLookup, vector<size_t>& outListTarget )
            {
                outListTarget.clear();
                const ItemDef&        from = listItem[fromIndex];
                vector<hashed_string> listSetItem;
                for ( const EquipCondition& condition : from._listEquipCondition )
                {
                    const bool bSetCondition = condition._kind == EquipConditionKind::SetComplete || condition._kind == EquipConditionKind::SetPieces;
                    listSetItem.clear();
                    if ( bSetCondition && pSetLookup != nullptr )
                        pSetLookup->collectSetItems( condition._setId, listSetItem );
                    for ( size_t toIndex = 0; toIndex < listItem.size(); ++toIndex )
                    {
                        const ItemDef& to = listItem[toIndex];
                        if ( toIndex == fromIndex || to.hasEquipConditions() == false )
                            continue;
                        bool bEdge = false;
                        if ( bSetCondition )
                        {
                            for ( const hashed_string& setItemId : listSetItem )
                            {
                                bEdge = bEdge || setItemId == to._id;
                            }
                        }
                        else if ( condition._kind == EquipConditionKind::EquippedTag )
                        {
                            bEdge = to.hasTagUnder( condition._tag );
                        }
                        if ( bEdge )
                            outListTarget.push_back( toIndex );
                    }
                }
            }

            static bool visit( const vector<ItemDef>& listItem, size_t index, const IEquipSetLookup* pSetLookup, vector<uint8>& inoutState, vector<size_t>& inoutStack,
                               vector<hashed_string>& outListCycle )
            {
                inoutState[index] = kVisitActive;
                inoutStack.push_back( index );
                vector<size_t> listTarget;
                collectEdges( listItem, index, pSetLookup, listTarget );
                for ( const size_t target : listTarget )
                {
                    if ( inoutState[target] == kVisitActive )
                    {
                        // 스택에서 target 부터 지금까지가 사슬이다.
                        bool bInCycle = false;
                        for ( const size_t stackIndex : inoutStack )
                        {
                            bInCycle = bInCycle || stackIndex == target;
                            if ( bInCycle )
                                outListCycle.push_back( listItem[stackIndex]._id );
                        }
                        outListCycle.push_back( listItem[target]._id );
                        return true;
                    }
                    if ( inoutState[target] == kVisitNone && visit( listItem, target, pSetLookup, inoutState, inoutStack, outListCycle ) )
                        return true;
                }
                inoutStack.pop_back();
                inoutState[index] = kVisitDone;
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( EquipBreakPolicy policy )
    {
        switch ( policy )
        {
            case EquipBreakPolicy::UnequipTogether:
                return "UnequipTogether";
            case EquipBreakPolicy::KeepHidden:
                return "KeepHidden";
            case EquipBreakPolicy::RefuseUnequip:
                return "RefuseUnequip";
        }
        return "Unknown";
    }

    bool parseEquipBreakPolicy( string_view text, EquipBreakPolicy& outPolicy )
    {
        constexpr EquipBreakPolicy kArrPolicy[] = { EquipBreakPolicy::UnequipTogether, EquipBreakPolicy::KeepHidden, EquipBreakPolicy::RefuseUnequip };
        for ( const EquipBreakPolicy policy : kArrPolicy )
        {
            if ( StringUtil::equals( text, string_view( toString( policy ) ), true ) )
            {
                outPolicy = policy;
                return true;
            }
        }
        return false;
    }

    bool EquipConditionUtil::isMet( const EquipCondition& condition, const vector<EquipSlot>& listSlot, int32 selfSlotIndex, const ItemCatalog& catalog,
                                    const IEquipSetLookup* pSetLookup, const EquipCharacterContext& context )
    {
        switch ( condition._kind )
        {
            case EquipConditionKind::SetComplete:
            {
                return pSetLookup != nullptr && pSetLookup->computeProgress( condition._setId, listSlot, context._bodyType ).isComplete();
            }
            case EquipConditionKind::SetPieces:
            {
                return pSetLookup != nullptr && pSetLookup->computeProgress( condition._setId, listSlot, context._bodyType )._equippedCount >= condition._pieceCount;
            }
            case EquipConditionKind::EquippedTag:
            {
                for ( size_t slotIndex = 0; slotIndex < listSlot.size(); ++slotIndex )
                {
                    const EquipSlot& slot = listSlot[slotIndex];
                    if ( static_cast<int32>( slotIndex ) == selfSlotIndex || slot._item.isEmpty() || slot._bSuppressed == SW_TRUE )
                        continue;
                    const ItemDef* pDef = catalog.findItem( slot._item._itemId );
                    if ( pDef != nullptr && pDef->hasTagUnder( condition._tag ) )
                        return true;
                }
                return false;
            }
            case EquipConditionKind::CharacterTag:
            {
                return context._tags.hasTag( condition._tag );
            }
            case EquipConditionKind::BodyShape:
            {
                for ( const hashed_string& shape : condition._listBodyShape )
                {
                    if ( shape == context._bodyShape )
                        return true;
                }
                return false;
            }
        }
        return false;
    }

    bool EquipConditionUtil::findConditionCycle( const ItemCatalog& catalog, const IEquipSetLookup* pSetLookup, vector<hashed_string>& outListCycle )
    {
        outListCycle.clear();
        const vector<ItemDef>& listItem = catalog.getItems();
        vector<uint8>          listState( listItem.size(), EquipConditionInternal::kVisitNone );
        vector<size_t>         listStack;
        for ( size_t index = 0; index < listItem.size(); ++index )
        {
            if ( listItem[index].hasEquipConditions() == false || listState[index] != EquipConditionInternal::kVisitNone )
                continue;
            if ( EquipConditionInternal::visit( listItem, index, pSetLookup, listState, listStack, outListCycle ) )
                return true;
        }
        return false;
    }
} // namespace sw
