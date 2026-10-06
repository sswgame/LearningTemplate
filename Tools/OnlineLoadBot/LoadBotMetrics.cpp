#include "pch.h"

#include "OnlineLoadBot/LoadBotMetrics.h"

#include "Core/Common/Defines.h"
#include "Core/String/formatString.h"

#include "Engine/Utility/Json/JsonDocument.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LoadBotMetricsInternal
        {
            static constexpr size_t kActionCount           = static_cast<size_t>( LoadBotAction::Count );
            static constexpr int32  kPercentileMedian      = 50;
            static constexpr int32  kPercentileHigh        = 95;
            static constexpr int32  kPercentileTail        = 99;
            static constexpr int32  kJsonIndent            = 2;
            static constexpr int64  kMicrosecondsPerMs     = 1000;
            static constexpr int64  kMicrosecondsPerTenth  = 100;
            static constexpr int64  kMillisecondsPerSecond = 1000;
            static constexpr int32  kActionColumnWidth     = 22;
            static constexpr int32  kNumberColumnWidth     = 10;

            /** @brief 칸 하나를 너비에 맞춰 붙입니다(글은 왼쪽, 수는 오른쪽 정렬). */
            static void appendCell( string& outText, string_view cell, int32 width, bool bLeft )
            {
                const int32 padCount = std::max( 0, width - static_cast<int32>( cell.size() ) );
                if ( bLeft )
                    outText += cell;
                outText.append( static_cast<size_t>( padCount ), ' ' );
                if ( bLeft == false )
                    outText += cell;
                outText += ' ';
            }

            static string makeNumberText( int64 value )
            {
                utf8 arrBuffer[constant::kMaxBuffer32];
                formatstring( arrBuffer, constant::kMaxBuffer32, "%#", value );
                return string( arrBuffer );
            }

            /** @brief 마이크로초를 "밀리초.한 자리" 글로 씁니다. */
            static string makeMillisecondText( int64 microseconds )
            {
                utf8 arrBuffer[constant::kMaxBuffer32];
                formatstring( arrBuffer, constant::kMaxBuffer32, "%#.%#", microseconds / kMicrosecondsPerMs,
                              ( microseconds % kMicrosecondsPerMs ) / kMicrosecondsPerTenth );
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
        , _listMatchId{}
        , _mapErrorKeyToCount{}
        , _mapPushKindToCount{}
        , _openedCount{ 0 }
        , _failedCount{ 0 }
        , _disconnectCount{ 0 }
    {
        _listSampleByAction.resize( LoadBotMetricsInternal::kActionCount ); // 중괄호는 원소 목록으로 읽힌다 — 크기는 여기서
        _listErrorCountByAction.resize( LoadBotMetricsInternal::kActionCount, 0 );
    }

    void LoadBotMetrics::recordLatency( LoadBotAction action, int64 latencyUs, string_view errorKey )
    {
        const size_t actionIndex = static_cast<size_t>( action );
        _listSampleByAction[actionIndex].push_back( std::max<int64>( latencyUs, 0 ) );
        if ( errorKey.empty() )
            return;
        ++_listErrorCountByAction[actionIndex];
        ++_mapErrorKeyToCount[string( errorKey )];
    }

    void LoadBotMetrics::recordPush( uint16 pushKind ) { ++_mapPushKindToCount[pushKind]; }

    void LoadBotMetrics::recordConnection( bool bOpened )
    {
        if ( bOpened )
            ++_openedCount;
        else
            ++_failedCount;
    }

    void LoadBotMetrics::recordDisconnect() { ++_disconnectCount; }

    void LoadBotMetrics::recordMatch( uint64 matchId ) { _listMatchId.push_back( matchId ); }

    int64 LoadBotMetrics::computePercentile( const vector<int64>& listSortedSample, int32 percentile )
    {
        if ( listSortedSample.empty() )
            return 0;
        const int64  sampleCount = static_cast<int64>( listSortedSample.size() );
        const int64  rank        = ( static_cast<int64>( percentile ) * sampleCount + 99 ) / 100; // 올림 — 가장 가까운 순위
        const size_t index       = static_cast<size_t>( std::min( sampleCount, std::max<int64>( rank, 1 ) ) - 1 );
        return listSortedSample[index];
    }

    void LoadBotMetrics::summarize( vector<LoadBotActionSummary>& outListSummary ) const
    {
        for ( size_t actionIndex = 0; actionIndex < LoadBotMetricsInternal::kActionCount; ++actionIndex )
        {
            vector<int64> listSorted = _listSampleByAction[actionIndex];
            if ( listSorted.empty() )
                continue;
            std::sort( listSorted.begin(), listSorted.end() );
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
            Internal::appendCell( text, header, Internal::kNumberColumnWidth, false );
        text += '\n';
        for ( const LoadBotActionSummary& summary : listSummary )
        {
            Internal::appendCell( text, summary._actionName, Internal::kActionColumnWidth, true );
            Internal::appendCell( text, Internal::makeNumberText( summary._count ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeNumberText( summary._errorCount ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._p50Us ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._p95Us ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._p99Us ), Internal::kNumberColumnWidth, false );
            Internal::appendCell( text, Internal::makeMillisecondText( summary._maxUs ), Internal::kNumberColumnWidth, false );
            text += '\n';
        }
        const int64 completedCount = getTotalCompletedCount();
        const int64 perSecond      = elapsedMs > 0 ? completedCount * Internal::kMillisecondsPerSecond / elapsedMs : 0;
        utf8        arrLine[constant::kMaxBuffer512];
        formatstring( arrLine, constant::kMaxBuffer512, "elapsed %# ms, %# actions (%#/s), errors %#, connections opened %# failed %# closed %#, matches %#\n",
                      elapsedMs, completedCount, perSecond, getTotalErrorCount(), _openedCount, _failedCount, _disconnectCount, getDistinctMatchCount() );
        text += arrLine;
        for ( const auto& [errorKey, count] : _mapErrorKeyToCount )
        {
            formatstring( arrLine, constant::kMaxBuffer512, "error %# x%#\n", errorKey.c_str(), count );
            text += arrLine;
        }
        for ( const auto& [pushKind, count] : _mapPushKindToCount )
        {
            formatstring( arrLine, constant::kMaxBuffer512, "push %# x%#\n", static_cast<uint32>( pushKind ), count );
            text += arrLine;
        }
        return text;
    }

    string LoadBotMetrics::formatJson( const LoadBotScenario& scenario, int64 elapsedMs ) const
    {
        vector<LoadBotActionSummary> listSummary;
        summarize( listSummary );
        JsonDocument    document;
        const JsonValue root = document.makeObject();
        root.set( "_scenario", false ).setString( scenario._name );
        root.set( "_botCount", false ).setInt( scenario._botCount );
        root.set( "_elapsedMs", false ).setInt( elapsedMs );
        const JsonValue listAction = root.set( "_listAction", false );
        listAction.setArray();
        for ( const LoadBotActionSummary& summary : listSummary )
        {
            const JsonValue entry = listAction.pushBack();
            entry.setObject();
            entry.set( "_name", false ).setString( summary._actionName );
            entry.set( "_count", false ).setInt( summary._count );
            entry.set( "_errorCount", false ).setInt( summary._errorCount );
            entry.set( "_p50Us", false ).setInt( summary._p50Us );
            entry.set( "_p95Us", false ).setInt( summary._p95Us );
            entry.set( "_p99Us", false ).setInt( summary._p99Us );
            entry.set( "_maxUs", false ).setInt( summary._maxUs );
        }
        const JsonValue mapError = root.set( "_mapError", false );
        mapError.setObject();
        for ( const auto& [errorKey, count] : _mapErrorKeyToCount )
            mapError.set( errorKey, false ).setInt( count );
        const JsonValue mapPush = root.set( "_mapPush", false );
        mapPush.setObject();
        for ( const auto& [pushKind, count] : _mapPushKindToCount )
            mapPush.set( LoadBotMetricsInternal::makeNumberText( pushKind ), false ).setInt( count );
        const JsonValue connection = root.set( "_connection", false );
        connection.setObject();
        connection.set( "_opened", false ).setInt( _openedCount );
        connection.set( "_failed", false ).setInt( _failedCount );
        connection.set( "_closed", false ).setInt( _disconnectCount );
        root.set( "_matchCount", false ).setInt( getDistinctMatchCount() );
        return document.dump( LoadBotMetricsInternal::kJsonIndent );
    }

    int64 LoadBotMetrics::getCompletedCount( LoadBotAction action ) const { return static_cast<int64>( _listSampleByAction[static_cast<size_t>( action )].size() ); }

    int64 LoadBotMetrics::getErrorCount( LoadBotAction action ) const { return _listErrorCountByAction[static_cast<size_t>( action )]; }

    int64 LoadBotMetrics::getTotalCompletedCount() const
    {
        int64 count = 0;
        for ( const vector<int64>& listSample : _listSampleByAction )
            count += static_cast<int64>( listSample.size() );
        return count;
    }

    int64 LoadBotMetrics::getTotalErrorCount() const
    {
        int64 count = 0;
        for ( const int64 errorCount : _listErrorCountByAction )
            count += errorCount;
        return count;
    }

    int64 LoadBotMetrics::getErrorKeyCount( string_view errorKey ) const
    {
        const auto errorIt = _mapErrorKeyToCount.find( string( errorKey ) );
        return errorIt != _mapErrorKeyToCount.end() ? errorIt->second : 0;
    }

    int64 LoadBotMetrics::getPushCount( uint16 pushKind ) const
    {
        const auto pushIt = _mapPushKindToCount.find( pushKind );
        return pushIt != _mapPushKindToCount.end() ? pushIt->second : 0;
    }

    int64 LoadBotMetrics::getDistinctMatchCount() const
    {
        vector<uint64> listMatchId = _listMatchId;
        std::sort( listMatchId.begin(), listMatchId.end() );
        return static_cast<int64>( std::unique( listMatchId.begin(), listMatchId.end() ) - listMatchId.begin() );
    }
} // namespace sw
