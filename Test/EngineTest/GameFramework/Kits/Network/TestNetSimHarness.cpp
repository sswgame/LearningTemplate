#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

#include "GameFramework/Kits/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Kits/Network/NetLockstep/LockstepSession.h"
#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/TestFramework.h"

// 가상 서버 하니스 — 서버 월드 1 + 클라이언트 월드 N 이 각자 씬 · 물리를 갖고 연결마다 다른 회선 위에서 돈다. 서버는 떨어지는 상자를
// 시뮬레이션해 자리를 복제하고, 클라이언트는 받은 자리로 자기 월드의 키네마틱 대리 바디를 옮긴다.

using namespace sw;

namespace
{
    constexpr uint32  kCrateCount  = 4;
    constexpr float32 kTickSeconds = 1.0f / 60.0f;

    RigidBodyComponent* spawnBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfExtents, PhysicsBodyType type )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
        if ( pBody == nullptr )
            return nullptr;
        PhysicsShapeDesc3D box;
        box._halfExtents = halfExtents;
        pBody->setShape( box );
        pBody->setBodyType( type );
        pBody->setLocalPosition( position );
        return pBody;
    }

    /** @brief 시험이 서버 쪽에서 본 것입니다. */
    struct CrateServerRecord
    {
        vector<NetHostEvent> _listEvent;
    };

    /** @brief 서버 — 바닥 위로 떨어지는 상자를 시뮬레이션하고 틱마다 자리를 스냅샷으로 보낸다. 오브젝트는 지우지 않으므로 포인터를 든다. */
    class CrateServerSession final : public INetSimSession
    {
    public:
        CrateServerSession( NetSimWorld& world, CrateServerRecord* pRecord )
            : _server{}
            , _listCrate{}
            , _pRecord{ pRecord }
        {
            GameObjectManager& manager = world.getObjectManager();
            (void)spawnBox( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, PhysicsBodyType::Static );
            for ( uint32 index = 0; index < kCrateCount; ++index )
            {
                const float3 position{ static_cast<float32>( index ) * 2.0f - 3.0f, 2.0f + static_cast<float32>( index ), 0.0f };
                const string name = string{ "Crate" } + sw::to_string( index );
                _listCrate.push_back( spawnBox( manager, name.c_str(), position, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Dynamic ) );
            }
            _server.initialize( &world.getHost(), ReplicationServerSettings{}, nullptr );
            world.getRouter().addHandler( &_server );
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            _server.beginTick( world.getLocalTick() );
            for ( uint32 index = 0; index < static_cast<uint32>( _listCrate.size() ); ++index )
            {
                const float3 position = _listCrate[index]->getWorldPosition();
                BitWriter    writer;
                writer.writeFloat( position._x );
                writer.writeFloat( position._y );
                writer.writeFloat( position._z );
                _server.setEntity( index + 1, 1, writer.getBytes() );
            }
            _server.endTick();
            _server.sendSnapshots();
        }

        void onHostEvent( NetSimWorld& world, const NetHostEvent& event ) override
        {
            (void)world;
            if ( _pRecord != nullptr )
                _pRecord->_listEvent.push_back( event );
        }

        const vector<RigidBodyComponent*>& getCrates() const { return _listCrate; }

    private:
        ReplicationServer           _server;
        vector<RigidBodyComponent*> _listCrate;
        CrateServerRecord*          _pRecord;
    };

    /** @brief 클라이언트 — 자기 바닥이 있고, 받은 상자 자리(보간)로 키네마틱 대리 바디를 옮긴다. */
    class CrateClientSession final : public INetSimSession
    {
    public:
        explicit CrateClientSession( NetSimWorld& world )
            : _client{}
            , _listProxy{}
            , _firstSnapshotTick{ -1 }
        {
            _listProxy.assign( kCrateCount, nullptr );
            (void)spawnBox( world.getObjectManager(), "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, PhysicsBodyType::Static );
            ReplicationClientSettings settings;
            settings._tickInterval = kTickSeconds;
            _client.initialize( &world.getHost(), settings );
            world.getRouter().addHandler( &_client );
        }

        void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
        {
            _client.update( deltaTime );
            if ( _client.hasSnapshot() && _firstSnapshotTick < 0 )
                _firstSnapshotTick = static_cast<int32>( world.getLocalTick() );
            for ( uint32 entityId = 1; entityId <= kCrateCount; ++entityId )
            {
                const NetEntityState* pFrom = nullptr;
                const NetEntityState* pTo   = nullptr;
                float32               alpha = 0.0f;
                if ( _client.sampleEntity( entityId, pFrom, pTo, alpha ) == false )
                    continue;
                const float3         from     = readPosition( pFrom->_buffer );
                const float3         to       = readPosition( pTo->_buffer );
                const float3         position = from + ( to - from ) * alpha;
                RigidBodyComponent*& pProxy   = _listProxy[entityId - 1];
                if ( pProxy == nullptr )
                {
                    const string name = string{ "CrateProxy" } + sw::to_string( entityId );
                    pProxy            = spawnBox( world.getObjectManager(), name.c_str(), position, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Kinematic );
                }
                else
                    pProxy->setLocalPosition( position );
            }
        }

        const RigidBodyComponent* findProxy( uint32 entityId ) const { return _listProxy[entityId - 1]; }
        int32                     getFirstSnapshotTick() const { return _firstSnapshotTick; }

    private:
        static float3 readPosition( const vector<uint8>& buffer )
        {
            BitReader     reader( buffer.data(), static_cast<int32>( buffer.size() ) );
            const float32 x = reader.readFloat();
            const float32 y = reader.readFloat();
            const float32 z = reader.readFloat();
            return float3{ x, y, z };
        }

        ReplicationClient           _client;
        vector<RigidBodyComponent*> _listProxy; ///< 엔티티 id - 1
        int32                       _firstSnapshotTick;
    };

    class CrateGame final : public INetSimGame
    {
    public:
        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
                return make_unique<CrateServerSession>( world, &_record );
            return make_unique<CrateClientSession>( world );
        }

        CrateServerRecord _record;
    };

    const CrateServerSession& getServerSession( const NetSimHarness& harness ) { return *static_cast<const CrateServerSession*>( harness.getServer().getSession() ); }

    const CrateClientSession* findClientSession( const NetSimHarness& harness, int32 worldIndex )
    {
        const NetSimWorld* pWorld = harness.findClient( worldIndex );
        return pWorld != nullptr ? static_cast<const CrateClientSession*>( pWorld->getSession() ) : nullptr;
    }

    /** @brief 클라이언트의 대리 바디가 모두 서버 상자와 @p tolerance 안이면 그 최대 오차, 아니면 음수입니다. */
    float32 computeProxyError( const NetSimHarness& harness, int32 worldIndex )
    {
        const CrateClientSession* pClient = findClientSession( harness, worldIndex );
        if ( pClient == nullptr )
            return -1.0f;
        const vector<RigidBodyComponent*>& listCrate = getServerSession( harness ).getCrates();
        float32                            maxError  = 0.0f;
        for ( uint32 entityId = 1; entityId <= kCrateCount; ++entityId )
        {
            const RigidBodyComponent* pProxy = pClient->findProxy( entityId );
            if ( pProxy == nullptr )
                return -1.0f;
            const float3 difference = pProxy->getWorldPosition() - listCrate[entityId - 1]->getWorldPosition();
            maxError                = MathUtil::max( maxError, MathUtil::sqrt( difference.dot( difference ) ) );
        }
        return maxError;
    }

    /** @brief `stepUntil` 조건 — @p pContext 의 클라이언트 대리 바디가 서버 상자와 1 cm 안이다. */
    bool isClientConverged( const NetSimHarness& harness, void* pContext )
    {
        const float32 error = computeProxyError( harness, *static_cast<const int32*>( pContext ) );
        return 0.0f <= error && error < 0.01f;
    }

    NetEmulationConditions makeConditions( float64 latencySeconds, float64 jitterSeconds, float32 lossRate )
    {
        NetEmulationConditions conditions;
        conditions._latency  = latencySeconds;
        conditions._jitter   = jitterSeconds;
        conditions._lossRate = lossRate;
        return conditions;
    }

    struct NetSimRunResult
    {
        vector<float3> _listProxyPosition;
        NetSimTraffic  _serverTraffic;
        uint64         _droppedCount{ 0 };
    };

    constexpr int32 kLockstepDelay = 3;

    /**
     * @brief 락스텝 세션 — 플레이어 번호는 서버 0, 클라이언트는 호스트가 준 번호 + 1. 서버는 클라이언트가 다 들어오면, 클라이언트는 연결되면 시작한다
     *        (빠진 플레이어 없이 틱 0 부터 모두의 입력이 모이게).
     */
    class LockstepNetSimSession final : public INetSimSession
    {
    public:
        LockstepNetSimSession( NetSimWorld& world, int32 playerCount )
            : _session{}
            , _playerCount{ playerCount }
            , _advancedCount{ 0 }
            , _bStarted{ false }
        {
            world.getRouter().addHandler( &_session );
        }

        void onHostEvent( NetSimWorld& world, const NetHostEvent& event ) override
        {
            if ( event._kind != NetHostEvent::Kind::Connected || _bStarted )
                return;
            NetHost& host = world.getHost();
            if ( world.isServer() && host.getConnectedCount() < _playerCount - 1 )
                return;
            _session.initialize( &host, _playerCount, world.isServer() ? 0 : host.getClientIndex() + 1, kLockstepDelay );
            _bStarted = true;
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            if ( _bStarted == false )
                return;
            (void)_session.submitLocalInput( vector<uint8>{ static_cast<uint8>( world.getLocalTick() ) } );
            vector<vector<uint8>> listInput;
            while ( _session.tryAdvance( listInput ) )
                ++_advancedCount;
        }

        int32 getAdvancedCount() const { return _advancedCount; }
        int32 getLocalPlayer() const { return _session.getLocalPlayer(); }

    private:
        LockstepSession _session;
        int32           _playerCount;
        int32           _advancedCount;
        bool            _bStarted;
    };

    class LockstepNetSimGame final : public INetSimGame
    {
    public:
        explicit LockstepNetSimGame( int32 playerCount )
            : _playerCount{ playerCount }
        {
        }

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override { return make_unique<LockstepNetSimSession>( world, _playerCount ); }

    private:
        int32 _playerCount;
    };

    /** @brief 흉내 거르개 — 서버가 보내는 `Accepted` 를 @p _remaining 개만 버린다. */
    struct AcceptedDropper
    {
        int32 _remaining{ 0 };
        int32 _droppedCount{ 0 };
    };

    bool dropAccepted( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
    {
        (void)to;
        AcceptedDropper& dropper = *static_cast<AcceptedDropper*>( pContext );
        if ( dropper._remaining <= 0 || NetHost::peekPacketType( pData, size ) != NetHost::PacketType::Accepted )
            return false;
        --dropper._remaining;
        ++dropper._droppedCount;
        return true;
    }

    /** @brief 흉내 거르개 — 모두 버린다(위조한 주소로 보낸 요청의 답은 공격자에게 오지 않는다). */
    bool dropEverything( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
    {
        (void)to;
        (void)pData;
        (void)size;
        (void)pContext;
        return true;
    }

    NetSimRunResult runLossyScenario( uint32 seed )
    {
        CrateGame      game;
        NetSimHarness  harness;
        NetSimSettings settings;
        settings._seed = seed;
        NetSimRunResult result;
        if ( harness.initialize( settings, &game ) == false )
            return result;
        NetEmulationConditions bad = makeConditions( 0.1, 0.02, 0.15f );
        bad._duplicateRate         = 0.05f;
        bad._reorderRate           = 0.05f;
        for ( int32 index = 0; index < 3; ++index )
            (void)harness.addClient( NetSimLinkConditions::makeSymmetric( bad ) );
        // 상자가 떨어지는 동안(움직이는 상태)에 멈춰 잰다.
        harness.stepTicks( 90 );
        vector<NetSimWorld*> listClient;
        harness.collectClients( listClient );
        for ( NetSimWorld* pClient : listClient )
        {
            const CrateClientSession* pSession = static_cast<const CrateClientSession*>( pClient->getSession() );
            for ( uint32 entityId = 1; entityId <= kCrateCount; ++entityId )
            {
                const RigidBodyComponent* pProxy = pSession->findProxy( entityId );
                result._listProxyPosition.push_back( pProxy != nullptr ? pProxy->getWorldPosition() : float3{ -999.0f, -999.0f, -999.0f } );
            }
        }
        result._serverTraffic = harness.getServer().getTraffic();
        result._droppedCount  = harness.getDroppedPacketCount();
        return result;
    }
} // namespace

/**
 * @brief [NetSimHarnessTest] 월드마다 자기 오브젝트 매니저 · 물리 씬이 돌고, 연결된 클라이언트 셋 모두가 서버 상자 자리를 받아 자기 월드에 옮긴다
 */
SW_TEST_CASE( NetSimHarnessTest, EveryWorldRunsItsOwnSceneAndPhysics )
{
    CrateGame     game;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    for ( int32 index = 0; index < 3; ++index )
        SW_ASSERT_TRUE( harness.addClient( NetSimLinkConditions::makeSymmetric( makeConditions( 0.03, 0.005, 0.02f ) ) ) > 0 );
    harness.stepTicks( 300 );

    SW_EXPECT_TRUE( harness.areAllClientsConnected() );
    vector<NetSimWorld*> listClient;
    harness.collectClients( listClient );
    SW_ASSERT_EQUAL( size_t{ 3 }, listClient.size() );
    const IPhysicsScene3D* pServerPhysics = harness.getServer().getObjectManager().getScenePhysics().findScene3D();
    SW_ASSERT_NOT_NULL( pServerPhysics );
    SW_EXPECT_TRUE( harness.getServer().getObjectManager().getScenePhysics().getStepCount() >= 290 );
    for ( NetSimWorld* pClient : listClient )
    {
        GameObjectManager& manager = pClient->getObjectManager();
        SW_EXPECT_TRUE( &manager != &harness.getServer().getObjectManager() );
        SW_EXPECT_TRUE( manager.getScenePhysics().findScene3D() != nullptr && manager.getScenePhysics().findScene3D() != pServerPhysics );
        SW_EXPECT_TRUE( manager.getScenePhysics().getStepCount() >= 290 );
        const float32 error = computeProxyError( harness, pClient->getWorldIndex() );
        SW_EXPECT_TRUE_MSG( 0.0f <= error && error < 0.01f, "client proxies rest where the server crates rest" );
    }
    // 상자는 서버 물리에서 바닥에 멈췄다(높이 0.5).
    for ( const RigidBodyComponent* pCrate : getServerSession( harness ).getCrates() )
        SW_EXPECT_NEAR_EQUAL( 0.5f, pCrate->getWorldPosition()._y, 0.05f );
}

/**
 * @brief [NetSimHarnessTest] 회선 조건은 클라이언트마다 따로다 — 내림 지연 200 ms 인 클라이언트는 깨끗한 클라이언트보다 첫 스냅샷을 그만큼 늦게 받는다
 */
SW_TEST_CASE( NetSimHarnessTest, LinkConditionsArePerClient )
{
    CrateGame     game;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    const int32          cleanClient = harness.addClient( NetSimLinkConditions{} );
    NetSimLinkConditions slow;
    slow._downstream       = makeConditions( 0.2, 0.0, 0.0f );
    const int32 slowClient = harness.addClient( slow );
    harness.stepTicks( 120 );

    const CrateClientSession* pClean = findClientSession( harness, cleanClient );
    const CrateClientSession* pSlow  = findClientSession( harness, slowClient );
    SW_ASSERT_NOT_NULL( pClean );
    SW_ASSERT_NOT_NULL( pSlow );
    SW_ASSERT_TRUE( pClean->getFirstSnapshotTick() >= 0 );
    SW_ASSERT_TRUE( pSlow->getFirstSnapshotTick() >= 0 );
    // 내림이 두 번 늦는다 — 도전, 그리고 수락(첫 스냅샷은 수락 바로 뒤에 같이 간다) = 200 ms × 2 ≈ 24 틱(보내기 간격의 위상만큼 ±).
    const int32 extraTicks = pSlow->getFirstSnapshotTick() - pClean->getFirstSnapshotTick();
    SW_EXPECT_TRUE_MSG( 21 <= extraTicks && extraTicks <= 27, "two downstream trips of 200 ms" );
    // 내림만 나쁘다 — 서버가 느린 클라이언트에게 보낸 양은 깨끗한 쪽과 비슷하다(흉내는 보내는 쪽에 걸리므로 서버 쪽 셈은 손실 전 양이다).
    const NetSimWorld* pCleanWorld = harness.findClient( cleanClient );
    const NetSimWorld* pSlowWorld  = harness.findClient( slowClient );
    SW_EXPECT_TRUE( harness.getServer().getSentBytesTo( pCleanWorld->getAddress() ) > 0 );
    SW_EXPECT_TRUE( harness.getServer().getSentBytesTo( pSlowWorld->getAddress() ) > 0 );
    SW_EXPECT_TRUE( pSlowWorld->getTraffic()._receivedBytes < pCleanWorld->getTraffic()._receivedBytes ); // 아직 날아가는 중인 몫
}

/**
 * @brief [NetSimHarnessTest] 늦게 들어온 클라이언트도 같은 길로 지어져 서버 상태를 받고, 떠난 클라이언트는 끊김 알림으로 서버에 알려진다(타임아웃이 아니다)
 */
SW_TEST_CASE( NetSimHarnessTest, LateJoinConvergesAndLeaveNotifiesServer )
{
    CrateGame     game;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    const int32 firstClient  = harness.addClient( NetSimLinkConditions{} );
    const int32 secondClient = harness.addClient( NetSimLinkConditions{} );
    harness.stepTicks( 180 );

    // 늦은 참가 — 상자는 이미 멈춰 있다. 새 월드는 바닥만 갖고 시작해 서버 자리를 받는다.
    const int32 lateClient = harness.addClient( NetSimLinkConditions::makeSymmetric( makeConditions( 0.05, 0.01, 0.05f ) ) );
    SW_ASSERT_TRUE( lateClient > secondClient );
    SW_EXPECT_TRUE( computeProxyError( harness, lateClient ) < 0.0f ); // 아직 아무것도 모른다
    int32       watchedClient = lateClient;
    const int32 joinTicks     = harness.stepUntil( &isClientConverged, &watchedClient, 300 );
    SW_EXPECT_TRUE_MSG( 0 < joinTicks && joinTicks < 120, "a late joiner converges within two seconds" );
    SW_EXPECT_EQUAL( 0u, harness.getServer().getLocalTick() - harness.getTick() ); // 서버는 처음부터
    SW_EXPECT_TRUE( harness.findClient( lateClient )->getLocalTick() < harness.getTick() );

    // 떠남 — 서버는 몇 틱 안에 Remote 로 안다.
    game._record._listEvent.clear();
    harness.removeClient( firstClient );
    SW_EXPECT_NULL( harness.findClient( firstClient ) );
    harness.stepTicks( 5 );
    bool bSawRemoteDisconnect = false;
    for ( const NetHostEvent& event : game._record._listEvent )
        bSawRemoteDisconnect = bSawRemoteDisconnect || ( event._kind == NetHostEvent::Kind::Disconnected && event._reason == NetDisconnectReason::Remote );
    SW_EXPECT_TRUE( bSawRemoteDisconnect );
    SW_EXPECT_EQUAL( 2, harness.getServer().getHost().getConnectedCount() );
    // 남은 클라이언트는 계속 받는다.
    harness.stepTicks( 30 );
    SW_EXPECT_TRUE( computeProxyError( harness, secondClient ) < 0.01f );
}

/**
 * @brief [NetSimHarnessTest] 서버의 `Accepted` 하나를 잃은 클라이언트도 자기 플레이어 번호로 락스텝에 들어와 판이 멈추지 않는다
 * @details 데이터 패킷이 수락을 대신하던 때는 클라이언트 번호가 −1 로 남아 플레이어 0 으로 입력을 보냈고, 서버가 그것을 위조로 버려 두 쪽 모두 첫 틱들에서 멈췄다.
 */
SW_TEST_CASE( NetSimHarnessTest, LostAcceptedStillGivesLockstepPlayersTheirNumber )
{
    LockstepNetSimGame game( 2 );
    NetSimHarness      harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    AcceptedDropper      dropper;
    NetSimLinkConditions lostAccepted;
    dropper._remaining                           = 1;
    lostAccepted._downstream._pDropFilter        = &dropAccepted;
    lostAccepted._downstream._pDropFilterContext = &dropper;
    const int32 client                           = harness.addClient( lostAccepted );
    harness.stepTicks( 180 );

    SW_EXPECT_EQUAL( 1, dropper._droppedCount );
    SW_ASSERT_TRUE( harness.areAllClientsConnected() );
    const auto& server  = *static_cast<const LockstepNetSimSession*>( harness.getServer().getSession() );
    const auto& unlucky = *static_cast<const LockstepNetSimSession*>( harness.findClient( client )->getSession() );
    SW_EXPECT_EQUAL( 1, unlucky.getLocalPlayer() );
    // 연결된 뒤(몇 틱)부터 매 틱 한 칸씩 — 입력 지연만큼 뒤처질 수 있다.
    SW_EXPECT_TRUE_MSG( server.getAdvancedCount() > 150, "the server keeps advancing" );
    SW_EXPECT_TRUE_MSG( unlucky.getAdvancedCount() > 150, "the client that lost Accepted keeps advancing" );
}

/**
 * @brief [NetSimHarnessTest] 위조한 주소의 연결 요청은 서버 자리를 잡지 않는다 — 답을 받지 못하는 요청자가 자리 수만큼 있어도 진짜 클라이언트가 들어온다
 * @details 요청 하나로 자리를 잡던 때는 답(도전)을 받지 못하는 요청 넷이 4 자리 서버를 연결 제한 시간(5 초) 동안 채워 진짜 클라이언트가 ServerFull 로 거절됐다.
 *          서버는 도전 값을 주소 · 클라이언트 소금 · 시간 칸에서 비밀 키로 만들어 돌려주기만 하고, 그 값을 되돌려 준 응답이 와야 자리를 잡는다.
 */
SW_TEST_CASE( NetSimHarnessTest, SpoofedConnectRequestsDoNotFillTheServer )
{
    CrateGame      game;
    NetSimHarness  harness;
    NetSimSettings settings;
    settings._hostSettings._maxConnections = 4;
    SW_ASSERT_TRUE( harness.initialize( settings, &game ) );
    NetSimLinkConditions spoofed;
    spoofed._downstream._pDropFilter = &dropEverything;
    for ( int32 index = 0; index < 4; ++index )
        SW_ASSERT_TRUE( harness.addClient( spoofed ) > 0 );
    harness.stepTicks( 30 );
    SW_EXPECT_EQUAL( 0, harness.getServer().getHost().getConnectedCount() );

    const int32 realClient = harness.addClient( NetSimLinkConditions{} );
    harness.stepTicks( 30 );
    SW_ASSERT_NOT_NULL( harness.findClient( realClient ) );
    SW_EXPECT_TRUE( harness.findClient( realClient )->isConnected() );
    SW_EXPECT_EQUAL( 1, harness.getServer().getHost().getConnectedCount() );
}

/**
 * @brief [NetSimHarnessTest] 같은 씨앗이면 나쁜 회선(손실 · 중복 · 순서)에서도 같은 패킷이 같은 틱에 도착해 클라이언트가 보는 것이 비트까지 같다
 */
SW_TEST_CASE( NetSimHarnessTest, SameSeedReplaysTheSameRun )
{
    const NetSimRunResult first  = runLossyScenario( 42u );
    const NetSimRunResult second = runLossyScenario( 42u );
    SW_ASSERT_EQUAL( size_t{ 3 * kCrateCount }, first._listProxyPosition.size() );
    SW_ASSERT_EQUAL( first._listProxyPosition.size(), second._listProxyPosition.size() );
    SW_EXPECT_EQUAL( first._serverTraffic._sentBytes, second._serverTraffic._sentBytes );
    SW_EXPECT_EQUAL( first._serverTraffic._receivedPacketCount, second._serverTraffic._receivedPacketCount );
    for ( size_t index = 0; index < first._listProxyPosition.size(); ++index )
    {
        SW_EXPECT_EQUAL( first._listProxyPosition[index]._x, second._listProxyPosition[index]._x );
        SW_EXPECT_EQUAL( first._listProxyPosition[index]._y, second._listProxyPosition[index]._y );
    }
    // 다른 씨앗은 다른 손실 — 받은 패킷 수가 달라진다.
    const NetSimRunResult other = runLossyScenario( 7u );
    SW_EXPECT_TRUE( other._serverTraffic._receivedPacketCount != first._serverTraffic._receivedPacketCount );
}
