#include "pch.h"

#include "Core/String/HexUtil.h"

namespace sw
{
    void HexUtil::appendBytes( string& inoutText, const uint8* pData, size_t size, HexCase hexCase )
    {
        const size_t begin = inoutText.size();
        inoutText.resize( begin + size * 2 );
        for ( size_t index = 0; index < size; ++index )
        {
            HexDigitUtil::writeDigits( inoutText.data() + begin + index * 2, pData[index], 2, hexCase );
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
        const size_t begin = inoutText.size();
        inoutText.resize( begin + digitCount );
        HexDigitUtil::writeDigits( inoutText.data() + begin, value, digitCount, hexCase );
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

    bool HexUtil::decodeBytes( string_view text, vector<uint8>& outBytes )
    {
        outBytes.clear();
        if ( text.size() % 2 != 0 )
            return false;
        outBytes.resize( text.size() / 2 );
        if ( HexDigitUtil::tryParseBytes( text, outBytes.data(), static_cast<uint32>( outBytes.size() ) ) == false )
        {
            outBytes.clear();
            return false;
        }
        return true;
    }
} // namespace sw
