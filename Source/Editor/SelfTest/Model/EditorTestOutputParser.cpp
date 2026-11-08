#include "pch.h"

#include "Editor/SelfTest/Model/EditorTestOutputParser.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/ParsedLogLine.h"
#include "Core/Testing/TestFramework.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTestOutputParserInternal
        {
            static constexpr string_view kRunPrefix     = "[ RUN      ] ";
            static constexpr string_view kOkPrefix      = "[       OK ] ";
            static constexpr string_view kFailedPrefix  = "[  FAILED  ] ";
            static constexpr string_view kSkippedPrefix = "[  SKIPPED ] ";
            static constexpr string_view kRepeatPrefix  = "Repeating all tests (iteration ";

            /** @brief 케이스 이름 형태(`이름.이름` — 영문자 · 숫자 · 밑줄, 점 하나, 매개변수 케이스의 `/번호`)인지입니다. */
            static bool isCaseName( string_view text )
            {
                const size_t dot                  = text.find( '.' );
                const bool   bMalformedDottedName = dot == string_view::npos || dot == 0 || dot + 1 >= text.size() || text.find( '.', dot + 1 ) != string_view::npos;
                if ( bMalformedDottedName )
                    return false;
                for ( const utf8 character : text )
                {
                    const bool bWordCharacter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) ||
                                                ( '0' <= character && character <= '9' ) || character == '_' || character == '.' || character == '/';
                    if ( bWordCharacter == false )
                        return false;
                }
                return true;
            }

            /** @brief 끝 줄의 `이름 (12.34 ms)` 에서 이름과 시간을 나눕니다. */
            static void splitNameAndTime( string_view rest, string& outName, float32& outMilliseconds )
            {
                outMilliseconds        = 0.0f;
                const size_t openParen = rest.find( " (" );
                outName                = string( openParen == string_view::npos ? rest : rest.substr( 0, openParen ) );
                if ( openParen == string_view::npos )
                    return;
                const string_view timeText = rest.substr( openParen + 2 );
                const size_t      space    = timeText.find( ' ' );
                if ( space != string_view::npos )
                    (void)StringUtil::parseFloat( timeText.substr( 0, space ), outMilliseconds ); // 시간이 읽히지 않으면 0 — 결과 판정에는 쓰지 않는다
            }

            /** @brief 같은 반복의 같은 이름 줄을 찾고 없으면 더합니다. */
            static EditorTestCaseResult& getOrCreateResult( vector<EditorTestCaseResult>& inoutListResult, const string& name, uint32 iteration )
            {
                for ( EditorTestCaseResult& result : inoutListResult )
                {
                    if ( result._name == name && result._iteration == iteration )
                        return result;
                }
                EditorTestCaseResult result;
                result._name      = name;
                result._iteration = iteration;
                inoutListResult.push_back( result );
                return inoutListResult.back();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const utf8* EditorTestCaseResult::getStateText() const
    {
        switch ( _state )
        {
            case EditorTestCaseState::Passed:
                return "OK";
            case EditorTestCaseState::Skipped:
                return "SKIPPED";
            case EditorTestCaseState::Failed:
                return "FAILED";
            case EditorTestCaseState::Running:
                break;
        }
        return "DID NOT FINISH";
    }

    void EditorTestOutputParser::collectResults( const vector<string>& listLine, vector<EditorTestCaseResult>& outListResult )
    {
        outListResult.clear();
        uint32 iteration = 1;
        for ( const string& rawLine : listLine )
        {
            const string_view line = StringUtil::trim( rawLine );
            if ( StringUtil::startsWith( line, EditorTestOutputParserInternal::kRepeatPrefix ) )
            {
                const string_view rest  = line.substr( EditorTestOutputParserInternal::kRepeatPrefix.size() );
                const size_t      space = rest.find( ' ' );
                uint64            value{ 0 };
                if ( space != string_view::npos && StringUtil::parseUint64( rest.substr( 0, space ), value ) )
                    iteration = static_cast<uint32>( value );
                continue;
            }

            EditorTestCaseState state{ EditorTestCaseState::Running };
            string_view         rest;
            if ( StringUtil::startsWith( line, EditorTestOutputParserInternal::kRunPrefix ) )
                rest = line.substr( EditorTestOutputParserInternal::kRunPrefix.size() );
            else if ( StringUtil::startsWith( line, EditorTestOutputParserInternal::kOkPrefix ) )
            {
                state = EditorTestCaseState::Passed;
                rest  = line.substr( EditorTestOutputParserInternal::kOkPrefix.size() );
            }
            else if ( StringUtil::startsWith( line, EditorTestOutputParserInternal::kFailedPrefix ) )
            {
                state = EditorTestCaseState::Failed;
                rest  = line.substr( EditorTestOutputParserInternal::kFailedPrefix.size() );
            }
            else if ( StringUtil::startsWith( line, EditorTestOutputParserInternal::kSkippedPrefix ) )
            {
                state = EditorTestCaseState::Skipped;
                rest  = line.substr( EditorTestOutputParserInternal::kSkippedPrefix.size() );
            }
            else
            {
                continue;
            }

            string  name;
            float32 milliseconds{ 0.0f };
            EditorTestOutputParserInternal::splitNameAndTime( rest, name, milliseconds );
            // 요약 블록의 FAILED 줄("[  FAILED  ] 3 tests, listed below:")은 케이스 이름이 아니다.
            if ( EditorTestOutputParserInternal::isCaseName( name ) == false )
                continue;
            EditorTestCaseResult& result = EditorTestOutputParserInternal::getOrCreateResult( outListResult, name, iteration );
            // 실패가 한 번 적힌 케이스는 뒤의 요약 줄이 OK 로 덮지 않는다(같은 반복 안에서는 끝 줄이 하나다).
            if ( result._state != EditorTestCaseState::Failed )
                result._state = state;
            if ( milliseconds > 0.0f )
                result._milliseconds = milliseconds;
        }
    }

    string_view EditorTestOutputParser::findSuiteName( string_view caseName )
    {
        const size_t dot = caseName.find( '.' );
        return dot == string_view::npos ? caseName : caseName.substr( 0, dot );
    }

    vector<string> EditorTestOutputParser::makeScenarioArguments( string_view scenarioPath, string_view backendFlag )
    {
        const bool     bEditor = ( "/" + string( scenarioPath ) ).find( "/automation/editor/" ) != string::npos;
        vector<string> listArgument;
        if ( backendFlag.empty() == false )
            listArgument.push_back( "-" + string( backendFlag ) );
        if ( bEditor )
            listArgument.push_back( "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::ENABLE_EDITOR ) ) );
        listArgument.push_back( "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::SCENARIO ) ) + "=" + string( scenarioPath ) );
        listArgument.push_back( "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::UNATTENDED ) ) );
        return listArgument;
    }

    uint32 EditorTestOutputParser::countUndeclaredErrorLines( const vector<string>& listLine )
    {
        uint32        count{ 0 };
        int64         declared{ 0 };
        ParsedLogLine parsed;
        for ( const string& line : listLine )
        {
            if ( StringUtil::startsWith( line, test::kDeclaredErrorLinesPrefix ) )
            {
                const string_view number = string_view{ line }.substr( test::kDeclaredErrorLinesPrefix.size() );
                if ( StringUtil::parseInt64( number.substr( 0, number.find( ' ' ) ), declared ) == false )
                    declared = 0; // 모양이 다르면 빼지 않는다(모두 센다)
                continue;
            }
            if ( ParsedLogLine::tryParse( line, parsed ) && parsed._level == LogLevel::Error )
                ++count;
        }
        return static_cast<int64>( count ) > declared ? count - static_cast<uint32>( declared ) : 0;
    }

    vector<string> EditorTestOutputParser::makeUnitTestArguments( string_view filter )
    {
        return { "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::TEST ) ),
                 "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::TEST_FILTER ) ) + "=" + string( filter ) };
    }
} // namespace sw::editor
