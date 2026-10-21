#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"

#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/Server/ServerSecret.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    SW_LOG_CALLER( "LoginTicketKey" );

    namespace
    {
        struct LoginTicketKeyInternal
        {
            static constexpr int32 kHexDigitsPerChunk = ServiceKeyUtil::kHexWidth;
            static constexpr int32 kBytesPerChunk     = 8;
        };
    } // namespace

    bool LoginTicketKey::parseHex( string_view text, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize] )
    {
        using Internal            = LoginTicketKeyInternal;
        const string_view trimmed = StringUtil::trim( text );
        if ( trimmed.size() != static_cast<size_t>( LoginTicketAuthority::kMasterKeySize ) * 2 )
            return false;
        const string lower = StringUtil::toLower( string( trimmed ).c_str() );
        for ( int32 chunkIndex = 0; chunkIndex < LoginTicketAuthority::kMasterKeySize / Internal::kBytesPerChunk; ++chunkIndex )
        {
            uint64 value = 0;
            if ( ServiceKeyUtil::parseHex64( string_view( lower ).substr( static_cast<size_t>( chunkIndex * Internal::kHexDigitsPerChunk ), Internal::kHexDigitsPerChunk ), value ) == false )
                return false;
            for ( int32 byteIndex = 0; byteIndex < Internal::kBytesPerChunk; ++byteIndex )
            {
                const int32 shift                                            = ( Internal::kBytesPerChunk - 1 - byteIndex ) * 8;
                outArrKey[chunkIndex * Internal::kBytesPerChunk + byteIndex] = static_cast<uint8>( ( value >> shift ) & 0xFFu );
            }
        }
        return true;
    }

    bool LoginTicketKey::readFromEnvironment( const string& environment, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize], string& outError )
    {
        if ( environment.empty() )
        {
#if defined( SW_SHIPPING )
            outError = "the game ticket master key is required - set the ticketKeySecretEnvironment option";
            return false;
#else
            SW_LOG_WARNING( "Account kit: no ticketKeySecretEnvironment - using the development ticket key (Dev only)" );
            Memory::set( outArrKey, kDevKeyByte, sizeof( outArrKey ) );
            return true;
#endif
        }
        string secret;
        if ( ServerSecret::read( environment, secret ) == false || secret.empty() )
        {
            outError = "the game ticket master key environment variable '" + environment + "' is not set";
            return false;
        }
        const bool bParsed = parseHex( secret, outArrKey );
        Memory::set( secret.data(), 0, secret.size() );
        if ( bParsed == false )
        {
            outError = "the game ticket master key must be 64 hexadecimal digits (32 bytes)";
            return false;
        }
        return true;
    }
} // namespace sw
