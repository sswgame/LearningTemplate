#include "pch.h"

#include "Editor/Panels/Profiler/Model/ProfilerPerformanceModel.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    ProfilerPerformanceModel::ProfilerPerformanceModel()
        : _arrFrameTimeHistory{}
        , _historyOffset{ 0 }
        , _drawStopwatch{}
        , _catalogJob{}
        , _catalogCounts{}
        , _bCatalogDirty{ SW_TRUE }
    {
    }

    ProfilerFrameTimeStats ProfilerPerformanceModel::sampleFrame()
    {
        const float32 dt = static_cast<float32>( _drawStopwatch.getElapsedNanoseconds() ) / static_cast<float32>( constant::time::kNanosecondsPerSecond );
        _drawStopwatch.restart();

        ProfilerFrameTimeStats stats;
        stats._currentMs  = dt * constant::time::kMillisecondsPerSecond;
        stats._currentFPS = ( dt > 0.0f ) ? ( 1.0f / dt ) : 0.0f;

        _arrFrameTimeHistory[_historyOffset] = stats._currentMs;
        _historyOffset                       = ( _historyOffset + 1 ) % kHistoryCount;

        // 창이 차기 전의 빈 칸은 표본이 아니다 — 창 크기로 나누면 처음 2 초 동안 평균이 작게, FPS 가 몇십 배로 보인다.
        stats._minMs = MathUtil::kMaxFloat;
        float32 sumMs{ 0.0f };
        uint32  sampleCount{ 0 };
        for ( const float32 value : _arrFrameTimeHistory )
        {
            if ( value <= 0.0f )
                continue;
            stats._minMs = MathUtil::min( stats._minMs, value );
            stats._maxMs = MathUtil::max( stats._maxMs, value );
            sumMs += value;
            ++sampleCount;
        }
        if ( sampleCount == 0 )
        {
            stats._minMs = 0.0f;
            return stats;
        }
        stats._averageMs  = sumMs / static_cast<float32>( sampleCount );
        stats._averageFPS = constant::time::kMillisecondsPerSecond / stats._averageMs;
        return stats;
    }

    void ProfilerPerformanceModel::pollCatalog()
    {
        EditorResourceCatalogCounts counts{};
        const EditorTaskPoll        catalogPoll = _catalogJob.take( counts );
        if ( catalogPoll == EditorTaskPoll::Ready )
            _catalogCounts = counts;
        if ( _bCatalogDirty == SW_TRUE && catalogPoll != EditorTaskPoll::Running )
        {
            _catalogJob.request();
            _bCatalogDirty = SW_FALSE;
        }
    }
} // namespace sw::editor
