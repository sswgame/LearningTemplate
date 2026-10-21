#include "pch.h"

#include "Core/Network/Connection/NetHostShardSet.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    NetHostShardSet::NetHostShardSet()
        : _listShard{}
        , _shardInbound{}
        , _connectionStride{ 0 }
    {
    }

    NetHostShardSet::~NetHostShardSet() = default;

    bool NetHostShardSet::initialize( const vector<NetHost*>& listShard, int32 connectionStride )
    {
        shutdown();
        if ( listShard.empty() || connectionStride < 1 )
        {
            SW_LOG_ERROR( "NetHostShardSet: %# shard(s) with connection stride %# - a shard set needs at least one shard and a stride of 1 or more",
                          static_cast<int32>( listShard.size() ), connectionStride );
            return false;
        }
        const int64 foldedLimit = static_cast<int64>( listShard.size() ) * static_cast<int64>( connectionStride );
        if ( foldedLimit > static_cast<int64>( MathUtil::kMaxInt32 ) )
        {
            SW_LOG_ERROR( "NetHostShardSet: %# shard(s) x stride %# does not fit a connection ID", static_cast<int32>( listShard.size() ), connectionStride );
            return false;
        }
        for ( const NetHost* pShard : listShard )
        {
            if ( pShard == nullptr )
            {
                SW_LOG_ERROR( "NetHostShardSet: a shard host is missing" );
                return false;
            }
        }
        _listShard        = listShard;
        _connectionStride = connectionStride;
        return true;
    }

    void NetHostShardSet::shutdown()
    {
        _listShard.clear();
        _shardInbound.clear();
        _connectionStride = 0;
    }

    NetHost* NetHostShardSet::findShard( int32 connectionID, int32& outLocalConnectionID ) const
    {
        outLocalConnectionID = -1;
        if ( connectionID < 0 || _connectionStride < 1 )
            return nullptr;
        const int32 shardIndex = connectionID / _connectionStride;
        if ( shardIndex >= getShardCount() )
            return nullptr;
        outLocalConnectionID = connectionID - shardIndex * _connectionStride;
        return _listShard[static_cast<size_t>( shardIndex )];
    }

    bool NetHostShardSet::sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size )
    {
        int32    localConnectionID = -1;
        NetHost* pShard            = findShard( connectionID, localConnectionID );
        return pShard != nullptr && pShard->sendMessage( localConnectionID, channel, pData, size );
    }

    bool NetHostShardSet::sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 tag )
    {
        int32    localConnectionID = -1;
        NetHost* pShard            = findShard( connectionID, localConnectionID );
        return pShard != nullptr && pShard->sendNotifiedMessage( localConnectionID, pData, size, tag );
    }

    int32 NetHostShardSet::broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID )
    {
        int32          exceptLocalID = -1;
        const NetHost* pExceptShard  = findShard( exceptID, exceptLocalID );
        int32          sentCount     = 0;
        for ( NetHost* pShard : _listShard )
        {
            sentCount += pShard->broadcast( channel, pData, size, pShard == pExceptShard ? exceptLocalID : -1 );
        }
        return sentCount;
    }

    void NetHostShardSet::disconnect( int32 connectionID )
    {
        int32    localConnectionID = -1;
        NetHost* pShard            = findShard( connectionID, localConnectionID );
        if ( pShard != nullptr )
            pShard->disconnect( localConnectionID );
    }

    void NetHostShardSet::drainInbound( NetInbound& outInbound )
    {
        outInbound.clear();
        for ( int32 shardIndex = 0; shardIndex < getShardCount(); ++shardIndex )
        {
            _listShard[static_cast<size_t>( shardIndex )]->drainInbound( _shardInbound );
            for ( NetHostEvent event : _shardInbound._listEvent )
            {
                event._connectionID = makeConnectionID( shardIndex, event._connectionID );
                outInbound._listEvent.push_back( event );
            }
            // 바이트는 한 아레나로 옮기고 메시지 범위를 그만큼 민다 — 사건은 모두 메시지 목록과 따로 쌓여 사건이 메시지보다 앞에 오는 약속이 그대로다.
            const int32 byteBase = static_cast<int32>( outInbound._bytes.size() );
            outInbound._bytes.insert( outInbound._bytes.end(), _shardInbound._bytes.begin(), _shardInbound._bytes.end() );
            for ( NetInboundMessage message : _shardInbound._listMessage )
            {
                message._connectionID = makeConnectionID( shardIndex, message._connectionID );
                message._offset += byteBase;
                outInbound._listMessage.push_back( message );
            }
            for ( NetDeliveryNotice notice : _shardInbound._listDeliveryNotice )
            {
                notice._connectionID = makeConnectionID( shardIndex, notice._connectionID );
                outInbound._listDeliveryNotice.push_back( notice );
            }
        }
    }

    bool NetHostShardSet::isServer() const
    {
        return _listShard.empty() == false && _listShard.front()->isServer();
    }

    NetConnectionState NetHostShardSet::getConnectionState( int32 connectionID ) const
    {
        int32          localConnectionID = -1;
        const NetHost* pShard            = findShard( connectionID, localConnectionID );
        return pShard != nullptr ? pShard->getConnectionState( localConnectionID ) : NetConnectionState::Disconnected;
    }

    bool NetHostShardSet::getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const
    {
        int32          localConnectionID = -1;
        const NetHost* pShard            = findShard( connectionID, localConnectionID );
        return pShard != nullptr && pShard->getConnectionStats( localConnectionID, outStats );
    }

    NetAddress NetHostShardSet::getConnectionAddress( int32 connectionID ) const
    {
        int32          localConnectionID = -1;
        const NetHost* pShard            = findShard( connectionID, localConnectionID );
        return pShard != nullptr ? pShard->getConnectionAddress( localConnectionID ) : NetAddress{};
    }

    int32 NetHostShardSet::getConnectedCount() const
    {
        int32 connectedCount = 0;
        for ( const NetHost* pShard : _listShard )
        {
            connectedCount += pShard->getConnectedCount();
        }
        return connectedCount;
    }

    void NetHostShardSet::collectConnected( vector<int32>& outListConnection ) const
    {
        outListConnection.clear();
        vector<int32> listShardConnection;
        for ( int32 shardIndex = 0; shardIndex < getShardCount(); ++shardIndex )
        {
            _listShard[static_cast<size_t>( shardIndex )]->collectConnected( listShardConnection );
            for ( const int32 localConnectionID : listShardConnection )
            {
                outListConnection.push_back( makeConnectionID( shardIndex, localConnectionID ) );
            }
        }
    }

    int32 NetHostShardSet::getMaxBytesPerSecond() const
    {
        return _listShard.empty() ? 0 : _listShard.front()->getMaxBytesPerSecond();
    }

    bool NetHostShardSet::isConnectionSaturated( int32 connectionID ) const
    {
        int32          localConnectionID = -1;
        const NetHost* pShard            = findShard( connectionID, localConnectionID );
        return pShard != nullptr && pShard->isConnectionSaturated( localConnectionID );
    }

    void NetHostShardSet::addSentKindTally( NetMessageKindTallyTable& outTable ) const
    {
        for ( const NetHost* pShard : _listShard )
        {
            pShard->addSentKindTally( outTable );
        }
    }
} // namespace sw
