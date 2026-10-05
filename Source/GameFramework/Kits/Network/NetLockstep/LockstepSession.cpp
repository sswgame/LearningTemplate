#include "pch.h"

#include "GameFramework/Kits/Network/NetLockstep/LockstepSession.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    SW_LOG_CALLER( "LockstepSession" );

    LockstepSession::LockstepSession()
        : _listTickInput{}
        , _listTickChecksum{}
        , _listNextInputTick{}
        , _listLeaveTick{}
        , _pHost{ nullptr }
        , _playerCount{ 0 }
        , _localPlayer{ 0 }
        , _inputDelay{ 0 }
        , _stallCount{ 0 }
        , _currentTick{ 0 }
        , _desyncTick{ 0 }
        , _bDesynced{ SW_FALSE }
        , _messageWriter{}
    {
    }

    void LockstepSession::initialize( NetHost* pHost, int32 playerCount, int32 localPlayer, int32 inputDelay )
    {
        _pHost       = pHost;
        _playerCount = MathUtil::max( 1, playerCount );
        _localPlayer = localPlayer;
        _inputDelay  = MathUtil::clamp( inputDelay, 0, kMaxInputDelay );
        _listTickInput.initialize( static_cast<int32>( kInputWindow ) );
        _listTickChecksum.initialize( static_cast<int32>( 2 * kChecksumWindow ) );
        _listNextInputTick.assign( static_cast<size_t>( _playerCount ), 0u );
        _listLeaveTick.assign( static_cast<size_t>( _playerCount ), kNoLeaveTick );
        _stallCount  = 0;
        _currentTick = 0;
        _desyncTick  = 0;
        _bDesynced   = SW_FALSE;
        // 처음 지연 틱들은 모두 빈 입력 — 그동안 첫 입력이 망을 건넌다.
        const vector<uint8> listEmpty;
        for ( uint32 tick = 0; tick < static_cast<uint32>( _inputDelay ); ++tick )
        {
            for ( int32 player = 0; player < _playerCount; ++player )
                (void)storeInput( player, tick, listEmpty );
        }
    }

    uint32 LockstepSession::getLeaveTick( int32 player ) const
    {
        if ( player < 0 || player >= static_cast<int32>( _listLeaveTick.size() ) )
            return kNoLeaveTick;
        return _listLeaveTick[static_cast<size_t>( player )];
    }

    bool LockstepSession::storeInput( int32 player, uint32 tick, const vector<uint8>& listInput )
    {
        if ( player < 0 || player >= _playerCount )
            return false;
        // 신뢰 순서라 플레이어마다 빈틈없이 온다 — 다음 틱이 아니면(겹친 것 · 먼 틱) 버린다. 떠난 뒤의 틱도 버린다(모두가 빈 입력으로 둔다).
        const bool bInWindow = _currentTick <= tick && tick - _currentTick < kInputWindow;
        if ( tick != _listNextInputTick[static_cast<size_t>( player )] || isPresentAt( player, tick ) == false || bInWindow == false )
            return false;
        TickInput* pTickInput = _listTickInput.find( tick );
        if ( pTickInput == nullptr )
        {
            // 그 틱에 처음 온 입력 — 자리의 옛 값(지난 바퀴에 꺼낸 틱)을 비운다.
            pTickInput = &_listTickInput.acquire( tick );
            pTickInput->_listInput.assign( static_cast<size_t>( _playerCount ), vector<uint8>{} );
            pTickInput->_listHas.assign( static_cast<size_t>( _playerCount ), SW_FALSE );
        }
        pTickInput->_listInput[static_cast<size_t>( player )] = listInput;
        pTickInput->_listHas[static_cast<size_t>( player )]   = SW_TRUE;
        ++_listNextInputTick[static_cast<size_t>( player )];
        return true;
    }

    bool LockstepSession::submitLocalInput( const vector<uint8>& listInput )
    {
        if ( static_cast<int32>( listInput.size() ) > NetLockstepMessage::kMaxInputBytes )
        {
            SW_LOG_ERROR( "Lockstep input of %# bytes exceeds the limit of %# bytes - not scheduled", listInput.size(), NetLockstepMessage::kMaxInputBytes );
            return false;
        }
        const uint32 tick = _listNextInputTick[static_cast<size_t>( _localPlayer )];
        if ( tick > _currentTick + kMaxInputLead || storeInput( _localPlayer, tick, listInput ) == false )
            return false;
        BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kInput );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
        writer.writeVarUint( tick );
        writer.writeBlob( listInput.data(), static_cast<int32>( listInput.size() ) );
        if ( _pHost != nullptr )
            (void)_messageWriter.sendToPeers( *_pHost, NetChannelType::ReliableOrdered );
        return true;
    }

    bool LockstepSession::tryAdvance( vector<vector<uint8>>& outListInput )
    {
        TickInput* pTickInput = _listTickInput.find( _currentTick );
        bool       bReady     = true;
        for ( int32 player = 0; player < _playerCount && bReady; ++player )
        {
            const bool bHas = pTickInput != nullptr && pTickInput->_listHas[static_cast<size_t>( player )] != SW_FALSE;
            bReady          = bHas || isPresentAt( player, _currentTick ) == false;
        }
        if ( bReady == false )
        {
            ++_stallCount;
            return false;
        }
        if ( pTickInput != nullptr )
        {
            outListInput = std::move( pTickInput->_listInput );
            _listTickInput.remove( _currentTick );
        }
        else
        {
            outListInput.assign( static_cast<size_t>( _playerCount ), vector<uint8>{} );
        }
        ++_currentTick;
        // 창 아래로 빠진 틱의 체크섬은 더 기다리지 않는다(보고하지 않는 플레이어가 있어도 쌓이지 않게). 틱은 하나씩 넘어가므로 빠지는 틱도
        // (지금 − kChecksumWindow − 1) 하나다 — 그보다 앞은 지난 진행들이 이미 지웠다.
        if ( _currentTick > kChecksumWindow )
            _listTickChecksum.remove( _currentTick - kChecksumWindow - 1u );
        return true;
    }

    bool LockstepSession::evaluateChecksum( uint32 tick, const vector<int64>& listChecksum )
    {
        int64 expected = -1;
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            if ( isPresentAt( player, tick ) == false )
                continue; // 떠난 플레이어는 그 틱부터 다른 입력으로 돌았을 수 있다 — 비교하지 않는다
            const int64 value = listChecksum[static_cast<size_t>( player )];
            if ( value < 0 )
                return false;
            if ( expected < 0 )
                expected = value;
            if ( value != expected && _bDesynced == SW_FALSE )
            {
                _bDesynced  = SW_TRUE;
                _desyncTick = tick;
            }
        }
        return true;
    }

    void LockstepSession::storeChecksum( int32 player, uint32 tick, uint32 checksum )
    {
        const bool bInWindow = tick + kChecksumWindow >= _currentTick && tick < _currentTick + kChecksumWindow;
        if ( player < 0 || player >= _playerCount || _bDesynced == SW_TRUE || bInWindow == false )
            return;
        vector<int64>* pListChecksum = _listTickChecksum.find( tick );
        if ( pListChecksum == nullptr )
        {
            pListChecksum = &_listTickChecksum.acquire( tick );
            pListChecksum->assign( static_cast<size_t>( _playerCount ), -1 );
        }
        ( *pListChecksum )[static_cast<size_t>( player )] = static_cast<int64>( checksum );
        if ( evaluateChecksum( tick, *pListChecksum ) )
            _listTickChecksum.remove( tick );
    }

    void LockstepSession::reportChecksum( uint32 tick, uint32 checksum )
    {
        storeChecksum( _localPlayer, tick, checksum );
        BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kChecksum );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
        writer.writeVarUint( tick );
        writer.writeUint32( checksum );
        if ( _pHost != nullptr )
            (void)_messageWriter.sendToPeers( *_pHost, NetChannelType::ReliableOrdered );
    }

    void LockstepSession::applyLeave( int32 player, uint32 tick )
    {
        _listLeaveTick[static_cast<size_t>( player )] = tick;
        // 그 플레이어만 기다리던 체크섬은 이제 비교할 수 있다 — 창 안을 틱 순으로 훑는다(어긋난 첫 틱이 desync 틱이 된다).
        const uint32 windowFirst = _currentTick > kChecksumWindow ? _currentTick - kChecksumWindow : 0u;
        const uint32 windowEnd   = _currentTick + kChecksumWindow;
        for ( uint32 checkTick = MathUtil::max( tick, windowFirst ); checkTick < windowEnd; ++checkTick )
        {
            const vector<int64>* pListChecksum = _listTickChecksum.find( checkTick );
            if ( pListChecksum != nullptr && evaluateChecksum( checkTick, *pListChecksum ) )
                _listTickChecksum.remove( checkTick );
        }
    }

    void LockstepSession::onConnectionClosed( int32 connectionId, NetDisconnectReason reason )
    {
        (void)reason;
        const int32 player = connectionId + 1;
        if ( _pHost == nullptr || _pHost->isServer() == false || player <= 0 || player >= _playerCount || hasLeft( player ) )
            return;
        // 서버가 받은 마지막 입력의 다음 틱 — 그 앞의 입력은 이미 모두에게 신뢰 순서로 나갔고, 그 뒤는 아무도 받지 못했다.
        const uint32 tick = MathUtil::max( _listNextInputTick[static_cast<size_t>( player )], _currentTick );
        applyLeave( player, tick );
        BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kLeave );
        writer.writeVarUint( static_cast<uint64>( player ) );
        writer.writeVarUint( tick );
        (void)_messageWriter.broadcast( *_pHost, NetChannelType::ReliableOrdered );
    }

    NetHandleResult LockstepSession::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        const int32  player  = static_cast<int32>( body.readVarUint() );
        const uint32 tick    = static_cast<uint32>( body.readVarUint() );
        const bool   bServer = _pHost != nullptr && _pHost->isServer();
        if ( context._kind == NetLockstepMessage::kLeave )
        {
            if ( body.hasOverflowed() )
                return NetHandleResult::Malformed;
            // 떠남은 서버만 정한다.
            if ( bServer == false && 0 < player && player < _playerCount && hasLeft( player ) == false )
                applyLeave( player, tick );
            return NetHandleResult::Handled;
        }
        // 서버는 클라이언트가 자기 번호로만 보내게 한다(남의 입력을 위조하지 못하게).
        if ( bServer && player != context._connectionId + 1 )
            return NetHandleResult::Handled;
        if ( context._kind == NetLockstepMessage::kInput )
        {
            vector<uint8> listInput;
            if ( body.readBlob( listInput, NetLockstepMessage::kMaxInputBytes ) == false )
                return NetHandleResult::Malformed;
            if ( body.hasOverflowed() )
                return NetHandleResult::Malformed;
            if ( storeInput( player, tick, listInput ) && _pHost != nullptr )
                (void)NetMessageRouter::relayToOtherPeers( *_pHost, context ); // 받아들인 것만 — 버린 입력이 다른 클라이언트의 순서를 흐리지 않게
            return NetHandleResult::Handled;
        }
        const uint32 checksum = body.readUint32();
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        storeChecksum( player, tick, checksum );
        if ( _pHost != nullptr )
            (void)NetMessageRouter::relayToOtherPeers( *_pHost, context );
        return NetHandleResult::Handled;
    }
} // namespace sw
