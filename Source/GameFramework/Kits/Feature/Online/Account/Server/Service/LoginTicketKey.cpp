#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/DevOnlineExtension.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/Server/ServerSecret.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/AccountDevOptions.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
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

    expected<void> LoginTicketKey::readFromEnvironment( const string& environment, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize] )
    {
        if ( environment.empty() )
        {
            // 개발 키는 DevOnline 단위만 준다 — 그 단위가 없는 구성(배포)은 주 키가 반드시 있어야 한다.
            expected<const AccountDevOptions*> devOptions = DevOnlineExtension<AccountDevOptions>::require();
            if ( devOptions.has_value() == false )
                return unexpected( std::move( devOptions.error().appendDetail( " - the game ticket master key is required, set the ticketKeySecretEnvironment option" ) ) );
            SW_LOG_WARNING_ID( "LoginTicketKey.AccountKitNoTicketKeySecretEnvironment", "Account kit: no ticketKeySecretEnvironment - using the development ticket key (DevOnline)" );
            Memory::set( outArrKey, ( *devOptions )->_devTicketKeyByte, sizeof( outArrKey ) );
            return {};
        }
        string secret;
        if ( ServerSecret::read( environment, secret ) == false || secret.empty() )
            return unexpected( Error{ ErrorKind::NotFound, "the game ticket master key environment variable is not set", environment } );
        const bool bParsed = parseHex( secret, outArrKey );
        Memory::wipe( secret.data(), secret.size() );
        if ( bParsed == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the game ticket master key must be 64 hexadecimal digits (32 bytes)" } );
        return {};
    }
} // namespace sw
