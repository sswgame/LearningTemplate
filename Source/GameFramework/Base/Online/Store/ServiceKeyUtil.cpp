#include "pch.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct ServiceKeyUtilInternal
        {
            static constexpr utf8  kHexDigit[] = "0123456789abcdef";
            static constexpr int32 kHexWidth   = 16;
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ServiceKeyUtil::appendHex64( string& outKey, uint64 value )
    {
        for ( int32 digitIndex = ServiceKeyUtilInternal::kHexWidth - 1; digitIndex >= 0; --digitIndex )
        {
            outKey.push_back( ServiceKeyUtilInternal::kHexDigit[( value >> ( digitIndex * 4 ) ) & 0xFu] );
        }
    }

    bool ServiceKeyUtil::parseHex64( string_view text, uint64& outValue )
    {
        if ( text.size() != static_cast<size_t>( ServiceKeyUtilInternal::kHexWidth ) )
            return false;
        uint64 value = 0;
        for ( const utf8 ch : text )
        {
            uint64 digit = 0;
            if ( '0' <= ch && ch <= '9' )
                digit = static_cast<uint64>( ch - '0' );
            else if ( 'a' <= ch && ch <= 'f' )
                digit = static_cast<uint64>( ch - 'a' + 10 );
            else
                return false;
            value = ( value << 4 ) | digit;
        }
        outValue = value;
        return true;
    }

    string ServiceKeyUtil::makeHex64( uint64 value )
    {
        string key;
        key.reserve( ServiceKeyUtilInternal::kHexWidth );
        appendHex64( key, value );
        return key;
    }

    void ServiceKeyUtil::writeString( BitWriter& outWriter, string_view text )
    {
        outWriter.writeBlob( reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
    }

    bool ServiceKeyUtil::readString( BitReader& reader, int32 maxSize, string& outText )
    {
        vector<uint8> bytes;
        if ( reader.readBlob( bytes, maxSize ) == false )
            return false;
        outText.assign( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return true;
    }
} // namespace sw
