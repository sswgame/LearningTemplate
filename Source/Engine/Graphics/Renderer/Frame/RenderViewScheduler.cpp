#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/RenderViewScheduler.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct RenderViewSchedulerInternal
        {
            /** @brief 갱신 시각 비교의 여유(초) — 60 Hz 프레임 여섯이 0.1 초에 조금 못 미쳐 한 프레임 늦게 그리는 일을 막는다. */
            static constexpr float64 kTimeSlack = 1.0e-4;
            /** @brief 처음 보는 뷰의 늦은 정도 — 어떤 기다린 뷰보다 먼저 그린다. */
            static constexpr float64 kNeverRenderedOverdue = 1.0e30;
        };
    } // namespace
} // namespace sw

namespace sw
{
    RenderViewScheduler::RenderViewScheduler()
        : _listEntry{}
        , _listScratchDue{}
        , _listScratchOverdue{}
        , _listScratchRenderCount{}
        , _listScratchOrder{}
    {
    }

    RenderViewScheduler::Entry& RenderViewScheduler::findOrAddEntry( uint64 viewId )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._viewId == viewId )
                return entry;
        }
        Entry entry;
        entry._viewId = viewId;
        _listEntry.push_back( entry );
        return _listEntry.back();
    }

    uint32 RenderViewScheduler::schedule( float64 now, const Candidate* pCandidate, uint32 candidateCount, uint32 budget, uint8* pOutRender )
    {
        for ( Entry& entry : _listEntry )
        {
            entry._bSeen = SW_FALSE;
        }
        _listScratchDue.clear();
        _listScratchOverdue.clear();
        _listScratchRenderCount.clear();

        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            pOutRender[index]          = SW_FALSE;
            const Candidate& candidate = pCandidate[index];
            Entry&           entry     = findOrAddEntry( candidate._viewId );
            entry._bSeen               = SW_TRUE;
            if ( candidate._bVisible == SW_FALSE )
                continue;
            float64 overdue = RenderViewSchedulerInternal::kNeverRenderedOverdue;
            if ( entry._lastRenderTime >= 0.0 )
            {
                const float64 interval = candidate._updateRate > 0.0f ? 1.0 / static_cast<float64>( candidate._updateRate ) : 0.0;
                overdue                = ( now - entry._lastRenderTime ) - interval;
                if ( overdue + RenderViewSchedulerInternal::kTimeSlack < 0.0 )
                    continue; // 아직 때가 아니다
            }
            _listScratchDue.push_back( index );
            _listScratchOverdue.push_back( overdue );
            _listScratchRenderCount.push_back( entry._renderCount );
        }

        // 예산이 모자라면 가장 늦은 것부터. 뽑지 못한 뷰는 시각이 그대로라 다음 프레임에 더 늦은 채로 다시 겨룬다.
        uint32 chosenCount = static_cast<uint32>( _listScratchDue.size() );
        if ( budget > 0 && chosenCount > budget )
        {
            _listScratchOrder.resize( _listScratchDue.size() );
            for ( uint32 order = 0; order < static_cast<uint32>( _listScratchOrder.size() ); ++order )
            {
                _listScratchOrder[order] = order;
            }
            std::stable_sort( _listScratchOrder.begin(), _listScratchOrder.end(),
                              [this]( uint32 lhs, uint32 rhs )
            {
                if ( _listScratchOverdue[lhs] != _listScratchOverdue[rhs] )
                    return _listScratchOverdue[lhs] > _listScratchOverdue[rhs];
                return _listScratchRenderCount[lhs] < _listScratchRenderCount[rhs];
            } );
            for ( uint32 order = 0; order < budget; ++order )
            {
                pOutRender[_listScratchDue[_listScratchOrder[order]]] = SW_TRUE;
            }
            chosenCount = budget;
        }
        else
        {
            for ( const uint32 index : _listScratchDue )
            {
                pOutRender[index] = SW_TRUE;
            }
        }

        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            if ( pOutRender[index] == SW_FALSE )
                continue;
            Entry& entry          = findOrAddEntry( pCandidate[index]._viewId );
            entry._lastRenderTime = now;
            ++entry._renderCount;
        }
        // 이번 후보에 없는 뷰(카메라가 사라졌다)는 잊는다.
        _listEntry.erase( std::remove_if( _listEntry.begin(), _listEntry.end(), []( const Entry& entry )
        { return entry._bSeen == SW_FALSE; } ),
                          _listEntry.end() );
        return chosenCount;
    }
} // namespace sw
