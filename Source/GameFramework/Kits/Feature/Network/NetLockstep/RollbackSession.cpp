#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetLockstep/RollbackSession.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"
#include "GameFramework/Kits/Feature/Network/NetLockstep/LockstepSession.h"

namespace sw
{
    RollbackSession::RollbackSession()
        : _listRecord{}
        , _listReceive{}
        , _listPeer{}
        , _listNewFrameScratch{}
        , _sendWindow{}
        , _settings{}
        , _pHost{ nullptr }
        , _pGame{ nullptr }
        , _playerCount{ 0 }
        , _localPlayer{ 0 }
        , _frame{ 0 }
        , _pendingRollbackFrame{ -1 }
        , _rollbackCount{ 0 }
        , _resimulatedFrameCount{ 0 }
        , _stallCount{ 0 }
        , _timeSyncWaitCount{ 0 }
        , _messageWriter{}
    {
    }

    bool RollbackSession::initialize( NetHost* pHost, IRollbackGame* pGame, int32 playerCount, int32 localPlayer, const RollbackConfig& settings )
    {
        // 실패하면 시작하지 않은 세션으로 남긴다. 값을 범위 안으로 맞춰 돌지 않는다.
        // 설정 칸의 범위는 `RollbackConfig` 의 `PROPERTY( Min · Max )` 가 원본이다. 여기서는 칸 사이의 관계(내 번호 < 플레이어 수)만 따로 본다.
        _pGame                     = nullptr;
        const bool   bPlayersValid = playerCount >= 1 && localPlayer >= 0 && localPlayer < playerCount;
        const string rangeError    = ConfigRangeUtil::describeRangeErrors( settings );
        const bool   bCanStart     = pGame != nullptr && bPlayersValid && rangeError.empty();
        if ( bCanStart == false )
        {
            SW_LOG_ERROR( "RollbackSession: refused to start - game %#, players %#, local player %#, settings '%#'", pGame != nullptr ? "set" : "missing", playerCount,
                          localPlayer, rangeError.empty() ? "within range" : rangeError.c_str() );
            return false;
        }
        _pHost       = pHost;
        _pGame       = pGame;
        _playerCount = playerCount;
        _localPlayer = localPlayer;
        _settings    = settings;
        _listRecord.initialize( kHistorySize );
        _listReceive.assign( static_cast<size_t>( _playerCount ), NetInputReceiveBuffer{} );
        for ( NetInputReceiveBuffer& buffer : _listReceive )
        {
            buffer.initialize( kHistorySize, kInputFormat, NetInputWindowMode::Manual );
        }
        _sendWindow.initialize( kFrameWindow, kInputFormat );
        // 지연 프레임은 모두 중립 입력이다. 모두가 아는 값이므로 상대도 이미 확인한 것으로 둔다.
        PeerState peer;
        peer._ackedLocalFrame = _settings._inputDelay - 1;
        _listPeer.assign( static_cast<size_t>( _playerCount ), peer );
        _listNewFrameScratch.clear();
        _frame                 = 0;
        _pendingRollbackFrame  = -1;
        _rollbackCount         = 0;
        _resimulatedFrameCount = 0;
        _stallCount            = 0;
        _timeSyncWaitCount     = 0;
        moveReceiveWindows();
        const uint8 neutralInput = 0;
        for ( int32 frame = 0; frame < _settings._inputDelay; ++frame )
        {
            for ( int32 player = 0; player < _playerCount; ++player )
            {
                receiveInput( player, frame, neutralInput );
            }
            (void)_sendWindow.push( static_cast<uint32>( frame ), &neutralInput, 1 );
        }
        return true;
    }

