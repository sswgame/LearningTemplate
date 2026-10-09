#include "pch.h"

#include "GameFramework/Kits/Online/Account/AccountTypes.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct AccountTypesInternal
        {
            /** @brief @p text 의 @p inoutCursor 부터 다음 '.' 까지를 정수로 읽고 커서를 그 뒤로 옮깁니다. 숫자가 아닌 글자를 만나면 그 칸은 거기까지의 값입니다. */
            static uint64 readBuildPart( string_view text, size_t& inoutCursor )
            {
                uint64 value    = 0;
                bool   bNumeric = true;
                size_t cursor   = inoutCursor;
                while ( cursor < text.size() && text[cursor] != '.' )
                {
                    const utf8 ch = text[cursor];
                    if ( bNumeric && '0' <= ch && ch <= '9' && value < 100000000ull )
                        value = value * 10 + static_cast<uint64>( ch - '0' );
                    else
                        bNumeric = false;
                    ++cursor;
                }
                inoutCursor = cursor < text.size() ? cursor + 1 : cursor;
                return value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( LoginResult result )
    {
        switch ( result )
        {
            case LoginResult::Ok:
                return "Ok";
            case LoginResult::InvalidName:
                return "InvalidName";
            case LoginResult::InvalidPassword:
                return "InvalidPassword";
            case LoginResult::NameTaken:
                return "NameTaken";
            case LoginResult::WrongCredentials:
                return "WrongCredentials";
            case LoginResult::AccountLocked:
                return "AccountLocked";
            case LoginResult::RateLimited:
                return "RateLimited";
            case LoginResult::InvalidToken:
                return "InvalidToken";
            case LoginResult::Expired:
                return "Expired";
            case LoginResult::Revoked:
                return "Revoked";
            case LoginResult::AlreadyLoggedIn:
                return "AlreadyLoggedIn";
            case LoginResult::StoreUnavailable:
                return "StoreUnavailable";
            case LoginResult::AlreadyLinked:
                return "AlreadyLinked";
            case LoginResult::AccountSuspended:
                return "AccountSuspended";
            case LoginResult::UpdateRequired:
                return "UpdateRequired";
            case LoginResult::ProviderUnavailable:
                return "ProviderUnavailable";
            case LoginResult::ProviderRejected:
                return "ProviderRejected";
            case LoginResult::LastLoginMethod:
                return "LastLoginMethod";
            case LoginResult::NotLinked:
                return "NotLinked";
            case LoginResult::InvalidRequest:
                return "InvalidRequest";
        }
        return "Unknown";
    }

    void LoginSessionToken::writeBytes( uint8 ( &outBytes )[LoginConstant::kTokenWireSize] ) const
    {
        for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
        {
            outBytes[byteIndex] = static_cast<uint8>( _sessionId >> ( byteIndex * 8 ) );
        }
        Memory::copy( outBytes + 8, _arrSecret, LoginConstant::kTokenSecretSize );
    }

    bool LoginSessionToken::readBytes( const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size != LoginConstant::kTokenWireSize )
            return false;
        uint64 sessionId = 0;
        for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
        {
            sessionId |= static_cast<uint64>( pData[byteIndex] ) << ( byteIndex * 8 );
        }
        _sessionId = sessionId;
        Memory::copy( _arrSecret, pData + 8, LoginConstant::kTokenSecretSize );
        return true;
    }

    int32 AccountUtil::compareBuild( string_view left, string_view right )
    {
        size_t leftCursor  = 0;
        size_t rightCursor = 0;
        while ( leftCursor < left.size() || rightCursor < right.size() )
        {
            const uint64 leftPart  = AccountTypesInternal::readBuildPart( left, leftCursor );
            const uint64 rightPart = AccountTypesInternal::readBuildPart( right, rightCursor );
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
            const bool bLower = 'a' <= ch && ch <= 'z';
            const bool bDigit = '0' <= ch && ch <= '9';
            if ( bLower == false && bDigit == false && ch != '_' )
                return false;
        }
        return true;
    }
} // namespace sw
