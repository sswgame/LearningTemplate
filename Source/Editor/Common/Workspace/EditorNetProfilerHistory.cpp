#include "pch.h"

#include "Editor/Common/Workspace/EditorNetProfilerHistory.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorNetPlaySession.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct EditorNetProfilerHistoryInternal
        {
            static size_t toSlot( EditorNetProfilerChannel channel ) { return static_cast<size_t>( channel ); }

            /** @brief 누적 값 둘의 차를 초당 값으로 바꿉니다. 줄었으면(연결 통계가 새로 시작) 0 입니다. */
            static float32 computeRate( uint64 previous, uint64 current, float64 elapsedSeconds )
            {
                if ( current < previous || elapsedSeconds <= 0.0 )
                    return 0.0f;
                return static_cast<float32>( static_cast<float64>( current - previous ) / elapsedSeconds );
            }

            /** @brief 종류가 같은 몫을 찾습니다(종류는 많아야 수십 개라 차례로 본다). */
            static EditorNetKindShare* findShare( vector<EditorNetKindShare>& listShare, uint8 kind )
            {
                for ( EditorNetKindShare& share : listShare )
                {
                    if ( share._kind == kind )
                        return &share;
                }
                return nullptr;
            }

            static bool isLargerShare( const EditorNetKindShare& left, const EditorNetKindShare& right )
            {
                if ( left._byteCount != right._byteCount )
                    return left._byteCount > right._byteCount;
                return left._kind < right._kind;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorNetProfilerSeries::EditorNetProfilerSeries( int32 capacity )
        : _listSample{ static_cast<size_t>( MathUtil::max( capacity, 1 ) ) }
        , _nextIndex{ 0 }
        , _count{ 0 }
    {
    }

    void EditorNetProfilerSeries::push( const EditorNetProfilerSample& sample )
    {
        _listSample[static_cast<size_t>( _nextIndex )] = sample;
        _nextIndex                                     = ( _nextIndex + 1 ) % getCapacity();
        _count                                         = MathUtil::min( _count + 1, getCapacity() );
    }

    void EditorNetProfilerSeries::clear()
    {
        _nextIndex = 0;
        _count     = 0;
    }

    const EditorNetProfilerSample& EditorNetProfilerSeries::getSample( int32 index ) const
    {
        SW_ASSERT( 0 <= index && index < _count );
        const int32 oldestIndex = ( _nextIndex - _count + getCapacity() ) % getCapacity();
        return _listSample[static_cast<size_t>( ( oldestIndex + index ) % getCapacity() )];
    }

    const EditorNetProfilerSample* EditorNetProfilerSeries::findLatest() const
    {
        return _count > 0 ? &getSample( _count - 1 ) : nullptr;
    }

    void EditorNetProfilerSeries::copyChannel( EditorNetProfilerChannel channel, vector<float32>& outListValue ) const
    {
        outListValue.clear();
        outListValue.reserve( static_cast<size_t>( _count ) );
        const size_t slot = EditorNetProfilerHistoryInternal::toSlot( channel );
        for ( int32 index = 0; index < _count; ++index )
        {
            outListValue.push_back( getSample( index )._arrValue[slot] );
        }
    }

    float32 EditorNetProfilerSeries::computePeak( EditorNetProfilerChannel channel ) const
    {
        const size_t slot = EditorNetProfilerHistoryInternal::toSlot( channel );
        float32      peak = 0.0f;
        for ( int32 index = 0; index < _count; ++index )
        {
            peak = MathUtil::max( peak, getSample( index )._arrValue[slot] );
        }
        return peak;
    }
} // namespace sw::editor

namespace sw::editor
{
    EditorNetProfilerSample EditorNetProfilerUtil::makeSample( const NetStatsReport* pPrevious, float64 previousTimeSeconds, const NetStatsReport& report, float64 timeSeconds )
    {
        using Internal = EditorNetProfilerHistoryInternal;
        EditorNetProfilerSample sample;
        sample._timeSeconds                                                                    = timeSeconds;
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::SentBytesPerSecond )]     = report._sentBytesPerSecond;
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::ReceivedBytesPerSecond )] = report._receivedBytesPerSecond;
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::RttMilliseconds )]        = report._rttMilliseconds;
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::LossPercent )]            = report._lossPercent;
        if ( pPrevious == nullptr )
            return sample;
        const float64 elapsedSeconds = timeSeconds - previousTimeSeconds;
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::SentPacketsPerSecond )] =
            Internal::computeRate( pPrevious->_sentPacketCount, report._sentPacketCount, elapsedSeconds );
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::ReceivedPacketsPerSecond )] =
            Internal::computeRate( pPrevious->_receivedPacketCount, report._receivedPacketCount, elapsedSeconds );
        sample._arrValue[Internal::toSlot( EditorNetProfilerChannel::ResentMessagesPerSecond )] =
            Internal::computeRate( pPrevious->_resentMessageCount, report._resentMessageCount, elapsedSeconds );
        return sample;
    }

    void EditorNetProfilerUtil::computeKindShares( const vector<NetStatsKindEntry>& listEntry, vector<EditorNetKindShare>& outListShare )
    {
        outListShare.clear();
        uint64 totalBytes = 0;
        for ( const NetStatsKindEntry& entry : listEntry )
        {
            totalBytes += entry._byteCount;
            EditorNetKindShare* pShare = EditorNetProfilerHistoryInternal::findShare( outListShare, entry._kind );
            if ( pShare == nullptr )
            {
                outListShare.push_back( EditorNetKindShare{ entry._messageCount, entry._byteCount, 0.0f, entry._kind } );
                continue;
            }
            pShare->_messageCount += entry._messageCount;
            pShare->_byteCount += entry._byteCount;
        }
        for ( EditorNetKindShare& share : outListShare )
        {
            share._share = totalBytes > 0 ? static_cast<float32>( static_cast<float64>( share._byteCount ) / static_cast<float64>( totalBytes ) ) : 0.0f;
        }
        std::sort( outListShare.begin(), outListShare.end(), &EditorNetProfilerHistoryInternal::isLargerShare );
    }
} // namespace sw::editor