    int32 RollbackSession::getConfirmedFrame() const
    {
        int32 confirmed = _frame;
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            confirmed = MathUtil::min( confirmed, getConfirmedInputFrame( player ) );
        }
        return confirmed;
    }

    int32 RollbackSession::computeFrameLead() const
    {
        int32 lead = 0;
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            const PeerState& peer = _listPeer[static_cast<size_t>( player )];
            if ( player == _localPlayer || peer._reportedFrame < 0 )
                continue;
            // 두 이점에는 같은 망 지연이 들어 있다. 서로 빼면 지연은 사라지고 프레임 차이의 두 배가 남는다.
            lead = MathUtil::max( lead, ( peer._localAdvantage - peer._remoteAdvantage ) / 2 );
        }
        return lead;
    }

    void RollbackSession::receiveInput( int32 player, int32 frame, uint8 input )
    {
        const bool bInvalidInputFrame = player < 0 || player >= _playerCount || frame < 0;
        if ( bInvalidInputFrame )
            return;
        // 창 [지금 − 창, 지금 + 창) 밖의 입력과 이미 있는 입력(다시 보낸 것)은 버퍼가 버린다.
        if ( _listReceive[static_cast<size_t>( player )].store( static_cast<uint32>( frame ), &input, 1 ) )
            markMisprediction( player, frame, input );
    }

    void RollbackSession::markMisprediction( int32 player, int32 frame, uint8 input )
    {
        // 이미 예측으로 진행한 프레임인데 입력이 다르면 되감아야 한다.
        if ( frame >= _frame )
            return;
        const FrameRecord* pRecord       = _listRecord.find( static_cast<uint32>( frame ) );
        const bool         bMispredicted = pRecord != nullptr && pRecord->_listInput.size() == static_cast<size_t>( _playerCount ) &&
                                   pRecord->_listInput[static_cast<size_t>( player )] != input;
        if ( bMispredicted )
            _pendingRollbackFrame = _pendingRollbackFrame < 0 ? frame : MathUtil::min( _pendingRollbackFrame, frame );
    }

    void RollbackSession::moveReceiveWindows()
    {
        // 링 버퍼(128슬롯)를 두 반쪽으로 쓴다. 창 안의 프레임끼리는 슬롯을 덮어쓰지 않고, 예측과 되감기가 읽는 지난 64프레임도 남는다.
        const uint32 first = static_cast<uint32>( MathUtil::max( 0, _frame - kFrameWindow ) );
        const uint32 end   = static_cast<uint32>( _frame + kFrameWindow );
        for ( NetInputReceiveBuffer& buffer : _listReceive )
        {
            buffer.setWindow( first, end );
        }
    }

    int32 RollbackSession::getConfirmedInputFrame( int32 player ) const
    {
        return static_cast<int32>( _listReceive[static_cast<size_t>( player )].getFirstMissingTick() ) - 1;
    }

    uint8 RollbackSession::predictInput( int32 player, int32 frame ) const
    {
        // 그 프레임의 입력이 있으면 그것을 쓴다. 없으면 가장 최근에 받은 입력을 쓰며, 같은 버튼을 계속 누르고 있다고 본다.
        const uint32         lowestFrame = static_cast<uint32>( MathUtil::max( 0, frame - kFrameWindow ) );
        const NetInputEntry* pEntry      = _listReceive[static_cast<size_t>( player )].findLatestAtOrBefore( static_cast<uint32>( frame ), lowestFrame );
        return pEntry != nullptr ? pEntry->_bytes[0] : static_cast<uint8>( 0 );
    }

    void RollbackSession::rollbackTo( int32 frame )
    {
        const FrameRecord* pStart = _listRecord.find( static_cast<uint32>( frame ) );
        if ( pStart == nullptr )
            return; // 기록 밖이라 고칠 수 없다. 최대 예측 안에서는 생기지 않는다
        _pGame->loadState( pStart->_listState );
        ++_rollbackCount;
        for ( int32 replay = frame; replay < _frame; ++replay )
        {
            FrameRecord& record = _listRecord.acquire( static_cast<uint32>( replay ) );
            if ( replay != frame )
                _pGame->saveState( record._listState );
            record._listInput.resize( static_cast<size_t>( _playerCount ) );
            for ( int32 player = 0; player < _playerCount; ++player )
            {
                record._listInput[static_cast<size_t>( player )] = predictInput( player, replay );
            }
            _pGame->advanceFrame( record._listInput, true );
            ++_resimulatedFrameCount;
        }
    }

    void RollbackSession::sendLocalInputs()
    {
        if ( _pHost == nullptr )
            return;
        // 모두가 확인한 다음 프레임부터 싣는다. 상대가 아직 못 받은 입력은 확인이 올라갈 때까지 메시지마다 다시 싣는다(`NetInputSendWindow`).
        int32 acked = getConfirmedInputFrame( _localPlayer );
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            if ( player != _localPlayer )
                acked = MathUtil::min( acked, _listPeer[static_cast<size_t>( player )]._ackedLocalFrame );
        }
        _sendWindow.acknowledge( static_cast<uint32>( acked + 1 ) );
        BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kRollbackInput );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
        writer.writeVarUint( static_cast<uint64>( _frame ) );
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            writer.writeVarUint( _listReceive[static_cast<size_t>( player )].getFirstMissingTick() );
            writer.writeVarInt( _listPeer[static_cast<size_t>( player )]._localAdvantage );
        }
        // 확인되지 않은 내 입력은 (예측 + 지연) × 2 + 1 프레임을 넘지 않는다(static_assert). 그래서 예산(1024 B)에 닿지 않는다.
        NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
        budget.reserveBits( writer.getBitCount() );
        (void)_sendWindow.write( writer, budget ); // 실은 수는 쓰지 않는다. 예산이 넘치지 않기 때문이다(위 주석)
        (void)_messageWriter.sendToPeers( *_pHost, NetChannelType::Unreliable );
    }

    bool RollbackSession::advanceFrame( uint8 localInput )
    {
        if ( _pGame == nullptr )
            return false;
        receiveInput( _localPlayer, _frame + _settings._inputDelay, localInput );
        (void)_sendWindow.push( static_cast<uint32>( _frame + _settings._inputDelay ), &localInput, 1 ); // 멈춘 프레임에 다시 내면 처음 값이 남는다
        sendLocalInputs();
        if ( _pendingRollbackFrame >= 0 )
        {
            rollbackTo( _pendingRollbackFrame );
            _pendingRollbackFrame = -1;
        }
        // 너무 앞서면 기다린다. 예측이 길수록 되감기가 길어지고 화면이 튀기 때문이다.
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            if ( _frame - getConfirmedInputFrame( player ) > _settings._maxPrediction )
            {
                ++_stallCount;
                return false;
            }
        }
        if ( _settings._maxFrameAdvantage > 0 && computeFrameLead() > _settings._maxFrameAdvantage )
        {
            ++_timeSyncWaitCount;
            return false;
        }
        FrameRecord& record = _listRecord.acquire( static_cast<uint32>( _frame ) );
        _pGame->saveState( record._listState );
        record._listInput.resize( static_cast<size_t>( _playerCount ) );
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            record._listInput[static_cast<size_t>( player )] = predictInput( player, _frame );
        }
        _pGame->advanceFrame( record._listInput, false );
        ++_frame;
        moveReceiveWindows();
        return true;
    }

    uint16 RollbackSession::getMessageKindMask() const
    {
        return 1u << ( NetLockstepMessage::kRollbackInput - NetKitMessageRange::kLockstep );
    }

    NetHandleResult RollbackSession::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        const int32 player      = static_cast<int32>( body.readVarUint() );
        const int32 remoteFrame = static_cast<int32>( body.readVarUint() );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        const bool bIgnoreInput = player < 0 || player >= _playerCount || player == _localPlayer;
        if ( bIgnoreInput )
            return NetHandleResult::Handled; // 시작 전이거나 다른 판의 메시지이다
        // 서버는 클라이언트가 자기 번호로만 보내게 한다. 다른 플레이어의 입력을 위조하지 못하게 하려는 것이다.
        const bool bForgedPlayerNumber = _pHost != nullptr && _pHost->isServer() && player != context._connectionID + 1;
        if ( bForgedPlayerNumber )
            return NetHandleResult::Handled;
        int32 ackedLocalFrame = -1;
        int32 advantage       = 0;
        for ( int32 index = 0; index < _playerCount; ++index )
        {
            const int32 acked         = static_cast<int32>( body.readVarUint() ) - 1;
            const int32 peerAdvantage = static_cast<int32>( body.readVarInt() );
            if ( index == _localPlayer )
            {
                ackedLocalFrame = acked;
                advantage       = peerAdvantage;
            }
        }
        // 입력 묶음이 손상됐으면 하나도 넣지 않는다. 창 밖이거나 이미 받은 프레임은 버퍼가 버린다.
        NetInputReceiveBuffer& buffer = _listReceive[static_cast<size_t>( player )];
        if ( body.hasOverflowed() || buffer.read( body, &_listNewFrameScratch ) == false )
            return NetHandleResult::Malformed;
        PeerState& peer = _listPeer[static_cast<size_t>( player )];
        // 확인은 늘어나기만 한다. 비신뢰 채널이라 늦게 도착한 오래된 메시지가 있을 수 있다. 내가 아직 만들지 않은 프레임은 확인할 수 없다.
        peer._ackedLocalFrame = MathUtil::max( peer._ackedLocalFrame, MathUtil::min( ackedLocalFrame, getConfirmedInputFrame( _localPlayer ) ) );
        if ( remoteFrame >= peer._reportedFrame )
        {
            peer._reportedFrame   = remoteFrame;
            peer._localAdvantage  = _frame - remoteFrame;
            peer._remoteAdvantage = advantage;
        }
        for ( const uint32 newFrame : _listNewFrameScratch )
        {
            markMisprediction( player, static_cast<int32>( newFrame ), buffer.find( newFrame )->_bytes[0] );
        }
        if ( _pHost != nullptr )
            (void)NetMessageRouter::relayToOtherPeers( *_pHost, context );
        return NetHandleResult::Handled;
    }
} // namespace sw
