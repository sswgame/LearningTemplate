#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotMetrics.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LoadBotMetricsInternal
        {
            static constexpr size_t kActionCount          = static_cast<size_t>( LoadBotAction::Count );
            static constexpr int32  kPercentileMedian     = 50;
            static constexpr int32  kPercentileHigh       = 95;
            static constexpr int32  kPercentileTail       = 99;
            static constexpr int32  kJSONIndent           = 2;
            static constexpr int64  kMicrosecondsPerTenth = 100;
            static constexpr int32  kActionColumnWidth    = 22;
            static constexpr int32  kNumberColumnWidth    = 10;
            static constexpr int64  kBasisPointScale      = 10000; ///< 비율을 만분율 정수로 — 표는 "퍼센트.두 자리"
            static constexpr int64  kPercentScale         = 100;
            static constexpr int64  kDecimalBase          = 10; ///< 퍼센트의 소수 두 자리를 한 자리씩 쓴다

            /** @brief @p part / @p whole 의 만분율입니다(전체가 0 이면 0). */
            static int64 computeBasisPoints( int64 part, int64 whole ) { return whole > 0 ? part * kBasisPointScale / whole : 0; }

            /** @brief 만분율을 "퍼센트.두 자리" 글로 씁니다. */
            static string makePercentText( int64 basisPoints )
            {
                utf8 arrBuffer[constant::buffer::kMax32];
                formatstring( arrBuffer, constant::buffer::kMax32, "%#.%#%#", basisPoints / kPercentScale, ( basisPoints % kPercentScale ) / kDecimalBase, basisPoints % kDecimalBase );
                return string( arrBuffer );
            }

            static string makeBusyErrorKey() { return "code" + to_string( OnlineError::kBusy ); }

            /** @brief 칸 하나를 너비에 맞춰 붙입니다(글은 왼쪽, 수는 오른쪽 정렬). */
            static void appendCell( string& outText, string_view cell, int32 width, bool bLeft )
            {
                const int32 padCount = MathUtil::max( 0, width - static_cast<int32>( cell.size() ) );
                if ( bLeft )
                    outText += cell;
                outText.append( static_cast<size_t>( padCount ), ' ' );
                if ( bLeft == false )
                    outText += cell;
                outText += ' ';
            }

            /** @brief 마이크로초를 "밀리초.한 자리" 글로 씁니다. */
            static string makeMillisecondText( int64 microseconds )
            {
                utf8 arrBuffer[constant::buffer::kMax32];
                formatstring( arrBuffer, constant::buffer::kMax32, "%#.%#", microseconds / constant::time::kMicrosecondsPerMillisecond,
                              ( microseconds % constant::time::kMicrosecondsPerMillisecond ) / kMicrosecondsPerTenth );
                return string( arrBuffer );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoadBotMetrics::LoadBotMetrics()
        : _listSampleByAction{}
        , _listErrorCountByAction{}
        , _listMatchID{}
        , _mapErrorKeyToCount{}
        , _mapPushKindToCount{}
        , _openedCount{ 0 }
        , _failedCount{ 0 }
        , _disconnectCount{ 0 }
        , _droppedArrivalCount{ 0 }
        , _serverStats{}
    {
        _listSampleByAction.resize( LoadBotMetricsInternal::kActionCount ); // 중괄호는 원소 목록으로 읽힌다 — 크기는 여기서
        _listErrorCountByAction.resize( LoadBotMetricsInternal::kActionCount, 0 );
    }

    void LoadBotMetrics::recordLatency( LoadBotAction action, int64 latencyUs, string_view errorKey )
    {
        const size_t actionIndex = static_cast<size_t>( action );
        _listSampleByAction[actionIndex].push_back( MathUtil::max<int64>( latencyUs, 0 ) );
        if ( errorKey.empty() )
            return;
        ++_listErrorCountByAction[actionIndex];
        ++_mapErrorKeyToCount[string( errorKey )];
    }

    void LoadBotMetrics::recordPush( uint16 pushKind )
    {
        ++_mapPushKindToCount[pushKind];
    }

    void LoadBotMetrics::recordConnection( bool bOpened )
    {
        if ( bOpened )
            ++_openedCount;
        else
            ++_failedCount;
    }

    void LoadBotMetrics::recordDisconnect()
    {
        ++_disconnectCount;
    }

    void LoadBotMetrics::recordMatch( uint64 matchID )
    {
        _listMatchID.push_back( matchID );
    }

    int64 LoadBotMetrics::computePercentile( const vector<int64>& listSortedSample, int32 percentile )
    {
        if ( listSortedSample.empty() )
            return 0;
        const int64  sampleCount = static_cast<int64>( listSortedSample.size() );
        const int64  rank        = ( static_cast<int64>( percentile ) * sampleCount + 99 ) / 100; // 올림 — 가장 가까운 순위
        const size_t index       = static_cast<size_t>( MathUtil::min( sampleCount, MathUtil::max<int64>( rank, 1 ) ) - 1 );
        return listSortedSample[index];
    }

    void LoadBotMetrics::summarize( vector<LoadBotActionSummary>& outListSummary ) const
    {
        for ( size_t actionIndex = 0; actionIndex < LoadBotMetricsInternal::kActionCount; ++actionIndex )
        {
            vector<int64> listSorted = _listSampleByAction[actionIndex];
            if ( listSorted.empty() )
                continue;
            VectorUtil::sort( listSorted );
            LoadBotActionSummary& summary = outListSummary.emplace_back();
            summary._actionName           = toString( static_cast<LoadBotAction>( actionIndex ) );
            summary._p50Us                = computePercentile( listSorted, LoadBotMetricsInternal::kPercentileMedian );
            summary._p95Us                = computePercentile( listSorted, LoadBotMetricsInternal::kPercentileHigh );
            summary._p99Us                = computePercentile( listSorted, LoadBotMetricsInternal::kPercentileTail );
            summary._maxUs                = listSorted.back();
            summary._count                = static_cast<int64>( listSorted.size() );
            summary._errorCount           = _listErrorCountByAction[actionIndex];
        }
    }

    string LoadBotMetrics::formatTable( int64 elapsedMs ) const
    {
        using Internal = LoadBotMetricsInternal;
        vector<LoadBotActionSummary> listSummary;
        summarize( listSummary );
        string text;
        Internal::appendCell( text, "action", Internal::kActionColumnWidth, true );
        for ( const string_view header : { "count", "errors", "p50 ms", "p95 ms", "p99 ms", "max ms" } )
        {
            Internal::appendCell( text, header, Internal::kNumberColumnWidth, false );
        }
        text += '\n';
        for ( const LoadBotActionSummary& summary : listSummary )
        {
            Internal::appendCell( text, summary._actionName, Internal::kActionColumnWidth, true );
            Internal::appendCell( text, to_string( summary._count ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, to_string( summary._errorCount ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._p50Us ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._p95Us ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._p99Us ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._maxUs ), Internal::kNumberColumnWidth, false );
            text += '\n';
        }
        const int64 completedCount = getTotalCompletedCount();
        const int64 perSecond      = elapsedMs > 0 ? completedCount * constant::time::kMillisecondsPerSecond / elapsedMs : 0;
        utf8        arrLine[constant::buffer::kMax512];
        formatstring( arrLine, constant::buffer::kMax512, "elapsed %# ms, %# actions (%#/s), errors %#, connections opened %# failed %# closed %#, matches %#\n",
                      elapsedMs, completedCount, perSecond, getTotalErrorCount(), _openedCount, _failedCount, _disconnectCount, getDistinctMatchCount() );
        text += arrLine;
        const int64 busyCount = getBusyCount();
        formatstring( arrLine, constant::buffer::kMax512, "kBusy %# (%# percent of actions), dropped arrivals %#\n", busyCount,
                      Internal::makePercentText( Internal::computeBasisPoints( busyCount, completedCount ) ).c_str(), _droppedArrivalCount );
        text += arrLine;
        const LoadBotServerStats& server = _serverStats;
        if ( server._bRead == SW_TRUE )
        {
            formatstring( arrLine, constant::buffer::kMax512, "store commits %# conflicts %# (conflict retry %# percent), ledger %# (assets %# unbalanced %# journals %# negative escrows %# unreadable %#)\n",
                          server._commitCount, server._conflictCount, Internal::makePercentText( Internal::computeBasisPoints( server._conflictCount, server._commitCount ) ).c_str(),
                          server._bBalanced == SW_TRUE ? "balanced" : "NOT BALANCED", server._assetCount, server._mismatchedAssetCount, server._journalCount,
                          server._negativeEscrowCount, server._unreadableRecordCount );
            text += arrLine;
        }
        else
        {
            text += "store and ledger stats: not read (remote server - only --local-server reads them)\n";
        }
        for ( const auto& [errorKey, count] : _mapErrorKeyToCount )
        {
            formatstring( arrLine, constant::buffer::kMax512, "error %# x%#\n", errorKey.c_str(), count );
            text += arrLine;
        }
        for ( const auto& [pushKind, count] : _mapPushKindToCount )
        {
            formatstring( arrLine, constant::buffer::kMax512, "push %# x%#\n", static_cast<uint32>( pushKind ), count );
            text += arrLine;
        }
        return text;
    }

    expected<string> LoadBotMetrics::formatJSON( const LoadBotScenario& scenario, int64 elapsedMs ) const
    {
        vector<LoadBotActionSummary> listSummary;
        summarize( listSummary );
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        root.set( "_scenario" ).setString( scenario._name );
        root.set( "_botCount" ).setInt( scenario._botCount );
        root.set( "_pacing" ).setString( scenario._pacing == LoadBotPacing::Open ? "open" : "closed" ); // 지연이 보내려던 시각 기준인지(열린 루프) 보고서만 보고 안다
        root.set( "_elapsedMs" ).setInt( elapsedMs );
        const JSONValue listAction = root.set( "_listAction" );
        listAction.setArray();
        for ( const LoadBotActionSummary& summary : listSummary )
        {
            const JSONValue entry = listAction.pushBack();
            entry.setObject();
            entry.set( "_name" ).setString( summary._actionName );
            entry.set( "_count" ).setInt( summary._count );
            entry.set( "_errorCount" ).setInt( summary._errorCount );
            entry.set( "_p50Us" ).setInt( summary._p50Us );
            entry.set( "_p95Us" ).setInt( summary._p95Us );
            entry.set( "_p99Us" ).setInt( summary._p99Us );
            entry.set( "_maxUs" ).setInt( summary._maxUs );
        }
        const JSONValue mapError = root.set( "_mapError" );
        mapError.setObject();
        for ( const auto& [errorKey, count] : _mapErrorKeyToCount )
        {
            mapError.set( errorKey ).setInt( count );
        }
        const JSONValue mapPush = root.set( "_mapPush" );
        mapPush.setObject();
        for ( const auto& [pushKind, count] : _mapPushKindToCount )
        {
            mapPush.set( to_string( pushKind ) ).setInt( count );
        }
        const JSONValue connection = root.set( "_connection" );
        connection.setObject();
        connection.set( "_opened" ).setInt( _openedCount );
        connection.set( "_failed" ).setInt( _failedCount );
        connection.set( "_closed" ).setInt( _disconnectCount );
        root.set( "_matchCount" ).setInt( getDistinctMatchCount() );
        root.set( "_executor" ).setString( scenario._executor == LoadBotExecutor::ConstantArrivalRate ? "constant_arrival_rate" : "per_vu_iterations" );
        root.set( "_droppedArrivalCount" ).setInt( _droppedArrivalCount );
        const int64 busyCount = getBusyCount();
        root.set( "_busyCount" ).setInt( busyCount );
        root.set( "_busyBasisPoints" ).setInt( LoadBotMetricsInternal::computeBasisPoints( busyCount, getTotalCompletedCount() ) ); // 만분율 — kBusy / 끝낸 동작
        const LoadBotServerStats& serverStats = _serverStats;
        const JSONValue           server      = root.set( "_server" );
        server.setObject();
        server.set( "_bRead" ).setBool( serverStats._bRead == SW_TRUE );
        server.set( "_commitCount" ).setInt( serverStats._commitCount );
        server.set( "_conflictCount" ).setInt( serverStats._conflictCount );
        server.set( "_conflictRetryBasisPoints" ).setInt( LoadBotMetricsInternal::computeBasisPoints( serverStats._conflictCount, serverStats._commitCount ) ); // 만분율 — 충돌 / 커밋
        server.set( "_bLedgerBalanced" ).setBool( serverStats._bBalanced == SW_TRUE );
        server.set( "_ledgerAssetCount" ).setInt( serverStats._assetCount );
        server.set( "_ledgerMismatchedAssetCount" ).setInt( serverStats._mismatchedAssetCount );
        server.set( "_ledgerJournalCount" ).setInt( serverStats._journalCount );
        server.set( "_ledgerNegativeEscrowCount" ).setInt( serverStats._negativeEscrowCount );
        server.set( "_ledgerUnreadableRecordCount" ).setInt( serverStats._unreadableRecordCount );
        return document.dump( LoadBotMetricsInternal::kJSONIndent );
    }

    int64 LoadBotMetrics::getCompletedCount( LoadBotAction action ) const
    {
        return static_cast<int64>( _listSampleByAction[static_cast<size_t>( action )].size() );
    }

    int64 LoadBotMetrics::getErrorCount( LoadBotAction action ) const
    {
        return _listErrorCountByAction[static_cast<size_t>( action )];
    }

    int64 LoadBotMetrics::getTotalCompletedCount() const
    {
        int64 count = 0;
        for ( const vector<int64>& listSample : _listSampleByAction )
        {
            count += static_cast<int64>( listSample.size() );
        }
        return count;
    }

    int64 LoadBotMetrics::getTotalErrorCount() const
    {
        int64 count = 0;
        for ( const int64 errorCount : _listErrorCountByAction )
        {
            count += errorCount;
        }
        return count;
    }

    int64 LoadBotMetrics::getErrorKeyCount( string_view errorKey ) const
    {
        const auto errorIt = _mapErrorKeyToCount.find( string( errorKey ) );
        return errorIt != _mapErrorKeyToCount.end() ? errorIt->second : 0;
    }

    int64 LoadBotMetrics::getBusyCount() const
    {
        return getErrorKeyCount( LoadBotMetricsInternal::makeBusyErrorKey() );
    }

    int64 LoadBotMetrics::getPushCount( uint16 pushKind ) const
    {
        const auto pushIt = _mapPushKindToCount.find( pushKind );
        return pushIt != _mapPushKindToCount.end() ? pushIt->second : 0;
    }

    int64 LoadBotMetrics::getDistinctMatchCount() const
    {
        vector<uint64> listMatchID = _listMatchID;
        VectorUtil::sort( listMatchID );
        return static_cast<int64>( std::unique( listMatchID.begin(), listMatchID.end() ) - listMatchID.begin() );
    }
} // namespace sw
