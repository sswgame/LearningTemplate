#include "pch.h"

#include "Engine/Automation/AutomationReportWriter.h"

#include "Core/Common/Defines.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct AutomationReportWriterInternal
        {
            static string escapeJSON( string_view text )
            {
                string escaped;
                escaped.reserve( text.size() + 8 );
                for ( const utf8 character : text )
                {
                    if ( character == '"' || character == '\\' )
                    {
                        escaped.push_back( '\\' );
                        escaped.push_back( character );
                    }
                    else if ( character == '\n' )
                    {
                        escaped += "\\n";
                    }
                    else if ( static_cast<uint8>( character ) >= 0x20 )
                    {
                        escaped.push_back( character );
                    }
                }
                return escaped;
            }

            /** @brief `"key": [ "a", "b" ]` 의 배열 부분을 붙입니다. 비어 있으면 `[]` 입니다. */
            static void appendStringArray( string& inoutJSON, const vector<string>& listText )
            {
                inoutJSON += "[";
                for ( size_t index = 0; index < listText.size(); ++index )
                {
                    inoutJSON += index == 0 ? "\n    \"" : ",\n    \"";
                    inoutJSON += escapeJSON( listText[index] ) + "\"";
                }
                inoutJSON += listText.empty() ? "]" : "\n  ]";
            }

            /** @brief `"timeline": { … },` 를 붙입니다 — 프레임 종류마다 `<키>Frames` · `<키>Milliseconds`, 단계 앞 대기 중 가장 긴 것은 `slowestWaits[]`. */
            static void appendTimeline( string& inoutJSON, const AutomationTimeline& timeline )
            {
                inoutJSON += "  \"timeline\": {\n";
                for ( uint32 kindIndex = 0; kindIndex < AutomationTimeline::kFrameKindCount; ++kindIndex )
                {
                    const AutomationFrameKind              kind  = static_cast<AutomationFrameKind>( kindIndex );
                    const AutomationTimelineSlice&         slice = timeline.getSlice( kind );
                    StringBuilder<constant::kMaxBuffer128> line;
                    line.appendFormat( "    \"%#Frames\": %#,\n    \"%#Milliseconds\": %#,\n", AutomationTimeline::getKindKey( kind ), slice._frameCount,
                                       AutomationTimeline::getKindKey( kind ), toMilliseconds( slice._seconds ) );
                    inoutJSON += line.c_str();
                }
                inoutJSON += "    \"slowestWaits\": [";
                const vector<AutomationSlowWait>& listWait = timeline.getSlowestWaits();
                for ( size_t index = 0; index < listWait.size(); ++index )
                {
                    const AutomationSlowWait&              wait = listWait[index];
                    StringBuilder<constant::kMaxBuffer256> line;
                    line.appendFormat( "%#\n      { \"beforeStep\": \"%# #%#\", \"frame\": %#, \"frames\": %#, \"milliseconds\": %# }", index == 0 ? "" : ",",
                                       escapeJSON( wait._stepKind ).c_str(), wait._stepNumber, wait._frameIndex, wait._waitedFrameCount, toMilliseconds( wait._seconds ) );
                    inoutJSON += line.c_str();
                }
                inoutJSON += listWait.empty() ? "]\n  },\n" : "\n    ]\n  },\n";
            }

            [[nodiscard]] static uint32 toMilliseconds( float64 seconds ) { return static_cast<uint32>( seconds * static_cast<float64>( constant::kMillisecondsPerSecond ) ); }
        };
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
        using Internal = AutomationReportWriterInternal;
        const vector<string> listEmpty;
        string               json = "{\n  \"name\": \"" + Internal::escapeJSON( params._scenarioName ) + "\",\n";
        json += "  \"result\": \"" + string( params._pResultName ) + "\",\n";
        json += "  \"exitCode\": " + to_string( params._exitCode ) + ",\n";
        json += "  \"frames\": " + to_string( params._frameCount ) + ",\n";
        json += "  \"reason\": \"" + Internal::escapeJSON( params._reason ) + "\",\n";
        json += "  \"backend\": \"" + Internal::escapeJSON( params._backendName ) + "\",\n";
        json += "  \"game\": \"" + Internal::escapeJSON( params._gameName ) + "\",\n";
        json += "  \"startedAt\": \"" + Internal::escapeJSON( params._startedAt ) + "\",\n";
        json += "  \"startedAtUnixMs\": " + to_string( params._startedUnixMs ) + ",\n";
        json += "  \"wallMilliseconds\": " + to_string( params._wallMilliseconds ) + ",\n";
        json += "  \"wallLimitMilliseconds\": " + to_string( params._wallLimitMilliseconds ) + ",\n";
        json += "  \"lastStep\": \"" + Internal::escapeJSON( params._lastStep ) + "\",\n";
        json += "  \"reproduce\": \"" + Internal::escapeJSON( params._reproduce ) + "\",\n";
        if ( params._pTimeline != nullptr )
            Internal::appendTimeline( json, *params._pTimeline );
        json += "  \"failures\": ";
        Internal::appendStringArray( json, params._pListFailure != nullptr ? *params._pListFailure : listEmpty );
        json += ",\n  \"unconfirmed\": ";
        Internal::appendStringArray( json, params._pListUnconfirmed != nullptr ? *params._pListUnconfirmed : listEmpty );
        json += ",\n  \"unhandledLogs\": ";
        Internal::appendStringArray( json, params._pListUnhandledLog != nullptr ? *params._pListUnhandledLog : listEmpty );
        json += ",\n  \"allowedLogs\": ";
        Internal::appendStringArray( json, params._pListAllowedLog != nullptr ? *params._pListAllowedLog : listEmpty );
        json += ",\n  \"metrics\": ";
        Internal::appendStringArray( json, _listMetricLine );
        json += ",\n  \"records\": ";
        Internal::appendStringArray( json, _listRecordLine );
        json += "\n}\n";
        const string directory = FileUtil::getDirectoryPart( _reportPath );
        if ( directory.empty() == false )
            (void)FileUtil::ensureDirectoryExists( directory ); // 실패하면 아래 쓰기가 실패로 알린다
        if ( FileUtil::writeTextFile( _reportPath, json ) == false )
            SW_LOG_WARNING( "[Scenario] could not write the report %#", _reportPath.c_str() );
    }
} // namespace sw
