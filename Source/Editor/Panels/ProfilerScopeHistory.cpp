#include "pch.h"

#include "Editor/Panels/ProfilerScopeHistory.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Widgets/EditorListFilter.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    namespace
    {
        struct ProfilerScopeHistoryInternal
        {
            /** @brief 안 불린 프레임 표시입니다. */
            static constexpr float32 kNoSample = -1.0f;
            /** @brief 백분위 최댓값입니다. */
            static constexpr uint32 kPercentMax = 100;

            /** @brief 정렬된 표본에서 @p percent 백분위를 고릅니다(순위는 1 부터, 올림 — FrameProfiler 와 같은 규칙). */
            static float64 pickPercentile( const vector<float32>& listSorted, uint32 percent )
            {
                if ( listSorted.empty() )
                    return static_cast<float64>( kNoSample );
                const size_t rank  = ( listSorted.size() * percent + ( kPercentMax - 1 ) ) / kPercentMax;
                const size_t index = rank == 0 ? 0 : MathUtil::min( rank - 1, listSorted.size() - 1 );
                return static_cast<float64>( listSorted[index] );
            }

            /** @brief 열의 값입니다. 이름 열은 따로 비교합니다. */
            static float64 getColumnValue( const ProfilerScopeRow& row, ProfilerSortColumn column )
            {
                switch ( column )
                {
                    case ProfilerSortColumn::Name:
                        return 0.0;
                    case ProfilerSortColumn::Last:
                        return row._last;
                    case ProfilerSortColumn::Average:
                        return row._average;
                    case ProfilerSortColumn::P50:
                        return row._p50;
                    case ProfilerSortColumn::P99:
                        return row._p99;
                    case ProfilerSortColumn::Max:
                        return row._max;
                }
                return 0.0;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ProfilerScopeHistory::ProfilerScopeHistory( uint32 windowFrame )
        : _listTrack{}
        , _capturedFrameCount{ 0 }
        , _lastProfilerFrame{ UINT64_MAX }
        , _windowFrame{ windowFrame > 0 ? windowFrame : 1u }
        , _writeIndex{ 0 }
    {
    }

    bool ProfilerScopeHistory::capture( const FrameProfiler& profiler )
    {
        // 프로파일러가 프레임을 접을 때마다 번호가 바뀐다(측정 세션의 워밍업 뒤 reset 은 0 으로 되돌린다 — 그것도 "바뀜" 이다).
        const uint64 profilerFrame = profiler.getFrameCount();
        if ( profilerFrame == _lastProfilerFrame || profiler.isEnabled() == false )
            return false;
        _lastProfilerFrame = profilerFrame;

        const uint32 scopeCount = profiler.getScopeCount();
        for ( uint32 slot = 0; slot < scopeCount; ++slot )
        {
            const utf8* pName = profiler.findScopeName( slot );
            if ( pName == nullptr )
                continue;
            if ( slot >= _listTrack.size() )
                _listTrack.resize( slot + 1 );
            ScopeTrack& track = _listTrack[slot];
            if ( track._listValue.empty() )
            {
                track._name = pName;
                track._listValue.assign( _windowFrame, ProfilerScopeHistoryInternal::kNoSample );
            }
            // 카운터 표시는 처음 쌓일 때 선다 — 종류는 매번 다시 본다.
            const bool bCounter = profiler.isCounterScope( slot );
            track._kind         = classifyScope( track._name, bCounter );

            const uint64 count = profiler.getLastFrameCount( slot );
            float32      value = ProfilerScopeHistoryInternal::kNoSample;
            if ( count > 0 )
                value = bCounter ? static_cast<float32>( count ) : static_cast<float32>( static_cast<float64>( profiler.getLastFrameNanos( slot ) ) / 1000.0 );
            track._listValue[_writeIndex] = value;
        }
        // 이번 프레임에 등록부에 없던 트랙(아직 이름이 안 실린 칸)도 한 칸 비워 둔다 — 고리의 칸 번호가 모든 트랙에서 같아야 한다.
        for ( size_t slot = scopeCount; slot < _listTrack.size(); ++slot )
        {
            if ( _listTrack[slot]._listValue.empty() == false )
                _listTrack[slot]._listValue[_writeIndex] = ProfilerScopeHistoryInternal::kNoSample;
        }

        _writeIndex = ( _writeIndex + 1 ) % _windowFrame;
        ++_capturedFrameCount;
        return true;
    }

    void ProfilerScopeHistory::reset()
    {
        _listTrack.clear();
        _capturedFrameCount = 0;
        _lastProfilerFrame  = UINT64_MAX;
        _writeIndex         = 0;
    }

    void ProfilerScopeHistory::makeRows( const ProfilerRowQuery& query, vector<ProfilerScopeRow>& outListRow ) const
    {
        outListRow.clear();
        const EditorListFilter filter{ string_view( query._filterText ) };
        for ( const ScopeTrack& track : _listTrack )
        {
            if ( track._listValue.empty() || track._kind != query._kind || filter.matches( track._name ) == false )
                continue;
            ProfilerScopeRow row{};
            fillRow( track, row );
            if ( row._sampleCount == 0 )
                continue;
            outListRow.push_back( row );
        }

        const ProfilerSortColumn column      = query._sortColumn;
        const bool               bDescending = query._bDescending;
        std::stable_sort( outListRow.begin(), outListRow.end(),
                          [column, bDescending]( const ProfilerScopeRow& lhs, const ProfilerScopeRow& rhs )
        {
            if ( column == ProfilerSortColumn::Name )
                return bDescending ? ( rhs._name < lhs._name ) : ( lhs._name < rhs._name );
            const float64 lhsValue = ProfilerScopeHistoryInternal::getColumnValue( lhs, column );
            const float64 rhsValue = ProfilerScopeHistoryInternal::getColumnValue( rhs, column );
            return bDescending ? ( lhsValue > rhsValue ) : ( lhsValue < rhsValue );
        } );
    }

    bool ProfilerScopeHistory::copySeries( const string& scopeName, vector<float32>& outListValue ) const
    {
        outListValue.clear();
        for ( const ScopeTrack& track : _listTrack )
        {
            if ( track._listValue.empty() || track._name != scopeName )
                continue;
            // 오래된 것부터 — 다음에 쓸 칸이 가장 오래된 칸이다.
            outListValue.reserve( _windowFrame );
            for ( uint32 offset = 0; offset < _windowFrame; ++offset )
            {
                const float32 value = track._listValue[( _writeIndex + offset ) % _windowFrame];
                outListValue.push_back( value < 0.0f ? 0.0f : value );
            }
            return true;
        }
        return false;
    }

    ProfilerScopeKind ProfilerScopeHistory::classifyScope( const string& name, bool bCounter )
    {
        if ( bCounter )
            return ProfilerScopeKind::Counter;
        if ( name.size() >= 4 && name.compare( 0, 4, "GPU." ) == 0 )
            return ProfilerScopeKind::Gpu;
        return ProfilerScopeKind::Cpu;
    }

    void ProfilerScopeHistory::fillRow( const ScopeTrack& track, ProfilerScopeRow& outRow ) const
    {
        outRow._name                = track._name;
        outRow._kind                = track._kind;
        constexpr float64 kNoSample = static_cast<float64>( ProfilerScopeHistoryInternal::kNoSample );
        outRow._last                = kNoSample;
        outRow._average             = kNoSample;
        outRow._p50                 = kNoSample;
        outRow._p99                 = kNoSample;
        outRow._max                 = kNoSample;

        // 가장 최근 칸은 다음에 쓸 칸의 바로 앞이다.
        const uint32 lastIndex = ( _writeIndex + _windowFrame - 1 ) % _windowFrame;
        outRow._last           = static_cast<float64>( track._listValue[lastIndex] );

        vector<float32> listSorted;
        listSorted.reserve( _windowFrame );
        float64 sum{ 0.0 };
        for ( const float32 value : track._listValue )
        {
            if ( value < 0.0f )
                continue;
            listSorted.push_back( value );
            sum += static_cast<float64>( value );
        }
        outRow._sampleCount = static_cast<uint32>( listSorted.size() );
        if ( listSorted.empty() )
            return;
        std::sort( listSorted.begin(), listSorted.end() );
        outRow._average = sum / static_cast<float64>( listSorted.size() );
        outRow._p50     = ProfilerScopeHistoryInternal::pickPercentile( listSorted, 50 );
        outRow._p99     = ProfilerScopeHistoryInternal::pickPercentile( listSorted, 99 );
        outRow._max     = static_cast<float64>( listSorted.back() );
    }
} // namespace sw::editor
