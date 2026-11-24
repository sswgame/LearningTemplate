/**
 * @file FakeNetPeers.h
 * @brief 키트 · 게임의 단위 테스트가 함께 쓰는 가짜 상대입니다 — 보낸 메시지를 쌓기만 하고, 신뢰 창의 규칙은 엔드포인트(`NetEndpoint`)와 같습니다(망 · 스레드 · 시각 없음).
 * @details 두 보내기를 같게 둔 가짜는 "기다릴 쪽이 닫는 보내기를 쓰는" 결함을 가린다. 여기서는 `trySendMessage` 는 나눠 보내는 쪽의 몫(`NetConnection::kReliableBulkWindow`)이 차면
 *          거절하고, `sendMessage` 는 창이 차면 그 연결을 닫는다. 쌓인 메시지(`_listSent`)가 곧 확인되지 않은 메시지이고, 꺼내면(`takeSent`) 상대가 받은 것이다.
 *          붙지 않은 연결에는 보내지 못하고 주체도 없다. 망을 거치는 테스트는 `LoopbackCluster` 를 쓴다.
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"

namespace sw::test
{
    /** @class FakeNetPeers @brief 붙은 연결을 테스트가 정하는 상대입니다. */
    class FakeNetPeers final : public INetPeers
    {
    public:
        struct Sent
        {
            vector<uint8>  _bytes{};
            int32          _connectionID{ -1 };
            NetChannelKind _channel{ NetChannelKind::Unreliable };
        };

        vector<Sent>    _listSent{};             ///< 보내고 상대가 아직 받지 않은 메시지
        vector<int32>   _listDisconnected{};     ///< 이쪽이 닫은 연결(창이 찬 `sendMessage` · `disconnect`), 차례대로
        optional<int32> _clientIndex{};          ///< 클라이언트 — 서버가 준 자기 번호
        int32           _heldFragmentCount{ 0 }; ///< 연결마다 창을 차지한 그 밖의 조각 수 — `NetConnection::kReliableWindow` 면 창이 찬 것이다

        explicit FakeNetPeers( bool bServer )
            : _listLink{}
            , _bServer{ static_cast<uint8>( bServer ? SW_TRUE : SW_FALSE ) }
        {
        }

        /** @brief 연결이 붙었습니다(@p principalID 는 접속 인증기가 준 계정, 0 은 없음). 닫힌 번호도 다시 붙일 수 있습니다. */
        void connect( int32 connectionID, uint64 principalID = 0 )
        {
            drop( connectionID );
            _listLink.push_back( Link{ principalID, connectionID } );
        }
        /** @brief 붙어 서버의 월드에 들어왔습니다. */
        void connectInWorld( int32 connectionID, uint64 principalID = 0 )
        {
            connect( connectionID, principalID );
            setInWorld( connectionID, true );
        }
        /** @brief 연결이 끊겼습니다(상대가). */
        void drop( int32 connectionID )
        {
            (void)VectorUtil::removeIf( _listLink, [connectionID]( const Link& link )
            { return link._connectionID == connectionID; } );
            setInWorld( connectionID, false );
        }
        /** @brief 쌓인 메시지를 모두 꺼냅니다 — 상대가 받아 확인했다. */
        [[nodiscard]] vector<Sent> takeSent()
        {
            vector<Sent> listSent = std::move( _listSent );
            _listSent.clear();
            return listSent;
        }

        bool sendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override
        {
            if ( isReliable( channel ) && findLink( connectionID ) != nullptr && computePendingFragmentCount( connectionID ) + computeFragmentCount( size ) > NetConnection::kReliableWindow - 1 )
            {
                disconnect( connectionID, NetLinger::none() );
                return false;
            }
            return queue( connectionID, channel, pData, size );
        }
        bool trySendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override
        {
            if ( isReliable( channel ) && computePendingFragmentCount( connectionID ) + computeFragmentCount( size ) > NetConnection::kReliableBulkWindow )
                return false;
            return queue( connectionID, channel, pData, size );
        }
        bool  sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 ) override { return queue( connectionID, NetChannelKind::Unreliable, pData, size ); }
        int32 broadcast( NetChannelKind channel, const uint8* pData, int32 size, int32 exceptID ) override
        {
            vector<int32> listConnection; // 창이 차 닫힌 연결은 목록에서 빠진다
            collectConnected( listConnection );
            int32 count = 0;
            for ( const int32 connectionID : listConnection )
            {
                if ( connectionID != exceptID && sendMessage( connectionID, channel, pData, size ) )
                    ++count;
            }
            return count;
        }
        void disconnect( int32 connectionID, NetLinger ) override
        {
            if ( findLink( connectionID ) == nullptr )
                return;
            _listDisconnected.push_back( connectionID );
            drop( connectionID );
        }
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return _bServer != SW_FALSE; }
        NetConnectionState getConnectionState( int32 connectionID ) const override { return findLink( connectionID ) != nullptr ? NetConnectionState::Connected : NetConnectionState::Disconnected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 connectionID ) const override
        {
            const Link* pLink = findLink( connectionID );
            return pLink != nullptr ? pLink->_principalID : 0;
        }
        optional<int32> getClientIndex() const override { return _clientIndex; }
        int32           getConnectedCount() const override { return static_cast<int32>( _listLink.size() ); }
        void            collectConnected( vector<int32>& outListConnection ) const override
        {
            outListConnection.clear();
            for ( const Link& link : _listLink )
            {
                outListConnection.push_back( link._connectionID );
            }
        }
        bool isConnectionSaturated( int32 ) const override { return false; }
        void addSentKindStats( NetMessageKindStatsTable& ) const override {}

    private:
        struct Link
        {
            uint64 _principalID{ 0 };
            int32  _connectionID{ -1 };
        };

        static bool  isReliable( NetChannelKind channel ) { return channel == NetChannelKind::ReliableOrdered || channel == NetChannelKind::ReliableUnordered; }
        static int32 computeFragmentCount( int32 size ) { return size > NetConnection::kMaxSingleMessageSize ? ( size + NetConnection::kMaxSingleMessageSize - 1 ) / NetConnection::kMaxSingleMessageSize : 1; }
        const Link*  findLink( int32 connectionID ) const { return VectorUtil::findBy<&Link::_connectionID>( _listLink, connectionID ); }
        /** @brief 이 연결의 확인되지 않은 신뢰 조각 수입니다. */
        int32 computePendingFragmentCount( int32 connectionID ) const
        {
            int32 count = _heldFragmentCount;
            for ( const Sent& sent : _listSent )
            {
                if ( sent._connectionID == connectionID && isReliable( sent._channel ) )
                    count += computeFragmentCount( static_cast<int32>( sent._bytes.size() ) );
            }
            return count;
        }
        bool queue( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size )
        {
            if ( findLink( connectionID ) == nullptr )
                return false;
            _listSent.push_back( Sent{ vector<uint8>( pData, pData + size ), connectionID, channel } );
            return true;
        }

    private:
        vector<Link> _listLink;
        uint8        _bServer;
    };
} // namespace sw::test
