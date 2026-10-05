#include "pch.h"

#include "TestFramework/TestFilter.h"

namespace test
{
    void TestFilter::setPattern( const sw::string& filter )
    {
        _listIncludePattern.clear();
        _listExcludePattern.clear();

        // gtest 와 같은 모양이다: 첫 `-` 앞은 고르는 패턴, 뒤는 빼는 패턴이다. 패턴 사이는 `:` 도 쉼표도 된다(`A.*:B.*-A.X` == `A.*,B.*,-A.X`).
        // 시험 이름에는 `-` 가 없으므로 첫 `-` 가 늘 경계다.
        const size_t     dash         = filter.find( '-' );
        const sw::string includePart  = filter.substr( 0, dash );
        const sw::string excludePart  = dash == sw::string::npos ? sw::string{} : filter.substr( dash + 1 );
        const auto       splitPattern = []( const sw::string& part, sw::vector<sw::string>& outListPattern )
        {
            size_t start{ 0 };
            while ( start <= part.size() )
            {
                const size_t separator = part.find_first_of( ",:", start );
                sw::string   token     = part.substr( start, separator == sw::string::npos ? sw::string::npos : separator - start );
                while ( token.empty() == false && token.front() == ' ' )
                    token.erase( token.begin() );
                while ( token.empty() == false && token.back() == ' ' )
                    token.pop_back();
                if ( token.empty() == false )
                    outListPattern.push_back( token );
                if ( separator == sw::string::npos )
                    break;
                start = separator + 1;
            }
        };
        splitPattern( includePart, _listIncludePattern );
        splitPattern( excludePart, _listExcludePattern );
    }

    bool TestFilter::matchGlob( const sw::string& pattern, const sw::string& text )
    {
        if ( pattern.empty() || pattern == "*" )
            return true;

        size_t textPos{ 0 };
        size_t patternPos{ 0 };
        size_t starPattern{ sw::string::npos };
        size_t starText{ sw::string::npos };

        while ( textPos < text.size() )
        {
            if ( patternPos < pattern.size() && pattern[patternPos] == '*' )
            {
                starPattern = patternPos++;
                starText    = textPos;
                continue;
            }
            if ( patternPos < pattern.size() && pattern[patternPos] == text[textPos] )
            {
                ++patternPos;
                ++textPos;
                continue;
            }
            if ( starPattern != sw::string::npos )
            {
                patternPos = starPattern + 1;
                textPos    = ++starText;
                continue;
            }
            return false;
        }

        while ( patternPos < pattern.size() && pattern[patternPos] == '*' )
            ++patternPos;
        return patternPos == pattern.size();
    }

    bool TestFilter::matches( const sw::string& fullName ) const
    {
        bool included = _listIncludePattern.empty();
        for ( const sw::string& pattern : _listIncludePattern )
        {
            if ( matchGlob( pattern, fullName ) )
            {
                included = true;
                break;
            }
        }
        if ( included == false )
            return false;

        for ( const sw::string& pattern : _listExcludePattern )
        {
            if ( matchGlob( pattern, fullName ) )
                return false;
        }
        return true;
    }
} // namespace test
