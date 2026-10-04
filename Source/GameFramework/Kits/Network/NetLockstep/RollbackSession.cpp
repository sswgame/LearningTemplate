#include "pch.h"

#include "GameFramework/Kits/Network/NetLockstep/RollbackSession.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/Kits/Network/NetLockstep/LockstepSession.h"

namespace sw
{
    RollbackSession::RollbackSession()
        : _listRecord{}
        , _arrInput{}
        , _arrInputFrame{}
        , _listConfirmed{}
        , _listPeer{}
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

    void RollbackSession::initialize( NetHost* pHost, IRollbackGame* pGame, int32 playerCount, int32 localPlayer, const RollbackSettings& settings )
    {
        _pHost                   = pHost;
        _pGame                   = pGame;
        _playerCount             = MathUtil::max( 1, playerCount );
        _localPlayer             = localPlayer;
        _settings                = settings;
        _settings._inputDelay    = MathUtil::clamp( _settings._inputDelay, 0, kMaxInputDelay );
        _settings._maxPrediction = MathUtil::clamp( _settings._maxPrediction, 1, kMaxPrediction );
        _listRecord.assign( static_cast<size_t>( kHistorySize ), FrameRecord{} );
        _arrInput.assign( static_cast<size_t>( _playerCount ), vector<int16>( static_cast<size_t>( kHistorySize ), static_cast<int16>( -1 ) ) );
        _arrInputFrame.assign( static_cast<size_t>( _playerCount ), vector<int32>( static_cast<size_t>( kHistorySize ), -1 ) );
        _listConfirmed.assign( static_cast<size_t>( _playerCount ), -1 );
        // 지연 프레임들은 모두 중립 입력 — 모두가 아는 값이라 상대도 이미 확인한 것으로 둔다.
        PeerState peer;
        peer._ackedLocalFrame = _settings._inputDelay - 1;
        _listPeer.assign( static_cast<size_t>( _playerCount ), peer );
        _frame                 = 0;
        _pendingRollbackFrame  = -1;
        _rollbackCount         = 0;
        _resimulatedFrameCount = 0;
        _stallCount            = 0;
        _timeSyncWaitCount     = 0;
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            for ( int32 frame = 0; frame < _settings._inputDelay; ++frame )
                receiveInput( player, frame, 0 );
        }
    }

    int32 RollbackSession::getConfirmedFrame() const
    {
        int32 confirmed = _frame;
        for ( const int32 frame : _listConfirmed )
            confirmed = MathUtil::min( confirmed, frame );
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
            // 두 이점에는 같은 망 지연이 들어 있다 — 빼면 지연은 지워지고 프레임 차이의 두 배가 남는다.
            lead = MathUtil::max( lead, ( peer._localAdvantage - peer._remoteAdvantage ) / 2 );
        }
        return lead;
    }

    void RollbackSession::receiveInput( int32 player, int32 frame, uint8 input )
    {
        const bool bInWindow = _frame - kFrameWindow <= frame && frame < _frame + kFrameWindow;
        if ( player < 0 || player >= _playerCount || frame < 0 || bInWindow == false )
            return;
        const size_t slot = static_cast<size_t>( frame % kHistorySize );
        if ( _arrInputFrame[static_cast<size_t>( player )][slot] == frame )
            return; // 이미 있다(다시 보낸 것)
        _arrInputFrame[static_cast<size_t>( player )][slot] = frame;
        _arrInput[static_cast<size_t>( player )][slot]      = static_cast<int16>( input );
        // 이미 예측으로 흘린 프레임인데 다르면 되감아야 한다.
        if ( frame < _frame )
        {
            const FrameRecord& record = _listRecord[slot];
            if ( record._frame == frame && record._listInput.size() == static_cast<size_t>( _playerCount ) &&
                 record._listInput[static_cast<size_t>( player )] != input )
                _pendingRollbackFrame = _pendingRollbackFrame < 0 ? frame : MathUtil::min( _pendingRollbackFrame, frame );
        }
        int32& confirmed = _listConfirmed[static_cast<size_t>( player )];
        while ( _arrInputFrame[static_cast<size_t>( player )][static_cast<size_t>( ( confirmed + 1 ) % kHistorySize )] == confirmed + 1 )
            ++confirmed;
    }

    uint8 RollbackSession::predictInput( int32 player, int32 frame ) const
    {
        // 그 프레임 것이 있으면 그것, 없으면 가장 최근에 받은 것(같은 버튼을 계속 누르고 있다고 본다).
        for ( int32 candidate = frame; candidate >= MathUtil::max( 0, frame - kFrameWindow ); --candidate )
        {
            const size_t slot = static_cast<size_t>( candidate % kHistorySize );
            if ( _arrInputFrame[static_cast<size_t>( player )][slot] == candidate )
                return static_cast<uint8>( _arrInput[static_cast<size_t>( player )][slot] );
        }
        return 0;
    }

    void RollbackSession::rollbackTo( int32 frame )
    {
        FrameRecord& start = _listRecord[static_cast<size_t>( frame % kHistorySize )];
        if ( start._frame != frame )
            return; // 기록 밖 — 고칠 수 없다(최대 예측 안이면 생기지 않는다)
        _pGame->loadState( start._listState );
        ++_rollbackCount;
        for ( int32 replay = frame; replay < _frame; ++replay )
        {
            FrameRecord& record = _listRecord[static_cast<size_t>( replay % kHistorySize )];
            if ( replay != frame )
                _pGame->saveState( record._listState );
            record._frame = replay;
            record._listInput.resize( static_cast<size_t>( _playerCount ) );
            for ( int32 player = 0; player < _playerCount; ++player )
                record._listInput[static_cast<size_t>( player )] = predictInput( player, replay );
            _pGame->advanceFrame( record._listInput, true );
            ++_resimulatedFrameCount;
        }
    }

    void RollbackSession::sendLocalInputs()
    {
        if ( _pHost == nullptr )
            return;
        // 모두가 확인한 다음 프레임부터 — 상대가 아직 못 받은 것은 확인이 오를 때까지 메시지마다 다시 싣는다.
        const int32 latest = _listConfirmed[static_cast<size_t>( _localPlayer )];
        int32       acked  = latest;
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            if ( player != _localPlayer )
                acked = MathUtil::min( acked, _listPeer[static_cast<size_t>( player )]._ackedLocalFrame );
        }
        const int32 first  = MathUtil::max( acked + 1, latest - kFrameWindow + 1 );
        BitWriter&  writer = _messageWriter.begin( NetLockstepMessage::kRollbackInput );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
        writer.writeVarUint( static_cast<uint64>( _frame ) );
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            writer.writeVarUint( static_cast<uint64>( _listConfirmed[static_cast<size_t>( player )] + 1 ) );
            writer.writeVarInt( _listPeer[static_cast<size_t>( player )]._localAdvantage );
        }
        writer.writeVarUint( static_cast<uint64>( first ) );
        writer.writeVarUint( static_cast<uint64>( latest - first + 1 ) );
        for ( int32 frame = first; frame <= latest; ++frame )
            writer.writeBits( predictInput( _localPlayer, frame ), 8 );
        if ( _pHost->isServer() )
            (void)_pHost->broadcast( NetChannelType::Unreliable, writer.getBytes().data(), writer.getByteCount() );
        else
            (void)_pHost->sendMessage( 0, NetChannelType::Unreliable, writer.getBytes() );
    }

    bool RollbackSession::advanceFrame( uint8 localInput )
    {
        if ( _pGame == nullptr )
            return false;
        receiveInput( _localPlayer, _frame + _settings._inputDelay, localInput );
        sendLocalInputs();
        if ( _pendingRollbackFrame >= 0 )
        {
            rollbackTo( _pendingRollbackFrame );
            _pendingRollbackFrame = -1;
        }
        // 너무 앞서면 기다린다(예측이 길수록 되감기가 길고 화면이 튄다).
        for ( int32 player = 0; player < _playerCount; ++player )
        {
            if ( _frame - _listConfirmed[static_cast<size_t>( player )] > _settings._maxPrediction )
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
        FrameRecord& record = _listRecord[static_cast<size_t>( _frame % kHistorySize )];
        _pGame->saveState( record._listState );
        record._frame = _frame;
        record._listInput.resize( static_cast<size_t>( _playerCount ) );
        for ( int32 player = 0; player < _playerCount; ++player )
            record._listInput[static_cast<size_t>( player )] = predictInput( player, _frame );
        _pGame->advanceFrame( record._listInput, false );
        ++_frame;
        return true;
    }

    uint16 RollbackSession::getMessageKindMask() const { return 1u << ( NetLockstepMessage::kRollbackInput - NetKitMessageRange::kLockstep ); }

    NetHandleResult RollbackSession::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        const int32 player      = static_cast<int32>( body.readVarUint() );
        const int32 remoteFrame = static_cast<int32>( body.readVarUint() );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        if ( player < 0 || player >= _playerCount || player == _localPlayer )
            return NetHandleResult::Handled; // 시작 전이거나 남의 판
        // 서버는 클라이언트가 자기 번호로만 보내게 한다(남의 입력을 위조하지 못하게).
        if ( _pHost != nullptr && _pHost->isServer() && player != context._connectionId + 1 )
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
        const int32 first = static_cast<int32>( body.readVarUint() );
        const int32 count = static_cast<int32>( body.readVarUint() );
        if ( body.hasOverflowed() || count < 0 || count > kFrameWindow )
            return NetHandleResult::Malformed;
        uint8 arrInput[kFrameWindow];
        for ( int32 index = 0; index < count; ++index )
            arrInput[index] = static_cast<uint8>( body.readBits( 8 ) );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        PeerState& peer = _listPeer[static_cast<size_t>( player )];
        // 확인은 늘기만 한다(비신뢰라 늦게 온 옛 메시지가 있다). 내가 아직 안 만든 프레임은 확인할 수 없다.
        peer._ackedLocalFrame = MathUtil::max( peer._ackedLocalFrame, MathUtil::min( ackedLocalFrame, _listConfirmed[static_cast<size_t>( _localPlayer )] ) );
        if ( remoteFrame >= peer._reportedFrame )
        {
            peer._reportedFrame   = remoteFrame;
            peer._localAdvantage  = _frame - remoteFrame;
            peer._remoteAdvantage = advantage;
        }
        for ( int32 index = 0; index < count; ++index )
            receiveInput( player, first + index, arrInput[index] );
        if ( _pHost != nullptr && _pHost->isServer() )
            (void)_pHost->broadcast( NetChannelType::Unreliable, context._pMessage, context._messageSize, context._connectionId );
        return NetHandleResult::Handled;
    }
} // namespace sw
