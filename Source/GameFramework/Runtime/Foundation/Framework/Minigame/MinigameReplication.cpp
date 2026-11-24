#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameReplication.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    namespace
    {
        struct MinigameReplicationInternal
        {
            static void writeFloat3( BitWriter& writer, const float3& value )
            {
                writer.writeFloat( value._x );
                writer.writeFloat( value._y );
                writer.writeFloat( value._z );
            }

            static float3 readFloat3( BitReader& reader )
            {
                float3 value{};
                value._x = reader.readFiniteFloat();
                value._y = reader.readFiniteFloat();
                value._z = reader.readFiniteFloat();
                return value;
            }
        };
    } // namespace

    bool MinigameNetMessage::isValidName( string_view name )
    {
        if ( name.empty() || name.size() > static_cast<size_t>( kMaxNameLength ) )
            return false;
        for ( const char letter : name )
        {
            const bool bAllowed = ( letter >= 'a' && letter <= 'z' ) || ( letter >= '0' && letter <= '9' ) || letter == '_';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }
} // namespace sw

namespace sw
{
    MinigameReplicationServer::MinigameReplicationServer()
        : _listEntry{}
        , _listPending{}
        , _listJoiningConnection{}
        , _writer{}
        , _pPeers{ nullptr }
        , _nextEntryID{ 1 }
        , _bChanged{ SW_FALSE }
    {
    }

    void MinigameReplicationServer::initialize( INetPeers* pPeers )
    {
        _pPeers = pPeers;
        _listEntry.clear();
        _listPending.clear();
        _listJoiningConnection.clear();
        _bChanged = SW_FALSE;
    }

    bool MinigameReplicationServer::addEntry( string_view name, const SubLevelPlacement& placement, bool bReleased, int64 deadlineNanoseconds )
    {
        const bool bRefused = MinigameNetMessage::isValidName( name ) == false || _listEntry.size() >= MinigameNetMessage::kMaxEntry || _pPeers == nullptr ||
                              VectorUtil::findBy<&MinigameNetEntry::_name>( _listEntry, string( name ) ) != nullptr;
        if ( bRefused )
        {
            SW_LOG_ERROR_ID( "MinigameReplication.EntryRefused", "Minigame '%#' is not replicated - its name is not [a-z0-9_], it is already listed, or %# sub-levels are listed (limit %#)", name,
                             static_cast<uint32>( _listEntry.size() ), MinigameNetMessage::kMaxEntry );
            return false;
        }
        MinigameNetEntry entry;
        entry._name      = string( name );
        entry._placement = placement;
        entry._entryID   = _nextEntryID++;
        entry._bReleased = bReleased ? SW_TRUE : SW_FALSE;
        Pending pending;
        pending._deadlineNanoseconds = deadlineNanoseconds;
        if ( bReleased == false )
            _pPeers->collectInWorld( pending._listConnection );
        _listEntry.push_back( std::move( entry ) );
        _listPending.push_back( std::move( pending ) );
        _bChanged = SW_TRUE;
        return true;
    }

