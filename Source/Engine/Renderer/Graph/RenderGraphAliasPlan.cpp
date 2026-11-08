#include "pch.h"

#include "Engine/Renderer/Graph/RenderGraphAliasPlan.h"

#include <algorithm>

namespace sw
{
    RenderGraphAliasPlan RenderGraphAliasPlan::build( const vector<RenderGraphAliasCandidate>& listCandidate )
    {
        RenderGraphAliasPlan result;
        // 서술 키 · 첫 레벨 · 목록 순으로 본다 — 같은 키 안에서 먼저 시작하는 후보가 칸을 먼저 연다.
        vector<size_t> listOrder( listCandidate.size() );
        for ( size_t index = 0; index < listOrder.size(); ++index )
        {
            listOrder[index] = index;
        }
        std::stable_sort( listOrder.begin(), listOrder.end(), [&listCandidate]( size_t left, size_t right )
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
            uint32        _lastLevel;
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
                listSlot.push_back( Slot{ candidate._name, candidate._descKey, candidate._lastLevel } );
                continue;
            }
            result._mapAliasToOwner[candidate._name] = pReuse->_owner;
            pReuse->_lastLevel                       = candidate._lastLevel;
        }
        result._ownerCount = static_cast<uint32>( listSlot.size() );
        return result;
    }
} // namespace sw
