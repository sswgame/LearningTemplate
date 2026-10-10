#include "pch.h"

#include "Editor/Panels/InputMapConflicts.h"

#include "Engine/Input/Map/InputMap.h"

namespace sw::editor
{
    namespace
    {
        struct InputMapConflictsInternal
        {
            /**
             * @brief 두 바인딩이 같은 입력에 발화하는지입니다. Chord(수식 키 + 방아쇠)와 Shortcut(수식 키 마스크 + 키)은 **조합 전체**가 같아야 한다 —
             *        Ctrl+F7 과 Ctrl+F8 은 수식 키 슬롯이 같아도 겹치지 않는다. 나머지 종류는 슬롯 하나라도 같으면 겹친다.
             * @param pOutSlot 겹친 슬롯(조합이면 방아쇠)
             */
            static bool overlaps( const ActionBinding& left, const ActionBinding& right, InputSlot* pOutSlot )
            {
                const bool bLeftCombo  = left._kind == BindingKind::Chord || left._kind == BindingKind::Shortcut;
                const bool bRightCombo = right._kind == BindingKind::Chord || right._kind == BindingKind::Shortcut;
                if ( bLeftCombo || bRightCombo )
                {
                    if ( left._kind != right._kind )
                        return false;
                    const bool bSame = left._kind == BindingKind::Chord ? ( left._arrSlot[0] == right._arrSlot[0] && left._arrSlot[1] == right._arrSlot[1] )
                                                                        : ( left._modifierMask == right._modifierMask && left._arrSlot[0] == right._arrSlot[0] );
                    if ( bSame )
                        *pOutSlot = left._kind == BindingKind::Chord ? left._arrSlot[1] : left._arrSlot[0];
                    return bSame;
                }
                const uint32 leftCount  = BindingKinds::getConflictSlotCount( left._kind );
                const uint32 rightCount = BindingKinds::getConflictSlotCount( right._kind );
                for ( uint32 leftSlot = 0; leftSlot < leftCount; ++leftSlot )
                {
                    for ( uint32 rightSlot = 0; rightSlot < rightCount; ++rightSlot )
                    {
                        if ( left._arrSlot[leftSlot] == right._arrSlot[rightSlot] )
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

    void InputMapConflicts::collect( const InputMap& inputMap, vector<InputMapConflict>& outListConflict )
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
                        if ( pBindingB == nullptr || inputMap.findBindingLayer( actionB, bindB ) != layerA ||
                             InputMapConflictsInternal::overlaps( *pBindingA, *pBindingB, &slot ) == false )
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

    bool InputMapConflicts::involves( const vector<InputMapConflict>& listConflict, const hashed_string& action )
    {
        for ( const InputMapConflict& conflict : listConflict )
        {
            if ( conflict._actionA == action || conflict._actionB == action )
                return true;
        }
        return false;
    }
} // namespace sw::editor
