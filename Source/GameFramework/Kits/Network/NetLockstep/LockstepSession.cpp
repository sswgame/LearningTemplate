#include "pch.h"

#include "GameFramework/Kits/Network/NetLockstep/LockstepSession.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    LockstepSession::LockstepSession()
        : _mapInput{}
        , _mapHasInput{}
        , _mapChecksum{}
        , _pHost{ nullptr }
        , _playerCount{ 0 }
        , _localPlayer{ 0 }
        , _inputDelay{ 0 }
        , _stallCount{ 0 }
        , _currentTick{ 0 }
        , _nextLocalTick{ 0 }
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
        _inputDelay  = MathUtil::max( 0, inputDelay );
        _mapInput.clear();
        _mapHasInput.clear();
        _mapChecksum.clear();
        _stallCount  = 0;
        _currentTick = 0;
        _desyncTick  = 0;
        _bDesynced   = SW_FALSE;
        // 처음 지연 틱들은 모두 빈 입력 — 그동안 첫 입력이 망을 건넌다.
        for ( uint32 tick = 0; tick < static_cast<uint32>( _inputDelay ); ++tick )
        {
            for ( int32 player = 0; player < _playerCount; ++player )
                storeInput( player, tick, vector<uint8>{} );
        }
        _nextLocalTick = static_cast<uint32>( _inputDelay );
    }

    void LockstepSession::storeInput( int32 player, uint32 tick, const vector<uint8>& listInput )
    {
        if ( player < 0 || player >= _playerCount || tick < _currentTick )
            return;
        vector<vector<uint8>>& listInputOfTick = _mapInput[tick];
        vector<uint8>&         listHas         = _mapHasInput[tick];
        if ( listInputOfTick.empty() )
        {
            listInputOfTick.resize( static_cast<size_t>( _playerCount ) );
            listHas.assign( static_cast<size_t>( _playerCount ), SW_FALSE );
        }
        listInputOfTick[static_cast<size_t>( player )] = listInput;
        listHas[static_cast<size_t>( player )]         = SW_TRUE;
    }

    void LockstepSession::submitLocalInput( const vector<uint8>& listInput )
    {
        const uint32 tick = _nextLocalTick++;
        storeInput( _localPlayer, tick, listInput );
        if ( _pHost == nullptr )
            return;
        BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kInput );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
        writer.writeVarUint( tick );
        writer.writeVarUint( listInput.size() );
        if ( listInput.empty() == false )
            writer.writeBytes( listInput.data(), static_cast<int32>( listInput.size() ) );
        if ( _pHost->isServer() )
            (void)_pHost->broadcast( NetChannelType::ReliableOrdered, writer.getBytes().data(), writer.getByteCount() );
        else
            (void)_pHost->sendMessage( 0, NetChannelType::ReliableOrdered, writer.getBytes() );
    }

    bool LockstepSession::tryAdvance( vector<vector<uint8>>& outListInput )
    {
        const auto hasIter = _mapHasInput.find( _currentTick );
        bool       bReady  = hasIter != _mapHasInput.end();
        if ( bReady )
        {
            for ( const uint8 bHas : hasIter->second )
                bReady = bReady && bHas != SW_FALSE;
        }
        if ( bReady == false )
        {
            ++_stallCount;
            return false;
        }
        outListInput = std::move( _mapInput[_currentTick] );
        _mapInput.erase( _currentTick );
        _mapHasInput.erase( _currentTick );
        ++_currentTick;
        return true;
    }

    void LockstepSession::storeChecksum( int32 player, uint32 tick, uint32 checksum )
    {
        if ( player < 0 || player >= _playerCount || _bDesynced )
            return;
        vector<int64>& listChecksum = _mapChecksum[tick];
        if ( listChecksum.empty() )
            listChecksum.assign( static_cast<size_t>( _playerCount ), -1 );
        listChecksum[static_cast<size_t>( player )] = static_cast<int64>( checksum );
        bool bAll                                   = true;
        for ( const int64 value : listChecksum )
            bAll = bAll && value >= 0;
        if ( bAll == false )
            return;
        for ( const int64 value : listChecksum )
        {
            if ( value != listChecksum[0] )
            {
                _bDesynced  = SW_TRUE;
                _desyncTick = tick;
                break;
            }
        }
        _mapChecksum.erase( tick );
    }

    void LockstepSession::reportChecksum( uint32 tick, uint32 checksum )
    {
        storeChecksum( _localPlayer, tick, checksum );
        if ( _pHost == nullptr )
            return;
        BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kChecksum );
        writer.writeVarUint( static_cast<uint64>( _localPlayer ) );
        writer.writeVarUint( tick );
        writer.writeUint32( checksum );
        if ( _pHost->isServer() )
            (void)_pHost->broadcast( NetChannelType::ReliableOrdered, writer.getBytes().data(), writer.getByteCount() );
        else
            (void)_pHost->sendMessage( 0, NetChannelType::ReliableOrdered, writer.getBytes() );
    }

    void LockstepSession::relay( int32 fromConnectionId, const uint8* pData, int32 size )
    {
        if ( _pHost != nullptr && _pHost->isServer() )
            (void)_pHost->broadcast( NetChannelType::ReliableOrdered, pData, size, fromConnectionId );
    }

    bool LockstepSession::handleNetMessage( int32 connectionId, const uint8* pData, int32 size )
    {
        if ( size <= 0 || ( pData[0] != NetLockstepMessage::kInput && pData[0] != NetLockstepMessage::kChecksum ) )
            return false;
        BitReader   reader( pData + 1, size - 1 );
        const int32 player = static_cast<int32>( reader.readVarUint() );
        // 서버는 클라이언트가 자기 번호로만 보내게 한다(남의 입력을 위조하지 못하게).
        if ( _pHost != nullptr && _pHost->isServer() && player != connectionId + 1 )
            return true;
        const uint32 tick = static_cast<uint32>( reader.readVarUint() );
        if ( pData[0] == NetLockstepMessage::kInput )
        {
            vector<uint8> listInput( static_cast<size_t>( MathUtil::min<uint64>( 1024, reader.readVarUint() ) ) );
            if ( listInput.empty() == false && reader.readBytes( listInput.data(), static_cast<int32>( listInput.size() ) ) == false )
                return true;
            if ( reader.hasOverflowed() )
                return true;
            storeInput( player, tick, listInput );
        }
        else
        {
            const uint32 checksum = reader.readUint32();
            if ( reader.hasOverflowed() )
                return true;
            storeChecksum( player, tick, checksum );
        }
        relay( connectionId, pData, size );
        return true;
    }
} // namespace sw
