#include "pch.h"

#include "Editor/Panels/InputMap/Model/InputMapConflict.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Input/Map/InputMap.h"

namespace sw::editor
{
    namespace
    {
        struct InputMapConflictsInternal
        {
            /** @brief 키가 비어 있는 슬롯이면 true 입니다. 빈 슬롯 둘은 같은 값이지만 같은 입력이 아니다. */
            static bool isEmptySlot( const InputSlot& slot )
            {
                return slot == InputSlot::fromKey( Key::Unknown );
            }

            /**
             * @brief 두 바인딩이 같은 입력에 발화하는지입니다. Chord(수식 키 + 방아쇠)와 Shortcut(수식 키 마스크 + 키)은 **조합 전체**와 발화 규칙이 같아야 한다 —
             *        Ctrl+F7 과 Ctrl+F8 은 수식 키 슬롯이 같아도 겹치지 않는다. 단일 키끼리는 슬롯과 발화 규칙이 같아야 하고, 합성 축은 누르는 동안 값을 읽으므로 슬롯 하나만 같아도 겹친다.
             * @param pOutSlot 겹친 슬롯(조합이면 방아쇠)
             */
            static bool overlaps( const ActionBinding& left, const ActionBinding& right, InputSlot* pOutSlot )
            {
                const bool bLeftCombo  = left._kind == BindingKind::Chord || left._kind == BindingKind::Shortcut;
                const bool bRightCombo = right._kind == BindingKind::Chord || right._kind == BindingKind::Shortcut;
                if ( bLeftCombo || bRightCombo )
                {
                    if ( left._kind != right._kind || left._trigger != right._trigger )
                        return false;
                    const bool bSame = left._kind == BindingKind::Chord ? ( left._arrSlot[0] == right._arrSlot[0] && left._arrSlot[1] == right._arrSlot[1] )
                                                                        : ( left._modifierMask == right._modifierMask && left._arrSlot[0] == right._arrSlot[0] );
                    if ( bSame )
                        *pOutSlot = left._kind == BindingKind::Chord ? left._arrSlot[1] : left._arrSlot[0];
                    return bSame;
                }
                if ( left._kind == BindingKind::SingleSlot && right._kind == BindingKind::SingleSlot && left._trigger != right._trigger )
                    return false;
                const uint32 leftCount  = BindingKinds::getConflictSlotCount( left._kind );
                const uint32 rightCount = BindingKinds::getConflictSlotCount( right._kind );
                for ( uint32 leftSlot = 0; leftSlot < leftCount; ++leftSlot )
                {
                    for ( uint32 rightSlot = 0; rightSlot < rightCount; ++rightSlot )
                    {
                        if ( left._arrSlot[leftSlot] == right._arrSlot[rightSlot] && isEmptySlot( left._arrSlot[leftSlot] ) == false )
                        {
                            *pOutSlot = left._arrSlot[leftSlot];
                            return true;
                        }
                    }
                }
                return false;
            }
        };
    } // namespace

    void InputMapConflict::collect( const InputMap& inputMap, vector<InputMapConflict>& outListConflict )
    {
        outListConflict.clear();
        const vector<hashed_string>& listAction = inputMap.getActionNames();
        for ( size_t indexA = 0; indexA < listAction.size(); ++indexA )
        {
            const hashed_string& actionA = listAction[indexA];
            for ( uint32 bindA = 0; bindA < inputMap.getBindingCount( actionA ); ++bindA )
            {
                const ActionBinding* pBindingA = inputMap.getBinding( actionA, bindA );
                if ( pBindingA == nullptr )
                    continue;
                const hashed_string layerA = inputMap.findBindingLayer( actionA, bindA );
                for ( size_t indexB = indexA + 1; indexB < listAction.size(); ++indexB )
                {
                    const hashed_string& actionB = listAction[indexB];
                    for ( uint32 bindB = 0; bindB < inputMap.getBindingCount( actionB ); ++bindB )
                    {
                        const ActionBinding* pBindingB = inputMap.getBinding( actionB, bindB );
                        InputSlot            slot{};
                        const bool           bNoBindingOverlap = pBindingB == nullptr || inputMap.findBindingLayer( actionB, bindB ) != layerA ||
                                                       InputMapConflictsInternal::overlaps( *pBindingA, *pBindingB, &slot ) == false;
                        if ( bNoBindingOverlap )
                            continue;
                        InputMapConflict conflict;
                        conflict._actionA    = actionA;
                        conflict._actionB    = actionB;
                        conflict._layer      = layerA;
                        conflict._slot       = slot;
                        conflict._bindIndexA = bindA;
                        conflict._bindIndexB = bindB;
                        outListConflict.push_back( conflict );
                    }
                }
            }
        }
    }

    bool InputMapConflict::isInvolved( const vector<InputMapConflict>& listConflict, const hashed_string& action )
    {
        return VectorUtil::containsIf( listConflict, [&]( const InputMapConflict& conflict )
        { return conflict._actionA == action || conflict._actionB == action; } );
    }

    bool InputMapConflict::isInvolved( const vector<InputMapConflict>& listConflict, const hashed_string& action, uint32 bindIndex )
    {
        return VectorUtil::containsIf( listConflict, [&]( const InputMapConflict& conflict )
        { return ( conflict._actionA == action && conflict._bindIndexA == bindIndex ) || ( conflict._actionB == action && conflict._bindIndexB == bindIndex ); } );
    }

    void InputMapConflict::collectFor( const vector<InputMapConflict>& listConflict, const hashed_string& action, vector<InputMapConflictView>& outListView )
    {
        outListView.clear();
        for ( const InputMapConflict& conflict : listConflict )
        {
            const bool bIsA = conflict._actionA == action;
            if ( bIsA == false && conflict._actionB != action )
                continue;
            InputMapConflictView view;
            view._otherAction    = bIsA ? conflict._actionB : conflict._actionA;
            view._layer          = conflict._layer;
            view._slot           = conflict._slot;
            view._bindIndex      = bIsA ? conflict._bindIndexA : conflict._bindIndexB;
            view._otherBindIndex = bIsA ? conflict._bindIndexB : conflict._bindIndexA;
            outListView.push_back( view );
        }
    }
} // namespace sw::editor