    void MinigameReplicationServer::removeEntry( string_view name )
    {
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._name != name )
                continue;
            _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) );
            _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( index ) );
            _bChanged = SW_TRUE;
            return;
        }
    }

    bool MinigameReplicationServer::isReady( string_view name, int64 nowNanoseconds ) const
    {
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._name == name )
                return _listPending[index]._listConnection.empty() || nowNanoseconds >= _listPending[index]._deadlineNanoseconds;
        }
        return false;
    }

    void MinigameReplicationServer::releaseEntry( string_view name )
    {
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._name != name || _listEntry[index]._bReleased == SW_TRUE )
                continue;
            if ( _listPending[index]._listConnection.empty() == false )
                SW_LOG_WARNING_ID( "MinigameReplication.NotEveryoneReady", "Minigame '%#' starts while %# connections have not read its map - they add it when they have", name,
                                   static_cast<uint32>( _listPending[index]._listConnection.size() ) );
            _listPending[index]._listConnection.clear();
            _listEntry[index]._bReleased = SW_TRUE;
            _bChanged                    = SW_TRUE;
        }
    }

    void MinigameReplicationServer::update()
    {
        if ( _pPeers == nullptr || ( _bChanged == SW_FALSE && _listJoiningConnection.empty() ) )
            return;
        writeState();
        if ( _bChanged == SW_TRUE )
            (void)_writer.broadcastMessage( *_pPeers ); // 방금 들어온 연결도 월드 안이라 함께 받는다
        else
        {
            for ( const int32 connectionID : _listJoiningConnection )
            {
                (void)_writer.sendMessage( *_pPeers, connectionID ); // 그새 떠났으면 보낼 곳이 없다
            }
        }
        _bChanged = SW_FALSE;
        _listJoiningConnection.clear();
    }

    void MinigameReplicationServer::writeState()
    {
        using Internal    = MinigameReplicationInternal;
        BitWriter& writer = _writer.beginMessage<MinigameNetMessage::Table, MinigameNetMessage::State>();
        writer.writeVarUint( _listEntry.size() );
        for ( const MinigameNetEntry& entry : _listEntry )
        {
            writer.writeVarUint( entry._entryID );
            writer.writeBool( entry._bReleased == SW_TRUE );
            writer.writeBlob( reinterpret_cast<const uint8*>( entry._name.data() ), static_cast<int32>( entry._name.size() ) );
            Internal::writeFloat3( writer, entry._placement._position );
            Internal::writeFloat3( writer, entry._placement._rotation );
        }
    }

    NetHandleResult MinigameReplicationServer::receive( MinigameNetMessage::Ready, const NetMessageContext& context, BitReader& body )
    {
        const uint32 entryID = body.readVarUint32();
        const bool   bLoaded = body.readBool();
        if ( body.readEnd() == false )
            return NetHandleResult::Malformed;
        // 기다리던 연결의 답만 센다 — 모르는 번호(이미 나온 하위 레벨) · 두 번째 답은 할 일이 없다.
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._entryID != entryID )
                continue;
            const bool bWaited = VectorUtil::removeFirst( _listPending[index]._listConnection, context._connectionID );
            if ( bWaited && bLoaded == false )
                SW_LOG_WARNING_ID( "MinigameReplication.ClientNotLoaded", "Connection %# could not add minigame '%#' - it plays on without it", context._connectionID, _listEntry[index]._name );
            break;
        }
        return NetHandleResult::Handled;
    }

    void MinigameReplicationServer::onPeerEnteredWorld( int32 connectionID )
    {
        _listJoiningConnection.push_back( connectionID );
        for ( size_t index = 0; index < _listEntry.size(); ++index )
        {
            if ( _listEntry[index]._bReleased == SW_FALSE && VectorUtil::contains( _listPending[index]._listConnection, connectionID ) == false )
                _listPending[index]._listConnection.push_back( connectionID );
        }
    }

    void MinigameReplicationServer::onPeerLeftWorld( int32 connectionID )
    {
        (void)VectorUtil::removeFirst( _listJoiningConnection, connectionID );
        for ( Pending& pending : _listPending )
        {
            (void)VectorUtil::removeFirst( pending._listConnection, connectionID );
        }
    }
} // namespace sw

namespace sw
{
    MinigameReplicationClient::MinigameReplicationClient()
        : _listEntry{}
        , _writer{}
        , _pPeers{ nullptr }
        , _bKnown{ SW_FALSE }
    {
    }

    void MinigameReplicationClient::initialize( INetPeers* pPeers )
    {
        _pPeers = pPeers;
        _listEntry.clear();
        _bKnown = SW_FALSE;
    }

    void MinigameReplicationClient::sendReady( uint32 entryID, bool bLoaded )
    {
        if ( _pPeers == nullptr )
            return;
        BitWriter& writer = _writer.beginMessage<MinigameNetMessage::Table, MinigameNetMessage::Ready>();
        writer.writeVarUint( entryID );
        writer.writeBool( bLoaded );
        (void)_writer.sendMessage( *_pPeers, 0 ); // 끊겼으면 보낼 곳이 없다 — 서버는 떠난 연결을 기다리지 않는다
    }

    NetHandleResult MinigameReplicationClient::receive( MinigameNetMessage::State, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        using Internal                 = MinigameReplicationInternal;
        const uint32             count = body.readCount( MinigameNetMessage::kMaxEntry );
        vector<MinigameNetEntry> listEntry;
        vector<uint8>            nameBytes;
        for ( uint32 index = 0; index < count; ++index )
        {
            MinigameNetEntry entry;
            entry._entryID   = body.readVarUint32();
            entry._bReleased = body.readBool() ? SW_TRUE : SW_FALSE;
            if ( body.readBlob( nameBytes, MinigameNetMessage::kMaxNameLength ) == false )
                return NetHandleResult::Malformed;
            const string_view name{ reinterpret_cast<const utf8*>( nameBytes.data() ), nameBytes.size() };
            entry._placement._position = Internal::readFloat3( body );
            entry._placement._rotation = Internal::readFloat3( body );
            const bool bTwice          = VectorUtil::findBy<&MinigameNetEntry::_entryID>( listEntry, entry._entryID ) != nullptr;
            if ( entry._entryID == 0 || bTwice || MinigameNetMessage::isValidName( name ) == false )
                return NetHandleResult::Malformed; // 이름은 파일 경로가 된다 — 정해진 글자 밖이면 받지 않는다
            entry._name = string( name );
            listEntry.push_back( std::move( entry ) );
        }
        if ( body.readEnd() == false )
            return NetHandleResult::Malformed;
        _listEntry = std::move( listEntry );
        _bKnown    = SW_TRUE;
        return NetHandleResult::Handled;
    }

    void MinigameReplicationClient::onWorldOpened( uint32 generation )
    {
        (void)generation;
        _listEntry.clear();
        _bKnown = SW_FALSE;
    }
} // namespace sw
