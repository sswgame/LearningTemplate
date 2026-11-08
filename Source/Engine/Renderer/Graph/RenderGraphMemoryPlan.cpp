#include "pch.h"

#include "Engine/Renderer/Graph/RenderGraphMemoryPlan.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct RenderGraphMemoryPlanInternal
        {
            /** @brief @p value 를 @p alignment(2 의 거듭제곱, 0 이면 1) 배수로 올립니다. */
            static uint64 alignUp( uint64 value, uint64 alignment )
            {
                const uint64 safe = alignment > 0 ? alignment : 1;
                return ( value + safe - 1 ) / safe * safe;
            }
        };
    } // namespace

    bool RenderGraphMemoryPlan::overlapsInTime( const RenderGraphMemoryItem& left, const RenderGraphMemoryItem& right )
    {
        return left._firstLevel <= right._lastLevel && right._firstLevel <= left._lastLevel;
    }

    RenderGraphMemoryPlan RenderGraphMemoryPlan::build( const vector<RenderGraphMemoryItem>& listItem )
    {
        using Internal = RenderGraphMemoryPlanInternal;
        RenderGraphMemoryPlan plan;
        plan._listOffset.assign( listItem.size(), 0 );
        // 큰 것부터 놓는다 — 작은 것이 큰 것의 틈을 채운다. 같은 크기는 앞 레벨, 그다음 목록 차례(결정적).
        vector<uint32> listOrder;
        listOrder.reserve( listItem.size() );
        for ( uint32 index = 0; index < static_cast<uint32>( listItem.size() ); ++index )
        {
            if ( listItem[index]._sizeBytes > 0 )
                listOrder.push_back( index );
            plan._sumBytes += listItem[index]._sizeBytes;
        }
        std::stable_sort( listOrder.begin(), listOrder.end(), [&listItem]( uint32 left, uint32 right )
        {
            if ( listItem[left]._sizeBytes != listItem[right]._sizeBytes )
                return listItem[left]._sizeBytes > listItem[right]._sizeBytes;
            return listItem[left]._firstLevel < listItem[right]._firstLevel;
        } );

        vector<uint32> listPlaced;
        listPlaced.reserve( listOrder.size() );
        for ( const uint32 index : listOrder )
        {
            const RenderGraphMemoryItem& item = listItem[index];
            // 수명이 겹치는 이미 놓은 항목의 바이트 구간을 피해 가장 낮은 정렬된 자리를 찾는다. 피할 구간에 걸리면 그 끝 뒤로 옮겨 다시 본다(구간 수만큼 돌면 끝난다).
            uint64 offset = 0;
            bool   bMoved = true;
            while ( bMoved )
            {
                bMoved = false;
                for ( const uint32 placedIndex : listPlaced )
                {
                    const RenderGraphMemoryItem& placed      = listItem[placedIndex];
                    const uint64                 placedBegin = plan._listOffset[placedIndex];
                    const uint64                 placedEnd   = placedBegin + placed._sizeBytes;
                    const bool                   bBytes      = offset < placedEnd && placedBegin < offset + item._sizeBytes;
                    if ( bBytes == false || overlapsInTime( item, placed ) == false )
                        continue;
                    offset = Internal::alignUp( placedEnd, item._alignment );
                    bMoved = true;
                }
            }
            offset                  = Internal::alignUp( offset, item._alignment );
            plan._listOffset[index] = offset;
            plan._heapBytes         = MathUtil::max( plan._heapBytes, offset + item._sizeBytes );
            listPlaced.push_back( index );
        }
        return plan;
    }
} // namespace sw
