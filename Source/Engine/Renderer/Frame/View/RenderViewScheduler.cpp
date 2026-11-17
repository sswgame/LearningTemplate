#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct RenderViewSchedulerInternal
        {
            /** @brief 갱신 시각 비교의 여유(초)입니다. 60 Hz 프레임 여섯이 0.1 초에 조금 못 미쳐 한 프레임 늦게 그리는 일을 막습니다. */
            static constexpr float64 kTimeSlack = 1.0e-4;
            /** @brief 처음 보는 뷰의 늦은 정도입니다. 어떤 기다린 뷰보다 먼저 그립니다. */
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
        , _listScratchListOverdue{}
        , _listScratchListOrder{}
    {
    }

    RenderViewScheduler::Entry& RenderViewScheduler::getOrCreateEntry( uint64 viewID )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._viewID == viewID )
                return entry;
        }
        Entry entry;
        entry._viewID = viewID;
        _listEntry.push_back( entry );
        return _listEntry.back();
    }

    uint32 RenderViewScheduler::schedule( float64 now, const Candidate* pCandidate, uint32 candidateCount, uint32 maxListed, const RenderViewBudget& budget, uint8* pOutListed,
                                          uint8* pOutRender )
    {
        uint32 chosenCount = 0;
        for ( Entry& entry : _listEntry )
        {
            entry._bSeen = SW_FALSE;
        }
        _listScratchDue.clear();
        _listScratchOverdue.clear();
        _listScratchRenderCount.clear();

        // 후보마다 늦은 정도를 먼저 구하고(기록도 이때 본 것으로 적는다), 자리를 보이는 것 → 늦은 것 → 덜 그린 것 순으로 준다.
        _listScratchListOverdue.resize( candidateCount );
        _listScratchListOrder.resize( candidateCount );
        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            const Candidate& candidate = pCandidate[index];
            Entry&           entry     = getOrCreateEntry( candidate._viewID );
            entry._bSeen               = SW_TRUE;
            float64 overdue            = RenderViewSchedulerInternal::kNeverRenderedOverdue;
            if ( entry._lastRenderTime >= 0.0 )
            {
                const float64 interval = candidate._updateRate > 0.0f ? 1.0 / static_cast<float64>( candidate._updateRate ) : 0.0;
                overdue                = ( now - entry._lastRenderTime ) - interval;
            }
            _listScratchListOverdue[index] = overdue;
            _listScratchListOrder[index]   = index;
            pOutListed[index]              = SW_FALSE;
            pOutRender[index]              = SW_FALSE;
        }
        VectorUtil::stableSort( _listScratchListOrder, [&]( uint32 lhs, uint32 rhs )
        {
            if ( pCandidate[lhs]._bVisible != pCandidate[rhs]._bVisible )
                return pCandidate[lhs]._bVisible != SW_FALSE;
            if ( _listScratchListOverdue[lhs] != _listScratchListOverdue[rhs] )
                return _listScratchListOverdue[lhs] > _listScratchListOverdue[rhs];
            return getOrCreateEntry( pCandidate[lhs]._viewID )._renderCount < getOrCreateEntry( pCandidate[rhs]._viewID )._renderCount;
        } );
        const uint32 listedCount = candidateCount < maxListed ? candidateCount : maxListed;
        for ( uint32 order = 0; order < listedCount; ++order )
        {
            pOutListed[_listScratchListOrder[order]] = SW_TRUE;
        }

        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            const Candidate& candidate = pCandidate[index];
            if ( pOutListed[index] == SW_FALSE || candidate._bVisible == SW_FALSE )
                continue;
            const float64 overdue = _listScratchListOverdue[index];
            if ( overdue + RenderViewSchedulerInternal::kTimeSlack < 0.0 )
                continue; // 아직 때가 아니다
            const Entry& entry = getOrCreateEntry( candidate._viewID );
            if ( candidate._bBudgeted == SW_FALSE )
            {
                pOutRender[index] = SW_TRUE;
                ++chosenCount;
                continue;
            }
            _listScratchDue.push_back( index );
            _listScratchOverdue.push_back( overdue );
            _listScratchRenderCount.push_back( entry._renderCount );
        }

        // 가장 늦은 것부터 예산이 찰 때까지. 뽑지 못한 뷰는 시각이 그대로라 다음 프레임에 더 늦은 채로 다시 겨룬다.
        _listScratchOrder.resize( _listScratchDue.size() );
        for ( uint32 order = 0; order < static_cast<uint32>( _listScratchOrder.size() ); ++order )
        {
            _listScratchOrder[order] = order;
        }
        VectorUtil::stableSort( _listScratchOrder, [this]( uint32 lhs, uint32 rhs )
        {
            if ( _listScratchOverdue[lhs] != _listScratchOverdue[rhs] )
                return _listScratchOverdue[lhs] > _listScratchOverdue[rhs];
            return _listScratchRenderCount[lhs] < _listScratchRenderCount[rhs];
        } );
        uint32 budgetedCount = 0;
        uint64 pixelCount    = 0;
        for ( const uint32 order : _listScratchOrder )
        {
            const uint32 index         = _listScratchDue[order];
            const uint64 nextPixel     = pixelCount + pCandidate[index]._pixelCount;
            const bool   bCountFull    = budget._maxViewCount > 0 && budgetedCount >= budget._maxViewCount;
            const bool   bPixelOver    = budget._maxPixelCount > 0 && budgetedCount > 0 && nextPixel > budget._maxPixelCount;
            const bool   bBudgetIsFull = bCountFull || bPixelOver;
            if ( bBudgetIsFull )
                break; // 건너뛰어 작은 뷰로 채우지 않는다 — 큰 뷰가 굶는다
            pOutRender[index] = SW_TRUE;
            pixelCount        = nextPixel;
            ++budgetedCount;
        }
        chosenCount += budgetedCount;

        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            if ( pOutRender[index] == SW_FALSE )
                continue;
            Entry& entry          = getOrCreateEntry( pCandidate[index]._viewID );
            entry._lastRenderTime = now;
            ++entry._renderCount;
        }
        // 이번 후보에 없는 뷰(카메라가 사라졌다)는 잊는다.
        VectorUtil::removeIf( _listEntry, []( const Entry& entry )
        { return entry._bSeen == SW_FALSE; } );
        return chosenCount;
    }
} // namespace sw
