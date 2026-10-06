#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/LoginTypes.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    void LoginSessionToken::writeBytes( uint8 ( &outBytes )[LoginConstant::kTokenWireSize] ) const
    {
        for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
            outBytes[byteIndex] = static_cast<uint8>( _sessionId >> ( byteIndex * 8 ) );
        Memory::copy( outBytes + 8, _arrSecret, LoginConstant::kTokenSecretSize );
    }

    bool LoginSessionToken::readBytes( const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size != LoginConstant::kTokenWireSize )
            return false;
        uint64 sessionId = 0;
        for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
            sessionId |= static_cast<uint64>( pData[byteIndex] ) << ( byteIndex * 8 );
        _sessionId = sessionId;
        Memory::copy( _arrSecret, pData + 8, LoginConstant::kTokenSecretSize );
        return true;
    }
} // namespace sw
