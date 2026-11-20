#include "pch.h"

#include "Engine/Renderer/Graph/RenderGraphAliasPlan.h"

#include "Core/Algorithm/VectorUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief union-find 의 뿌리를 찾으며 경로를 줄입니다. */
        uint32 findAliasJoinRoot( vector<uint32>& listParent, uint32 index )
        {
            while ( listParent[index] != index )
            {
                listParent[index] = listParent[listParent[index]];
                index             = listParent[index];
            }
            return index;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void RenderGraphAliasPlan::joinLifetimes( vector<RenderGraphAliasCandidate>& listCandidate, const vector<RenderGraphAliasJoin>& listJoin )
    {
        const uint32   count = static_cast<uint32>( listCandidate.size() );
        vector<uint32> listParent( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            listParent[index] = index;
        }
        for ( const RenderGraphAliasJoin& join : listJoin )
        {
            SW_ASSERT( join._source < count && join._target < count );
            listParent[findAliasJoinRoot( listParent, join._source )] = findAliasJoinRoot( listParent, join._target );
        }
        // 뿌리마다 묶음 구간을 모은 뒤 모든 후보에 나눠 준다.
        vector<uint32> listFirst( count, 0xFFFFFFFFu );
        vector<uint32> listLast( count, 0u );
        for ( uint32 index = 0; index < count; ++index )
        {
            const uint32 root = findAliasJoinRoot( listParent, index );
            listFirst[root]   = std::min( listFirst[root], listCandidate[index]._firstLevel );
            listLast[root]    = std::max( listLast[root], listCandidate[index]._lastLevel );
        }
        for ( uint32 index = 0; index < count; ++index )
        {
            const uint32 root                = findAliasJoinRoot( listParent, index );
            listCandidate[index]._firstLevel = listFirst[root];
            listCandidate[index]._lastLevel  = listLast[root];
        }
    }

    RenderGraphAliasPlan RenderGraphAliasPlan::make( const vector<RenderGraphAliasCandidate>& listCandidate )
    {
        RenderGraphAliasPlan result;
        // 서술 키 · 첫 레벨 · 목록 순으로 본다 — 같은 키 안에서 먼저 시작하는 후보가 칸을 먼저 연다.
        vector<size_t> listOrder( listCandidate.size() );
        for ( size_t index = 0; index < listOrder.size(); ++index )
        {
            listOrder[index] = index;
        }
        VectorUtil::stableSort( listOrder, [&listCandidate]( size_t left, size_t right )
        {
            const RenderGraphAliasCandidate& a = listCandidate[left];
            const RenderGraphAliasCandidate& b = listCandidate[right];
            if ( a._descKey != b._descKey )
                return a._descKey < b._descKey;
            return a._firstLevel < b._firstLevel;
        } );

        /** @brief 텍스처 하나(칸)입니다 — 주인 이름과 지금까지 그 칸을 쓴 마지막 레벨. */
        struct Slot
        {
            hashed_string _owner;
            uint64        _descKey;
            uint32        _firstLevel;
            uint32        _lastLevel;
            uint8         _bPlaceable;
        };
        vector<Slot> listSlot;
        for ( size_t orderIndex : listOrder )
        {
            const RenderGraphAliasCandidate& candidate = listCandidate[orderIndex];
            Slot*                            pReuse    = nullptr;
            for ( Slot& slot : listSlot )
            {
                // 엄격히 앞 레벨에서 끝난 칸만 — 같은 레벨은 병렬로 기록되어 GPU 에서 겹칠 수 있다.
                if ( slot._descKey == candidate._descKey && slot._lastLevel < candidate._firstLevel )
                {
                    pReuse = &slot;
                    break;
                }
            }
            if ( pReuse == nullptr )
            {
                listSlot.push_back( Slot{ candidate._name, candidate._descKey, candidate._firstLevel, candidate._lastLevel, candidate._bPlaceable } );
                continue;
            }
            result._mapAliasToOwner[candidate._name] = pReuse->_owner;
            pReuse->_lastLevel                       = candidate._lastLevel;
            pReuse->_bPlaceable                      = pReuse->_bPlaceable != 0 && candidate._bPlaceable != 0 ? 1 : 0;
        }
        result._ownerCount = static_cast<uint32>( listSlot.size() );
        for ( const Slot& slot : listSlot )
        {
            if ( slot._bPlaceable != 0 )
                result._listPlaceable.push_back( RenderGraphAliasCandidate{ slot._owner, slot._descKey, slot._firstLevel, slot._lastLevel, 1 } );
        }
        return result;
    }
} // namespace sw