namespace sw::editor
{
    EditorNetProfilerHistory::EditorNetProfilerHistory()
        : _listTrack{}
        , _lastQuerySeconds{ 0.0 }
        , _sessionPort{ 0 }
        , _bQueried{ false }
    {
    }

    void EditorNetProfilerHistory::update( EditorNetPlaySession& session, float64 nowSeconds )
    {
        if ( session.isRunning() == false )
            return;
        if ( _sessionPort != session.getPort() )
        {
            clear();
            _sessionPort = session.getPort();
        }
        const int32 processCount = session.getProcessCount();
        if ( _listTrack.size() < static_cast<size_t>( processCount ) )
            _listTrack.resize( static_cast<size_t>( processCount ) );
        for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
        {
            const EditorNetPlayProcessStatus status = session.getProcessStatus( processIndex );
            EditorNetProfilerTrack&          track  = _listTrack[static_cast<size_t>( processIndex )];
            if ( status._statsReportCount != track._seenReportCount )
            {
                track._seenReportCount = status._statsReportCount;
                ingest( processIndex, status._statsReport, nowSeconds );
            }
        }
        const bool bQueryDue = _bQueried == false || EditorNetProfilerUtil::kQueryIntervalSeconds <= nowSeconds - _lastQuerySeconds;
        if ( bQueryDue == false )
            return;
        _bQueried         = true;
        _lastQuerySeconds = nowSeconds;
        for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
        {
            (void)session.sendProcessCommand( processIndex, EditorNetProfilerUtil::kStatsCommand ); // 끝난 프로세스는 false — 그 칸은 더 늘지 않는다
        }
    }

    void EditorNetProfilerHistory::ingest( int32 processIndex, const NetStatsReport& report, float64 timeSeconds )
    {
        if ( processIndex < 0 )
            return;
        if ( _listTrack.size() <= static_cast<size_t>( processIndex ) )
            _listTrack.resize( static_cast<size_t>( processIndex ) + 1 );
        EditorNetProfilerTrack& track = _listTrack[static_cast<size_t>( processIndex )];
        track._series.push( EditorNetProfilerUtil::makeSample( track._bHasReport ? &track._lastReport : nullptr, track._lastTimeSeconds, report, timeSeconds ) );
        track._lastReport      = report;
        track._lastTimeSeconds = timeSeconds;
        track._bHasReport      = true;
    }

    void EditorNetProfilerHistory::clear()
    {
        _listTrack.clear();
        _lastQuerySeconds = 0.0;
        _sessionPort      = 0;
        _bQueried         = false;
    }

    const EditorNetProfilerTrack* EditorNetProfilerHistory::findTrack( int32 processIndex ) const
    {
        if ( processIndex < 0 || _listTrack.size() <= static_cast<size_t>( processIndex ) )
            return nullptr;
        return &_listTrack[static_cast<size_t>( processIndex )];
    }

    int32 EditorNetProfilerHistory::computeSampleCount() const
    {
        int32 count = 0;
        for ( const EditorNetProfilerTrack& track : _listTrack )
        {
            count += track._series.getCount();
        }
        return count;
    }

    float32 EditorNetProfilerHistory::computePeak( EditorNetProfilerChannel channel ) const
    {
        float32 peak = 0.0f;
        for ( const EditorNetProfilerTrack& track : _listTrack )
        {
            peak = MathUtil::max( peak, track._series.computePeak( channel ) );
        }
        return peak;
    }

    void EditorNetProfilerHistory::collectKindEntries( vector<NetStatsKindEntry>& outListEntry ) const
    {
        for ( const EditorNetProfilerTrack& track : _listTrack )
        {
            if ( track._bHasReport )
                outListEntry.insert( outListEntry.end(), track._lastReport._listKindEntry.begin(), track._lastReport._listKindEntry.end() );
        }
    }
} // namespace sw::editor
