#include "pch.h"

#include "Engine/Net/GameConnectSecurity.h"

#include <mutex>

namespace sw
{
    SW_LOG_CALLER( "GameConnectSecurity" );

    GameConnectSecurity::GameConnectSecurity()
        : _mutex{}
        , _pProvider{ nullptr }
        , _pAuthenticator{ nullptr }
        , _refusedWhileUnbound{ 0 }
    {
    }

    void GameConnectSecurity::setProvider( INetSecurityProvider* pProvider )
    {
        ScopedLock<mutex> lock{ _mutex };
        _pProvider = pProvider;
    }

    INetSecurityProvider* GameConnectSecurity::findProvider() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _pProvider;
    }

    bool GameConnectSecurity::bindAuthenticator( INetConnectAuthenticator* pAuthenticator )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( pAuthenticator == nullptr || pAuthenticator == this )
            return false;
        if ( _pAuthenticator != nullptr && _pAuthenticator != pAuthenticator )
        {
            SW_LOG_ERROR( "A game connect authenticator is already bound - only one assembly may check game tickets" );
            return false;
        }
        _pAuthenticator = pAuthenticator;
        return true;
    }

    void GameConnectSecurity::unbindAuthenticator( INetConnectAuthenticator* pAuthenticator )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _pAuthenticator == pAuthenticator )
            _pAuthenticator = nullptr;
    }

    bool GameConnectSecurity::isAuthenticatorBound() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _pAuthenticator != nullptr;
    }

    bool GameConnectSecurity::findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalID )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _pAuthenticator == nullptr )
        {
            // 서비스가 서기 전이다 — 받지 않는다. 처음과 2 의 거듭제곱 번째만 알린다(공격 패킷으로 로그를 채우지 않게).
            const uint64 count = _refusedWhileUnbound.fetch_add( 1, std::memory_order_relaxed ) + 1;
            if ( ( count & ( count - 1 ) ) == 0 )
                SW_LOG_WARNING( "Game connect refused - no authenticator is bound yet (%# so far)", count );
            return false;
        }
        return _pAuthenticator->findSessionSecret( pToken, tokenSize, outSecret, outPrincipalID );
    }
} // namespace sw
