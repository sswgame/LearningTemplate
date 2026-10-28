#include "pch.h"

#include "OnlineLoadBot/LoadBotGameSession.h"

namespace sw
{
    bool LoadBotGameSession::start( const NetAddress& serverAddress )
    {
        stop();
        // 서버가 루프백이면 루프백 소켓, 아니면 모든 인터페이스(`GameNetSession::createShard` 와 같은 규칙).
        const bool bLoopbackOnly = serverAddress == NetAddress::makeLoopback( serverAddress._port );
        if ( _transport.open( 0, bLoopbackOnly ) == false )
            return false;
        _bOpen = true;
        _host.initialize( &_transport, NetHostConfig{} ); // 서버의 기본 설정(`GameNetSession::makeServerParams`)과 같다 — 게임 번호 · 선로 버전이 맞아야 붙는다
        _failReason = NetDisconnectReason::None;
        _state      = _host.connect( serverAddress ) ? State::Connecting : State::Failed;
        if ( _state == State::Failed )
            _failReason = NetDisconnectReason::Rejected;
        return true;
    }

    void LoadBotGameSession::stop()
    {
        if ( _bOpen == false )
            return;
        _host.disconnectAll();
        _host.update( 0.0 ); // 끊김 패킷이 나가게 한 번 돈다
        _transport.close();
        _bOpen = false;
        _state = State::Idle;
    }

    void LoadBotGameSession::update( float64 nowSeconds )
    {
        if ( _bOpen == false )
            return;
        _host.update( nowSeconds );
        _host.drainEvents( _listEvent );
        for ( const NetHostEvent& event : _listEvent )
        {
            if ( event._kind == NetHostEvent::Kind::Connected )
            {
                _state = State::Connected;
                continue;
            }
            _state      = State::Failed;
            _failReason = event._reason;
        }
        _listEvent.clear();
    }
} // namespace sw
