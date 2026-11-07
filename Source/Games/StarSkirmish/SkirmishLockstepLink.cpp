#include "pch.h"

#include "Games/StarSkirmish/SkirmishLockstepLink.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Net/GameNetSession.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/StarSkirmish/SkirmishMatch.h"

namespace sw
{
    SkirmishLockstepLink::SkirmishLockstepLink()
        : _session{}
        , _handlerSubscription{}
        , _listLocalCommand{}
        , _listPendingInput{}
        , _listTickInput{}
        , _listDecoded{}
        , _pNetSession{ nullptr }
        , _accumulator{ 0.0f }
        , _arrAppliedCount{ 0, 0 }
        , _bPendingInput{ SW_FALSE }
        , _bReportedDesync{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SkirmishLockstepLink::~SkirmishLockstepLink()
    {
        close();
    }

    bool SkirmishLockstepLink::open()
    {
        close();
        GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
        if ( pNetSession == nullptr || pNetSession->isActive() == false )
            return true; // 혼자 하는 판이다
        // 2 인 대전이다 - 서버(리슨 호스트)가 0 번, 붙는 클라이언트가 1 번이다(`LockstepSession` 의 번호 규칙: 서버 연결 id + 1, 첫 연결은 0).
        const int32 localPlayer = pNetSession->getRole() == GameNetRole::Server ? 0 : 1;
        _handlerSubscription    = pNetSession->getRouter().registerHandler( &_session );
        if ( _handlerSubscription.isActive() == false )
        {
            SW_LOG_ERROR_ID( "SkirmishLockstep.HandlerTaken", "[Skirmish] the lockstep message range is taken by another handler - the network match cannot start" );
            return false;
        }
        if ( _session.initialize( &pNetSession->getPeers(), kPlayerCount, localPlayer, LockstepConfig{} ) == false )
        {
            _handlerSubscription.reset();
            return false; // 까닭은 세션이 남겼다
        }
        _pNetSession = pNetSession;
        SW_LOG_INFO_ID( "SkirmishLockstep.Opened", "[Skirmish] lockstep match - you are player %# (%#)", localPlayer, localPlayer == 0 ? "host, blue" : "client, red" );
        return true;
    }

    void SkirmishLockstepLink::close()
    {
        _handlerSubscription.reset(); // 세션 상태는 다음 `open` 의 `initialize` 가 비운다
        _pNetSession        = nullptr;
        _accumulator        = 0.0f;
        _arrAppliedCount[0] = 0;
        _arrAppliedCount[1] = 0;
        _bPendingInput      = SW_FALSE;
        _bReportedDesync    = SW_FALSE;
        _listLocalCommand.clear();
        _listPendingInput.clear();
    }

    bool SkirmishLockstepLink::queueCommand( RTSCommand&& command )
    {
        if ( isActive() == false )
            return false;
        _listLocalCommand.push_back( std::move( command ) );
        return true;
    }

    int32 SkirmishLockstepLink::getAppliedCommandCount( int32 player ) const
    {
        return 0 <= player && player < kPlayerCount ? _arrAppliedCount[player] : 0;
    }

    bool SkirmishLockstepLink::isPeerConnected() const
    {
        return _pNetSession != nullptr && _pNetSession->getConnectedCount() > 0;
    }

    int32 SkirmishLockstepLink::advance( float32 deltaTime, float32 step, SkirmishMatch& match )
    {
        if ( isActive() == false )
            return 0;
        // 걸음마다 입력 하나를 낸다. 상대가 붙기 전에는 내지 않는다 - 받을 쪽이 없는 입력은 사라지고 그 틱은 영영 모이지 않는다.
        _accumulator = MathUtil::min( _accumulator + deltaTime, step * static_cast<float32>( kMaxCatchUpSteps ) );
        while ( isPeerConnected() && _accumulator >= step )
        {
            if ( _bPendingInput == SW_FALSE )
            {
                RTSCommand::writeList( _listLocalCommand, _listPendingInput );
                _listLocalCommand.clear();
                _bPendingInput = SW_TRUE;
            }
            if ( _session.submitLocalInput( _listPendingInput ) == false )
                break; // 앞선 틱이 다 찼다(상대가 늦다) - 같은 입력을 다음 프레임에 다시 낸다
            _bPendingInput = SW_FALSE;
            _accumulator -= step;
        }
        int32 advancedCount = 0;
        while ( _session.tryAdvance( _listTickInput ) )
        {
            const uint32 tick = _session.getCurrentTick() - 1;
            for ( int32 player = 0; player < static_cast<int32>( _listTickInput.size() ); ++player )
            {
                if ( RTSCommand::readList( _listTickInput[static_cast<size_t>( player )], _listDecoded ) == false )
                {
                    // 같은 바이트가 모두에게 갔으므로 모두 같은 자리에서 버린다 - 판은 어긋나지 않는다.
                    SW_LOG_WARNING_ID( "SkirmishLockstep.InputUnreadable", "[Skirmish] lockstep input of player %# at tick %# is unreadable - dropped", player, tick );
                    continue;
                }
                for ( const RTSCommand& command : _listDecoded )
                {
                    _arrAppliedCount[player] += match.getWorld().applyCommand( player, command ) > 0 ? 1 : 0;
                }
            }
            match.update( step );
            if ( tick % kChecksumInterval == 0 )
                _session.reportChecksum( tick, match.computeChecksum() );
            ++advancedCount;
        }
        if ( _session.isDesynced() && _bReportedDesync == SW_FALSE )
        {
            _bReportedDesync = SW_TRUE;
            SW_LOG_ERROR_ID( "SkirmishLockstep.Desynced", "[Skirmish] lockstep desync at tick %# - the two players' matches differ", _session.getDesyncTick() );
        }
        return advancedCount;
    }
} // namespace sw
