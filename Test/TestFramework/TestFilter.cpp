#include "pch.h"

#include "TestFramework/TestFilter.h"

namespace test
{
    void TestFilter::setPattern( const sw::string& filter )
    {
        _listIncludePattern.clear();
        _listExcludePattern.clear();

        size_t start{ 0 };
        while ( start <= filter.size() )
        {
            const size_t comma = filter.find( ',', start );
            sw::string   token = filter.substr( start, comma == sw::string::npos ? sw::string::npos : comma - start );
            while ( token.empty() == false && token.front() == ' ' )
                token.erase( token.begin() );
            while ( token.empty() == false && token.back() == ' ' )
                token.pop_back();

            if ( token.empty() == false )
            {
                if ( token.front() == '-' )
                    _listExcludePattern.push_back( token.substr( 1 ) );
                else
                    _listIncludePattern.push_back( token );
            }

            if ( comma == sw::string::npos )
                break;
            start = comma + 1;
        }
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
