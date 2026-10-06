#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"

namespace sw
{
    namespace
    {
        struct LocalStoreInternal
        {
            static bool isSlotCharacter( utf8 character )
            {
                const bool bLower = 'a' <= character && character <= 'z';
                const bool bDigit = '0' <= character && character <= '9';
                return bLower || bDigit || character == '_' || character == '.' || character == '-';
            }

            /** @brief 마디 하나 — 비지 않고, `.` 으로 시작하지 않고(`..` · 숨은 파일), 허용 글자만. */
            static bool isValidSegment( string_view segment )
            {
                if ( segment.empty() || segment[0] == '.' )
                    return false;
                for ( const utf8 character : segment )
                {
                    if ( isSlotCharacter( character ) == false )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ILocalStore::isValidSlotName( string_view slot )
    {
        if ( slot.empty() || static_cast<int32>( slot.size() ) > kMaxSlotNameSize )
            return false;
        const size_t separator = slot.find( '/' );
        if ( separator == string_view::npos )
            return LocalStoreInternal::isValidSegment( slot );
        return LocalStoreInternal::isValidSegment( slot.substr( 0, separator ) ) && LocalStoreInternal::isValidSegment( slot.substr( separator + 1 ) );
    }

    bool ILocalStore::isValidGroupPrefix( string_view groupPrefix )
    {
        if ( groupPrefix.empty() )
            return true;
        const bool bEndsWithSeparator = groupPrefix.back() == '/' && static_cast<int32>( groupPrefix.size() ) <= kMaxSlotNameSize;
        return bEndsWithSeparator && LocalStoreInternal::isValidSegment( groupPrefix.substr( 0, groupPrefix.size() - 1 ) );
    }
} // namespace sw
