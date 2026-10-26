#include "pch.h"

#include "Engine/Automation/AutomationReportWriter.h"

#include "Core/Common/Defines.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        /** @brief `"key": [ "a", "b" ]` 한 멤버를 씁니다. @p pListText 가 없거나 비어 있으면 `[]` 입니다. */
        void writeAutomationReportList( JSONWriter& writer, string_view key, const vector<string>* pListText )
        {
            writer.key( key );
            writer.beginArray();
            if ( pListText != nullptr )
            {
                for ( const string& text : *pListText )
                {
                    writer.valueString( text );
                }
            }
            writer.endArray();
        }

        [[nodiscard]] uint32 toAutomationMilliseconds( float64 seconds )
        {
            return static_cast<uint32>( seconds * static_cast<float64>( constant::kMillisecondsPerSecond ) );
        }

        /** @brief `"timeline": { … }` 을 씁니다 — 프레임 종류마다 `<키>Frames` · `<키>Milliseconds`, 단계 앞 대기 중 가장 긴 것은 `slowestWaits[]`. */
        void writeAutomationTimeline( JSONWriter& writer, const AutomationTimeline& timeline )
        {
            writer.key( "timeline" );
            writer.beginObject();
            for ( uint32 kindIndex = 0; kindIndex < AutomationTimeline::kFrameKindCount; ++kindIndex )
            {
                const AutomationFrameKind      kind  = static_cast<AutomationFrameKind>( kindIndex );
                const AutomationTimelineSlice& slice = timeline.getSlice( kind );
                writer.key( string( AutomationTimeline::getKindKey( kind ) ) + "Frames" );
                writer.valueUint( slice._frameCount );
                writer.key( string( AutomationTimeline::getKindKey( kind ) ) + "Milliseconds" );
                writer.valueUint( toAutomationMilliseconds( slice._seconds ) );
            }
            writer.key( "slowestWaits" );
            writer.beginArray();
            for ( const AutomationSlowWait& wait : timeline.getSlowestWaits() )
            {
                writer.beginObject();
                writer.key( "beforeStep" );
                writer.valueString( wait._stepKind + " #" + to_string( wait._stepNumber ) );
                writer.key( "frame" );
                writer.valueUint( wait._frameIndex );
                writer.key( "frames" );
                writer.valueUint( wait._waitedFrameCount );
                writer.key( "milliseconds" );
                writer.valueUint( toAutomationMilliseconds( wait._seconds ) );
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }
    } // namespace
} // namespace sw

namespace sw
{
    AutomationReportWriter::AutomationReportWriter()
        : _reportPath{}
        , _listMetricLine{}
        , _listRecordLine{}
    {
    }

    void AutomationReportWriter::write( const AutomationReportParams& params ) const
    {
        if ( _reportPath.empty() )
            return;
        // 들여쓰기 2 칸 — 행 감시 보고(`AutomationHangReport::format`)와 같은 모양이고 `records` 가 마지막 멤버다(`readReportRecords`).
        string     json;
        JSONWriter writer{ json, 2 };
        writer.beginObject();
        writer.key( "name" );
        writer.valueString( params._scenarioName );
        writer.key( "result" );
        writer.valueString( params._pResultName );
        writer.key( "exitCode" );
        writer.valueInt( params._exitCode );
        writer.key( "frames" );
        writer.valueUint( params._frameCount );
        writer.key( "reason" );
        writer.valueString( params._reason );
        writer.key( "backend" );
        writer.valueString( params._backendName );
        writer.key( "game" );
        writer.valueString( params._gameName );
        writer.key( "startedAt" );
        writer.valueString( params._startedAt );
        writer.key( "startedAtUnixMs" );
        writer.valueInt( params._startedUnixMs );
        writer.key( "wallMilliseconds" );
        writer.valueUint( params._wallMilliseconds );
        writer.key( "wallLimitMilliseconds" );
        writer.valueUint( params._wallLimitMilliseconds );
        writer.key( "lastStep" );
        writer.valueString( params._lastStep );
        writer.key( "reproduce" );
        writer.valueString( params._reproduce );
        if ( params._pTimeline != nullptr )
            writeAutomationTimeline( writer, *params._pTimeline );
        writeAutomationReportList( writer, "failures", params._pListFailure );
        writeAutomationReportList( writer, "unconfirmed", params._pListUnconfirmed );
        writeAutomationReportList( writer, "unhandledLogs", params._pListUnhandledLog );
        writeAutomationReportList( writer, "allowedLogs", params._pListAllowedLog );
        writeAutomationReportList( writer, "metrics", &_listMetricLine );
        writeAutomationReportList( writer, "records", &_listRecordLine );
        writer.endObject();
        SW_ASSERT( writer.isComplete() );
        json += '\n';
        const string directory = FileUtil::getDirectoryPart( _reportPath );
        if ( directory.empty() == false )
            (void)FileUtil::ensureDirectoryExists( directory ); // 실패하면 아래 쓰기가 실패로 알린다
        if ( FileUtil::writeTextFile( _reportPath, json ) == false )
            SW_LOG_WARNING( "[Scenario] could not write the report %#", _reportPath.c_str() );
    }
} // namespace sw
