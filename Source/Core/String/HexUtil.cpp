#include "pch.h"

#include "Core/String/HexUtil.h"

namespace sw
{
    namespace
    {
        struct HexUtilInternal
        {
            static constexpr const utf8* kLowerDigit = "0123456789abcdef";
            static constexpr const utf8* kUpperDigit = "0123456789ABCDEF";

            static const utf8* getDigits( HexCase hexCase ) { return hexCase == HexCase::Upper ? kUpperDigit : kLowerDigit; }

            /** @brief 16진 글자 하나의 값입니다. 16진 글자가 아니면 false 입니다. */
            [[nodiscard]] static bool tryReadDigit( utf8 character, uint8& outValue )
            {
                if ( character >= '0' && character <= '9' )
                    outValue = static_cast<uint8>( character - '0' );
                else if ( character >= 'a' && character <= 'f' )
                    outValue = static_cast<uint8>( character - 'a' + 10 );
                else if ( character >= 'A' && character <= 'F' )
                    outValue = static_cast<uint8>( character - 'A' + 10 );
                else
                    return false;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void HexUtil::appendBytes( string& inoutText, const uint8* pData, size_t size, HexCase hexCase )
    {
        const utf8* pDigits = HexUtilInternal::getDigits( hexCase );
        inoutText.reserve( inoutText.size() + size * 2 );
        for ( size_t index = 0; index < size; ++index )
        {
            inoutText.push_back( pDigits[pData[index] >> 4] );
            inoutText.push_back( pDigits[pData[index] & 0x0F] );
        }
    }

    string HexUtil::encodeBytes( const uint8* pData, size_t size, HexCase hexCase )
    {
        string text;
        appendBytes( text, pData, size, hexCase );
        return text;
    }

    void HexUtil::appendUint64( string& inoutText, uint64 value, uint32 digitCount, HexCase hexCase )
    {
        SW_ASSERT( digitCount >= 1 && digitCount <= 16 );
        const utf8* pDigits = HexUtilInternal::getDigits( hexCase );
        for ( uint32 digit = digitCount; digit > 0; --digit )
        {
            inoutText.push_back( pDigits[( value >> ( ( digit - 1 ) * 4 ) ) & 0xF] );
        }
    }

    void HexUtil::appendUint64Minimal( string& inoutText, uint64 value, uint32 minDigits, HexCase hexCase )
    {
        uint32 digitCount = 1;
        for ( uint64 rest = value >> 4; rest != 0; rest >>= 4 )
        {
            ++digitCount;
        }
        if ( digitCount < minDigits )
            digitCount = minDigits > 16 ? 16 : minDigits;
        appendUint64( inoutText, value, digitCount, hexCase );
    }

    bool HexUtil::tryReadDigit( const utf8 character, uint8& outValue )
    {
        return HexUtilInternal::tryReadDigit( character, outValue );
    }

    bool HexUtil::decodeBytes( string_view text, vector<uint8>& outBytes )
    {
        outBytes.clear();
        if ( text.size() % 2 != 0 )
            return false;
        outBytes.reserve( text.size() / 2 );
        for ( size_t index = 0; index < text.size(); index += 2 )
        {
            uint8      high  = 0;
            uint8      low   = 0;
            const bool bRead = tryReadDigit( text[index], high ) && tryReadDigit( text[index + 1], low );
            if ( bRead == false )
            {
                outBytes.clear();
                return false;
            }
            outBytes.push_back( static_cast<uint8>( ( high << 4 ) | low ) );
        }
        return true;
    }
} // namespace sw
