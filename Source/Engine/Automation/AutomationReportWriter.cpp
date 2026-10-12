#include "pch.h"

#include "Engine/Automation/AutomationReportWriter.h"

#include "Core/File/FileUtil.h"

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    AutomationReportWriter::AutomationReportWriter()
        : _reportPath{}
        , _listMetricLine{}
    {
    }

    void AutomationReportWriter::write( const AutomationReportParams& params ) const
    {
        if ( _reportPath.empty() )
            return;
        using Internal = AutomationReportWriterInternal;
        const vector<string> listNoFailure;
        string               json = "{\n  \"name\": \"" + Internal::escapeJSON( params._scenarioName ) + "\",\n";
        json += "  \"result\": \"" + string( params._pResultName ) + "\",\n";
        json += "  \"exitCode\": " + to_string( params._exitCode ) + ",\n";
        json += "  \"frames\": " + to_string( params._frameCount ) + ",\n";
        json += "  \"reason\": \"" + Internal::escapeJSON( params._reason ) + "\",\n";
        json += "  \"failures\": ";
        Internal::appendStringArray( json, params._pListFailure != nullptr ? *params._pListFailure : listNoFailure );
        json += ",\n  \"metrics\": ";
        Internal::appendStringArray( json, _listMetricLine );
        json += "\n}\n";
        const string directory = FileUtil::getDirectoryPart( _reportPath );
        if ( directory.empty() == false )
            (void)FileUtil::ensureDirectoryExists( directory ); // 실패하면 아래 쓰기가 실패로 알린다
        if ( FileUtil::writeTextFile( _reportPath, json ) == false )
            SW_LOG_WARNING( "[Scenario] could not write the report %#", _reportPath.c_str() );
    }
} // namespace sw
