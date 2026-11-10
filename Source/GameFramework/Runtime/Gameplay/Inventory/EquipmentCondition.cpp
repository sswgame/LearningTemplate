#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/EquipmentCondition.h"

#include "Core/Container/DependencyGraph.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 장착 어휘가 읽는 판정 상태입니다. */
        struct EquipmentConditionState
        {
            const vector<EquipmentSlot>* _pListSlot;
            const ItemCatalog*           _pCatalog;
            const IEquipmentSetLookup*   _pSetLookup;
            hashed_string                _bodyType;
            int32                        _selfSlotIndex;
        };

        struct EquipmentConditionSlot
        {
            using Type                      = const EquipmentConditionState;
            static constexpr uint32 kSlotID = FourCCUtil::make( "EQUP" );
        };

        struct EquipmentConditionInternal
        {
            static bool isSetComplete( const GameplayContext& context, const ConditionNameArg& args )
            {
                const EquipmentConditionState* pState = context.find<EquipmentConditionSlot>();
                return pState != nullptr && pState->_pSetLookup != nullptr && pState->_pSetLookup->computeProgress( args._name, *pState->_pListSlot, pState->_bodyType ).isComplete();
            }

            static bool hasSetPieces( const GameplayContext& context, const EquipmentSetPiecesArgs& args )
            {
                const EquipmentConditionState* pState = context.find<EquipmentConditionSlot>();
                return pState != nullptr && pState->_pSetLookup != nullptr && pState->_pSetLookup->computeProgress( args._set, *pState->_pListSlot, pState->_bodyType )._equippedCount >= args._pieces;
            }

            /** @brief 자기 슬롯과 숨김 슬롯을 빼고, 다른 장비 하나가 태그를 가지고 있으면 참입니다. */
            static bool hasEquippedTag( const GameplayContext& context, const ConditionTagArg& args )
            {
                const EquipmentConditionState* pState = context.find<EquipmentConditionSlot>();
                if ( pState == nullptr )
                    return false;
                const vector<EquipmentSlot>& listSlot = *pState->_pListSlot;
                for ( size_t slotIndex = 0; slotIndex < listSlot.size(); ++slotIndex )
                {
                    const EquipmentSlot& slot      = listSlot[slotIndex];
                    const bool           bSkipSlot = static_cast<int32>( slotIndex ) == pState->_selfSlotIndex || slot._item.isEmpty() || slot._bSuppressed == SW_TRUE;
                    if ( bSkipSlot )
                        continue;
                    const ItemDef* pDef = pState->_pCatalog->find( slot._item._itemID );
                    if ( pDef != nullptr && pDef->hasTagUnder( args._tag ) )
                        return true;
                }
                return false;
            }
            /** @brief 조건이 있는 아이템 @p fromIndex 의 조건을 채울 수 있는, 조건이 있는 아이템 목록입니다. */
            static void collectEdges( const vector<ItemDef>& listItem, size_t fromIndex, const IEquipmentSetLookup* pSetLookup, vector<size_t>& outListTarget )
            {
                outListTarget.clear();
                const ItemDef&        from = listItem[fromIndex];
                vector<hashed_string> listSetItem;
                for ( const EquipmentCondition& condition : from._listEquipCondition )
                {
                    const bool bSetCondition = condition._kind == EquipmentConditionKind::SetComplete || condition._kind == EquipmentConditionKind::SetPieces;
                    listSetItem.clear();
                    if ( bSetCondition && pSetLookup != nullptr )
                        pSetLookup->collectSetItems( condition._setID, listSetItem );
                    for ( size_t toIndex = 0; toIndex < listItem.size(); ++toIndex )
                    {
                        const ItemDef& to = listItem[toIndex];
                        if ( toIndex == fromIndex || to.hasEquipConditions() == false )
                            continue;
                        bool bEdge = false;
                        if ( bSetCondition )
                        {
                            for ( const hashed_string& setItemID : listSetItem )
                            {
                                bEdge = bEdge || setItemID == to._id;
                            }
                        }
                        else if ( condition._kind == EquipmentConditionKind::EquippedTag )
                        {
                            bEdge = to.hasTagUnder( condition._tag );
                        }
                        if ( bEdge )
                            outListTarget.push_back( toIndex );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_CONDITION( setComplete, ConditionNameArg, &EquipmentConditionInternal::isSetComplete );
    SW_CONDITION( setPieces, EquipmentSetPiecesArgs, &EquipmentConditionInternal::hasSetPieces );
    SW_CONDITION( equippedTag, ConditionTagArg, &EquipmentConditionInternal::hasEquippedTag );

    bool EquipmentCondition::isMet( const EquipmentCondition& condition, const vector<EquipmentSlot>& listSlot, int32 selfSlotIndex, const ItemCatalog& catalog,
                                    const IEquipmentSetLookup* pSetLookup, const EquipmentCharacterContext& context )
    {
        const EquipmentConditionState state{ &listSlot, &catalog, pSetLookup, context._bodyType, selfSlotIndex };
        GameplayContext               world;
        world.set<EquipmentConditionSlot>( &state );
        world.set<SubjectTagsSlot>( &context._tags );
        world.set<BodyShapeSlot>( &context._bodyShape );
        world.set<BodyTypeSlot>( &context._bodyType );
        return condition._expr.evaluate( world );
    }

    bool EquipmentCondition::findConditionCycle( const ItemCatalog& catalog, const IEquipmentSetLookup* pSetLookup, vector<hashed_string>& outListCycle )
    {
        // 간선은 조건이 있는 아이템끼리만 생긴다 — 조건이 있는 아이템을 후보로 순환을 찾는다.
        outListCycle.clear();
        const vector<ItemDef>& listItem = catalog.getAll();
        DependencyGraph        graph{ static_cast<uint32>( listItem.size() ) };
        vector<uint32>         listCandidate;
        vector<size_t>         listTarget;
        for ( size_t index = 0; index < listItem.size(); ++index )
        {
            if ( listItem[index].hasEquipConditions() == false )
                continue;
            listCandidate.push_back( static_cast<uint32>( index ) );
            EquipmentConditionInternal::collectEdges( listItem, index, pSetLookup, listTarget );
            for ( const size_t target : listTarget )
            {
                graph.addDependency( static_cast<uint32>( index ), static_cast<uint32>( target ) );
            }
        }
        for ( const uint32 index : graph.findCycle( listCandidate ) )
        {
            outListCycle.push_back( listItem[index]._id );
        }
        return outListCycle.empty() == false;
    }
} // namespace sw
