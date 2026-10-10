#include "pch.h"

#include "Editor/Panels/PackagingProgressParser.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct PackagingProgressParserInternal
        {
            static constexpr string_view kPrefix     = "[package] ";
            static constexpr string_view kStepWord   = "step ";
            static constexpr string_view kDoneWord   = "done ";
            static constexpr string_view kFailedWord = "FAILED ";

            /** @brief 앞의 10 진수를 읽고 그 뒤를 @p outRest 에 둡니다. 숫자가 없으면 false 입니다. */
            [[nodiscard]] static bool readUnsigned( string_view text, uint64& outValue, string_view& outRest )
            {
                size_t digitCount = 0;
                outValue          = 0;
                while ( digitCount < text.size() && '0' <= text[digitCount] && text[digitCount] <= '9' )
                {
                    outValue = outValue * 10 + static_cast<uint64>( text[digitCount] - '0' );
                    ++digitCount;
                }
                outRest = text.substr( digitCount );
                return digitCount > 0;
            }

            /** @brief `k/n <단계>` 를 읽습니다. */
            [[nodiscard]] static bool parseStep( string_view body, PackagingProgress& inoutProgress )
            {
                uint64      stepIndex = 0;
                uint64      stepCount = 0;
                string_view rest;
                if ( readUnsigned( body, stepIndex, rest ) == false || rest.empty() || rest.front() != '/' )
                    return false;
                if ( readUnsigned( rest.substr( 1 ), stepCount, rest ) == false || stepIndex == 0 || stepCount < stepIndex )
                    return false;
                inoutProgress._stepIndex = static_cast<uint32>( stepIndex );
                inoutProgress._stepCount = static_cast<uint32>( stepCount );
                inoutProgress._stepName  = string{ StringUtil::trim( rest ) };
                inoutProgress._state     = PackagingState::Running;
                return true;
            }

            /** @brief `<폴더> <바이트>` 를 읽습니다. 폴더에 공백이 있을 수 있어 바이트는 마지막 낱말이다. */
            [[nodiscard]] static bool parseDone( string_view body, PackagingProgress& inoutProgress )
            {
                const size_t lastSpace = body.find_last_of( ' ' );
                if ( lastSpace == string_view::npos )
                    return false;
                uint64      byteCount = 0;
                string_view rest;
                if ( readUnsigned( body.substr( lastSpace + 1 ), byteCount, rest ) == false || rest.empty() == false )
                    return false;
                inoutProgress._outputFolder = string{ body.substr( 0, lastSpace ) };
                inoutProgress._byteCount    = byteCount;
                inoutProgress._stepIndex    = inoutProgress._stepCount;
                inoutProgress._state        = PackagingState::Succeeded;
                return true;
            }

            /** @brief `<단계> <이유>` 를 읽습니다. */
            [[nodiscard]] static bool parseFailed( string_view body, PackagingProgress& inoutProgress )
            {
                const size_t space      = body.find( ' ' );
                inoutProgress._stepName = string{ body.substr( 0, space ) };
                inoutProgress._failure  = space == string_view::npos ? string{} : string{ body.substr( space + 1 ) };
                inoutProgress._state    = PackagingState::Failed;
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float32 PackagingProgress::computeFraction() const
    {
        if ( _state == PackagingState::Succeeded || _state == PackagingState::Failed )
            return 1.0f;
        if ( _stepCount == 0 )
            return 0.0f;
        // 시작한 단계는 반쯤 온 것으로 본다 — 단계 안의 진행은 줄로 오지 않는다.
        return ( static_cast<float32>( _stepIndex ) - 0.5f ) / static_cast<float32>( _stepCount );
    }

    bool PackagingProgressParser::parseLine( string_view line, PackagingProgress& inoutProgress )
    {
        using Internal            = PackagingProgressParserInternal;
        const string_view trimmed = StringUtil::trim( line );
        if ( StringUtil::startsWith( trimmed, Internal::kPrefix ) == false )
            return false;
        const string_view body = trimmed.substr( Internal::kPrefix.size() );
        if ( StringUtil::startsWith( body, Internal::kStepWord ) )
            return Internal::parseStep( body.substr( Internal::kStepWord.size() ), inoutProgress );
        if ( StringUtil::startsWith( body, Internal::kDoneWord ) )
            return Internal::parseDone( body.substr( Internal::kDoneWord.size() ), inoutProgress );
        if ( StringUtil::startsWith( body, Internal::kFailedWord ) )
            return Internal::parseFailed( body.substr( Internal::kFailedWord.size() ), inoutProgress );
        return false;
    }

    PackagingProgress PackagingProgressParser::parseOutput( const vector<string>& listLine, int32 exitCode )
    {
        PackagingProgress progress{};
        for ( const string& line : listLine )
        {
            (void)parseLine( line, progress ); // 진입점이 찍지 않은 줄(빌드 · 쿠커 출력)은 지나친다
        }
        const bool bFinishedWithoutVerdict = progress._state == PackagingState::Idle || progress._state == PackagingState::Running;
        if ( bFinishedWithoutVerdict || ( progress._state == PackagingState::Succeeded && exitCode != 0 ) )
        {
            progress._state   = PackagingState::Failed;
            progress._failure = listLine.empty() ? string{ "the packaging script printed nothing (is Python on PATH?)" } : listLine.back();
        }
        return progress;
    }
} // namespace sw::editor
