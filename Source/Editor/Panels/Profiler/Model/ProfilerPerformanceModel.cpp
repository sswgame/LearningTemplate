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
        const float32 dt = static_cast<float32>( _drawStopwatch.getElapsedNanoseconds() ) / static_cast<float32>( constant::kNanosecondsPerSecond );
        _drawStopwatch.restart();

        ProfilerFrameTimeStats stats;
        stats._currentMs  = dt * constant::kMillisecondsPerSecond;
        stats._currentFPS = ( dt > 0.0f ) ? ( 1.0f / dt ) : 0.0f;

        _arrFrameTimeHistory[_historyOffset] = stats._currentMs;
        _historyOffset                       = ( _historyOffset + 1 ) % kHistoryCount;

        stats._minMs = MathUtil::kMaxFloat;
        float32 sumMs{ 0.0f };
        for ( const float32 value : _arrFrameTimeHistory )
        {
            if ( value <= 0.0f )
                continue;
            stats._minMs = MathUtil::min( stats._minMs, value );
            stats._maxMs = MathUtil::max( stats._maxMs, value );
            sumMs += value;
        }
        stats._averageMs  = sumMs / static_cast<float32>( kHistoryCount );
        stats._averageFPS = ( stats._averageMs > 0.0f ) ? ( 1000.0f / stats._averageMs ) : 0.0f;
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
