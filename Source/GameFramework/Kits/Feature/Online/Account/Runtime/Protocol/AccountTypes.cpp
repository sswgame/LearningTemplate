#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountTypes.h"

namespace sw
{
    namespace
    {
        /** @brief @p text 의 @p inoutCursor 부터 다음 '.' 까지를 정수로 읽고 커서를 그 뒤로 옮깁니다. 숫자가 아닌 글자를 만나면 그 항목은 거기까지 읽은 값입니다. */
        uint64 readBuildPart( string_view text, size_t& inoutCursor )
        {
            uint64 value    = 0;
            bool   bNumeric = true;
            size_t cursor   = inoutCursor;
            while ( cursor < text.size() && text[cursor] != '.' )
            {
                const utf8 ch           = text[cursor];
                const bool bAppendDigit = bNumeric && '0' <= ch && ch <= '9' && value < 100000000ull;
                if ( bAppendDigit )
                    value = value * 10 + static_cast<uint64>( ch - '0' );
                else
                    bNumeric = false;
                ++cursor;
            }
            inoutCursor = cursor < text.size() ? cursor + 1 : cursor;
            return value;
        }
    } // namespace
} // namespace sw

namespace sw
{
    int32 AccountUtil::compareBuild( string_view left, string_view right )
    {
        size_t leftCursor  = 0;
        size_t rightCursor = 0;
        while ( leftCursor < left.size() || rightCursor < right.size() )
        {
            const uint64 leftPart  = readBuildPart( left, leftCursor );
            const uint64 rightPart = readBuildPart( right, rightCursor );
            if ( leftPart != rightPart )
                return leftPart < rightPart ? -1 : 1;
        }
        return 0;
    }

    bool AccountUtil::isValidLowerToken( string_view text, int32 maxSize )
    {
        if ( text.empty() || text.size() > static_cast<size_t>( maxSize ) )
            return false;
        for ( const utf8 ch : text )
        {
            const bool bLower            = 'a' <= ch && ch <= 'z';
            const bool bDigit            = '0' <= ch && ch <= '9';
            const bool bInvalidCharacter = bLower == false && bDigit == false && ch != '_';
            if ( bInvalidCharacter )
                return false;
        }
        return true;
    }
} // namespace sw
