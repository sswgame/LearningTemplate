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
    {
    }

    void RollbackSession::initialize( NetHost* pHost, IRollbackGame* pGame, int32 playerCount, int32 localPlayer, const RollbackSettings& settings )
    {
        _pHost                   = pHost;
        _pGame                   = pGame;
        _playerCount             = MathUtil::max( 1, playerCount );
        _localPlayer             = localPlayer;
        _settings                = settings;
        _settings._maxPrediction = MathUtil::clamp( _settings._maxPrediction, 1, kHistorySize / 2 );
        _listRecord.assign( static_cast<size_t>( kHistorySize ), FrameRecord{} );
        _arrInput.assign( static_cast<size_t>( _playerCount ), vector<int16>( static_cast<size_t>( kHistorySize ), static_cast<int16>( -1 ) ) );
        _arrInputFrame.assign( static_cast<size_t>( _playerCount ), vector<int32>( static_cast<size_t>( kHistorySize ), -1 ) );
        _listConfirmed.assign( static_cast<size_t>( _playerCount ), -1 );
        _frame                 = 0;
        _pendingRollbackFrame  = -1;
        _rollbackCount         = 0;
        _resimulatedFrameCount = 0;
        _stallCount            = 0;
        // 지연 프레임들은 모두 중립 입력 — 모두가 아는 값이라 확인된 것으로 둔다.
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

    void RollbackSession::receiveInput( int32 player, int32 frame, uint8 input )
    {
        if ( player < 0 || player >= _playerCount || frame < 0 || frame < _frame - kHistorySize / 2 )
            return;
        const size_t slot = static_cast<size_t>( frame % kHistorySize );
        if ( _arrInputFrame[static_cast<size_t>( player )][slot] == frame )
            return; // 이미 있다(겹쳐 온 것)
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
        for ( int32 candidate = frame; candidate >= MathUtil::max( 0, frame - kHistorySize / 2 ); --candidate )
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
        const int32 latest = _listConfirmed[static_cast<size_t>( _localPlayer )];
        const int32 first  = MathUtil::max( 0, latest - _settings._redundancy + 1 );
        BitWriter   writer;
        writer.writeBits( NetLockstepMessage::kRollbackInput, 8 );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
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

    bool RollbackSession::handleMessage( int32 connectionId, const vector<uint8>& buffer )
    {
        if ( buffer.empty() || buffer[0] != NetLockstepMessage::kRollbackInput )
            return false;
        BitReader   reader( buffer.data() + 1, static_cast<int32>( buffer.size() ) - 1 );
        const int32 player = static_cast<int32>( reader.readVarUint() );
        const int32 first  = static_cast<int32>( reader.readVarUint() );
        const int32 count  = static_cast<int32>( reader.readVarUint() );
        if ( reader.hasOverflowed() || count < 0 || count > 64 )
            return true;
        if ( _pHost != nullptr && _pHost->isServer() && player != connectionId + 1 )
            return true;
        for ( int32 index = 0; index < count; ++index )
        {
            const uint8 input = static_cast<uint8>( reader.readBits( 8 ) );
            if ( reader.hasOverflowed() )
                return true;
            receiveInput( player, first + index, input );
        }
        if ( _pHost != nullptr && _pHost->isServer() )
            (void)_pHost->broadcast( NetChannelType::Unreliable, buffer.data(), static_cast<int32>( buffer.size() ), connectionId );
        return true;
    }
} // namespace sw
