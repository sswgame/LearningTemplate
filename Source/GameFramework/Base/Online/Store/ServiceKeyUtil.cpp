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
            uint8 digit = 0;
            // 키는 소문자 하나로 정한다 — 대문자를 받으면 같은 값이 두 키가 된다.
            if ( ( 'A' <= ch && ch <= 'F' ) || HexUtil::tryReadDigit( ch, digit ) == false )
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
