#include "pch.h"

#include "Editor/Panels/EditorTestOutputParser.h"

#include "Core/Container/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTestOutputParserInternal
        {
            static constexpr string_view kRunPrefix        = "[ RUN      ] ";
            static constexpr string_view kOkPrefix         = "[       OK ] ";
            static constexpr string_view kFailedPrefix     = "[  FAILED  ] ";
            static constexpr string_view kSkippedPrefix    = "[  SKIPPED ] ";
            static constexpr string_view kHostSuitesPrefix = "Host suites";
            static constexpr string_view kRepeatPrefix     = "Repeating all tests (iteration ";

            /** @brief 케이스 이름 꼴(`이름.이름` — 영문자 · 숫자 · 밑줄, 점 하나)인지입니다. */
            static bool isCaseName( string_view text )
            {
                const size_t dot = text.find( '.' );
                if ( dot == string_view::npos || dot == 0 || dot + 1 >= text.size() || text.find( '.', dot + 1 ) != string_view::npos )
                    return false;
                for ( const utf8 character : text )
                {
                    const bool bWordCharacter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) ||
                                                ( '0' <= character && character <= '9' ) || character == '_' || character == '.';
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
            static EditorTestCaseResult& findOrAddResult( vector<EditorTestCaseResult>& inoutListResult, const string& name, uint32 iteration )
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
    void EditorTestOutputParser::collectTestNames( const vector<string>& listLine, vector<string>& outListName )
    {
        outListName.clear();
        for ( const string& rawLine : listLine )
        {
            const string_view line = StringUtil::trimEnd( rawLine );
            if ( StringUtil::startsWith( line, EditorTestOutputParserInternal::kHostSuitesPrefix ) )
                break;
            // 케이스 줄은 두 칸 들여 쓴 이름 하나다(로그로 같이 나오는 줄은 앞에 시각 · 태그가 붙어 걸리지 않는다).
            if ( StringUtil::startsWith( line, "  " ) == false )
                continue;
            const string_view name = StringUtil::trim( line );
            if ( EditorTestOutputParserInternal::isCaseName( name ) == false )
                continue;
            const string nameText( name );
            if ( std::find( outListName.begin(), outListName.end(), nameText ) == outListName.end() )
                outListName.push_back( nameText );
        }
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
            EditorTestCaseResult& result = EditorTestOutputParserInternal::findOrAddResult( outListResult, name, iteration );
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

    string EditorTestOutputParser::makeFilter( const vector<string>& listCase )
    {
        string filter;
        for ( const string& caseName : listCase )
        {
            if ( filter.empty() == false )
                filter += ":";
            filter += caseName;
        }
        return filter;
    }
} // namespace sw::editor
