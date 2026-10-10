#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountConnectAuthenticator.h"

#include "Core/Memory/Memory.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"

namespace sw
{
    AccountConnectAuthenticator::AccountConnectAuthenticator( const LoginTicketAuthority* pAuthority, const hashed_string& serverID )
        : _serverID{ serverID }
        , _pAuthority{ pAuthority }
        , _nowMs{ 0 }
    {
    }

    bool AccountConnectAuthenticator::findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalID )
    {
        NetGameTicketClaim claim;
        if ( _pAuthority == nullptr || _pAuthority->verifyTicket( pToken, tokenSize, _serverID, _nowMs.load( std::memory_order_relaxed ), claim ) == false )
            return false;
        static_assert( sizeof( outSecret._arrByte ) == NetGameTicket::kSecretSize );
        Memory::copy( outSecret._arrByte, claim._arrSecret, NetGameTicket::kSecretSize );
        outPrincipalID = claim._accountID;
        return true;
    }
} // namespace sw
