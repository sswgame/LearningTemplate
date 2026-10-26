#include "pch.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include "Core/Container/vector.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/HexUtil.h"

namespace sw
{
    void ServiceKeyUtil::appendHex64( string& outKey, uint64 value )
    {
        HexUtil::appendUint64( outKey, value, static_cast<uint32>( kHexWidth ) );
    }

    bool ServiceKeyUtil::parseHex64( string_view text, uint64& outValue )
    {
        if ( text.size() != static_cast<size_t>( kHexWidth ) )
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
        key.reserve( kHexWidth );
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
