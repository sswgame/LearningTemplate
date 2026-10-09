#include "pch.h"

#include "Core/Network/NetTypes.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    bool NetAddress::parse( string_view text, uint16 defaultPort, NetAddress& outAddress )
    {
        const size_t      colon       = text.rfind( ':' );
        const string_view hostPart    = colon == string_view::npos ? text : text.substr( 0, colon );
        uint32            arrOctet[4] = { 0, 0, 0, 0 };
        int32             index       = 0;
        size_t            start       = 0;
        while ( index < 4 )
        {
            const size_t      dot   = hostPart.find( '.', start );
            const string_view token = hostPart.substr( start, dot == string_view::npos ? string_view::npos : dot - start );
            int32             value = 0;
            if ( token.empty() || StringUtil::parseInt( token, value ) == false || value < 0 || value > 255 )
                return false;
            arrOctet[index++] = static_cast<uint32>( value );
            if ( dot == string_view::npos )
                break;
            start = dot + 1;
        }
        if ( index != 4 )
            return false;
        int32 port = defaultPort;
        if ( colon != string_view::npos && ( StringUtil::parseInt( text.substr( colon + 1 ), port ) == false || port <= 0 || port > 65535 ) )
            return false;
        outAddress = make( static_cast<uint8>( arrOctet[0] ), static_cast<uint8>( arrOctet[1] ), static_cast<uint8>( arrOctet[2] ), static_cast<uint8>( arrOctet[3] ),
                           static_cast<uint16>( port ) );
        return true;
    }

    string NetAddress::toString() const
    {
        string text;
        for ( int32 shift = 24; shift >= 0; shift -= 8 )
        {
            text += std::to_string( ( _ipv4 >> shift ) & 0xFFu ).c_str();
            if ( shift > 0 )
                text += '.';
        }
        text += ':';
        text += std::to_string( _port ).c_str();
        return text;
    }
} // namespace sw
