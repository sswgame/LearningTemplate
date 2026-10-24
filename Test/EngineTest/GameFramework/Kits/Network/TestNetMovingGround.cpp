#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Gameplay/Gimmick/TickedPathMoverComponent.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"

// 움직이는 바닥이 있는 넷 씬 — 서버 세계와 클라이언트 세계가 같은 장면(틱 함수 바닥 + 그 위의 캐릭터)을 각자 돌리고, 클라이언트가 예측한 자리가
// 서버가 그 입력 틱까지 처리한 자리와 보정 없이 맞는지 본다(MovingGroundFollowUps M4).

using namespace sw;

namespace
{
    struct NetMovingGroundInternal
    {
        static constexpr uint16  kEndpointServerPort    = 4300;
        static constexpr uint16  kEndpointClientPort    = 5300;
        static constexpr int32   kFrameCount            = 60 * 8;
        static constexpr uint32  kWalkStartTick         = 200; ///< 이 입력 틱부터 걷는다 — 그 전에는 서 있는 채 바닥에 실려 간다
        static constexpr uint32  kCompareStartTick      = 120; ///< 이 입력 틱부터 비교한다(접속 직후 서버 틱 차이를 알기 전의 어긋남을 버린다)
        static constexpr uint32  kServerInputDelayTicks = 12;  ///< 서버는 접속 뒤 이만큼 기다렸다가 입력을 꺼낸다 — 지연 50 ms(3 틱)에 여유를 더해, 꺼낼 틱이 늘 도착해 있게 한다
        static constexpr float32 kWalkSpeed             = 1.5f;
        static constexpr float32 kPositionTolerance     = 0.05f;

        /** @brief 입력 틱 @p tick 의 입력 바이트입니다. 0 = 뒤로, 1 = 섬, 2 = 앞으로(45 틱마다 바뀐다). */
        static uint8 makeInputByte( uint32 tick )
        {
            if ( tick < kWalkStartTick )
                return 1;
            return static_cast<uint8>( ( ( tick - kWalkStartTick ) / 45u ) % 3u );
        }

        static float3 makeMoveVelocity( uint8 inputByte )
        {
            return float3{ 0.0f, 0.0f, ( static_cast<float32>( inputByte ) - 1.0f ) * kWalkSpeed };
        }

        /** @brief 바닥(키네마틱 + 틱 함수 경로)과 그 위에 선 캐릭터, 조종 시스템(틱 번호)을 세웁니다. */
        static CharacterControllerComponent* buildWorld( GameObjectManager& manager )
        {
            GameObject*         pPlatform = manager.createGameObject( hashed_string( "Platform" ) );
            RigidBodyComponent* pBody     = pPlatform->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._halfExtents = float3{ 4.0f, 0.5f, 4.0f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Kinematic );
            pBody->setLayer( hashed_string( "Default" ) );
            pBody->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );
            pPlatform->addComponent<TickedPathMoverComponent>()->setPath( vector<float3>{
                                                                              float3{ 6.0f, 0.0f, 0.0f }
            },
                                                                          2.0f, true );

            GameObject*                   pHero       = manager.createGameObject( hashed_string( "Hero" ) );
            CharacterControllerComponent* pController = pHero->addComponent<CharacterControllerComponent>();
            pController->setLocalPosition( float3{ 0.0f, 0.02f, 0.0f } );
            ControlSystem::ensureFor( manager ); // 틱 번호를 세는 시스템을 붙인다
            return pController;
        }

        /** @brief 바닥을 틱 @p desiredTick 의 자리로 둡니다. 이번 갱신에서 조종 시스템 틱이 하나 올라간 값을 바닥이 읽는다. */
        static void alignPlatform( GameObjectManager& manager, int64 desiredTick )
        {
            const ControlSystem* pControl = ControlSystem::find( manager );
            TickedPathMoverComponent::setTickOffsetInScene( manager, desiredTick - ( static_cast<int64>( pControl->getTick() ) + 1 ) );
        }

        static void writePosition( const float3& position, vector<uint8>& outBytes )
        {
            BitWriter writer;
            writer.writeFloat( position._x );
            writer.writeFloat( position._y );
            writer.writeFloat( position._z );
            outBytes = writer.getBytes();
        }

        static float3 readPosition( const vector<uint8>& bytes )
        {
            BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
            float3    position{};
            position._x = reader.readFloat();
            position._y = reader.readFloat();
            position._z = reader.readFloat();
            return position;
        }
    };
} // namespace

/**
 * @brief [NetMovingGroundTest] 틱 함수 바닥 위 캐릭터의 예측은 서버가 같은 입력 틱까지 처리한 자리와 보정 없이 맞는다
 * @details 서버는 넷 틱에 바닥을 두고(`넷 틱 − 조종 시스템 틱`), 클라이언트는 예측하는 입력 틱에 서버 차이(`findServerTickOffset`)를 더한 틱에 둔다.
 *          지연 50 ms 망 위에서 둘 다 60 Hz 로 돌고, 바닥은 3 m/s 로 왕복한다. 캐릭터는 처음에 서서 실려 가다가 입력 틱 200 부터 바닥 위에서 걷는다.
 *          클라이언트가 입력 틱마다 기록한 자리를 서버의 소유자 상태(그 입력 틱까지 처리한 자리)와 견준다. 틱 차이를 알기 전(접속 직후)의 몇 틱은 비교하지 않는다.
 */
SW_TEST_CASE( NetMovingGroundTest, PredictedPawnOnTickedPlatformMatchesServer )
{
    using Scene = NetMovingGroundInternal;

    LoopbackNetwork        network;
    NetEmulationTransport  serverTransport( network.createEndpoint( Scene::kEndpointServerPort ), 21u );
    NetEmulationTransport  clientTransport( network.createEndpoint( Scene::kEndpointClientPort ), 22u );
    NetEmulationConditions conditions;
    conditions._latency = 0.05;
    serverTransport.setDefaultConditions( conditions );
    clientTransport.setDefaultConditions( conditions );
    NetHostConfig hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( &serverTransport, hostSettings );
    clientHost.initialize( &clientTransport, hostSettings );
    SW_ASSERT_TRUE( serverHost.listen() );
    SW_ASSERT_TRUE( clientHost.connect( NetAddress::makeLoopback( Scene::kEndpointServerPort ) ) );

    ReplicationServerConfig serverSettings;
    serverSettings._tickInterval = 1.0f / 60.0f;
    ReplicationClientConfig clientSettings;
    clientSettings._tickInterval = 1.0f / 60.0f;
    ReplicationServer server;
    ReplicationClient client;
    SW_ASSERT_TRUE( server.initialize( &serverHost, serverSettings, nullptr ) );
    SW_ASSERT_TRUE( client.initialize( &clientHost, clientSettings ) );

    GameObjectManager             serverManager;
    GameObjectManager             clientManager;
    CharacterControllerComponent* pServerPawn = Scene::buildWorld( serverManager );
    CharacterControllerComponent* pClientPawn = Scene::buildWorld( clientManager );
    SW_ASSERT_TRUE( pServerPawn != nullptr && pClientPawn != nullptr );
    serverManager.beginPlay();
    clientManager.beginPlay();

    float64        time           = 0.0;
    uint32         serverTick     = 0;
    uint32         clientTick     = 0;
    uint32         nextInputTick  = 1;
    uint32         lastCompared   = 0;
    int32          compareCount   = 0;
    float32        maxError       = 0.0f;
    float32        maxServerX     = 0.0f;
    float32        maxServerSwing = 0.0f;
    vector<float3> listPredicted( 1, float3{} ); ///< 입력 틱 번호가 인덱스(0 은 비워 둔다)
    vector<uint8>  buffer;
    vector<uint8>  inputBytes;
    vector<uint8>  stateBytes;
    for ( int32 frame = 0; frame < Scene::kFrameCount; ++frame )
    {
        time += 1.0 / 60.0;
        serverTransport.update( time );
        clientTransport.update( time );
        serverHost.update( time );
        clientHost.update( time );
        int32          connectionID = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        while ( serverHost.receiveMessage( connectionID, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == server.handleMessage( connectionID, buffer ) );
        }
        while ( clientHost.receiveMessage( connectionID, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, buffer ) );
        }
        if ( clientHost.getConnectionState( 0 ) != NetConnectionState::Connected )
            continue;

        // 서버 틱 — 그 연결의 다음 입력을 꺼내 캐릭터를 몰고(없으면 선다), 바닥은 넷 틱의 자리에 둔다.
        ++serverTick;
        bool       bExact       = false;
        uint32     handledInput = 0;
        float3     serverMove{};
        const bool bInputDue = serverTick > Scene::kServerInputDelayTicks;
        const bool bGotInput = bInputDue && server.popInput( 0, nextInputTick, inputBytes, bExact ) && inputBytes.size() == 1;
        if ( bGotInput )
        {
            handledInput = nextInputTick;
            ++nextInputTick;
            serverMove = Scene::makeMoveVelocity( inputBytes[0] );
        }
        pServerPawn->setMoveVelocity( serverMove );
        Scene::alignPlatform( serverManager, static_cast<int64>( serverTick ) );
        test::updateFrames( serverManager, 1 );
        const float3 serverPosition = pServerPawn->getWorldPosition();
        maxServerX                  = MathUtil::max( maxServerX, serverPosition._x );
        maxServerSwing              = MathUtil::max( maxServerSwing, MathUtil::abs( serverPosition._z ) );
        Scene::writePosition( serverPosition, stateBytes );
        server.beginTick( serverTick );
        server.setEntity( 1, 1, stateBytes );
        server.endTick();
        if ( handledInput != 0 )
            SW_EXPECT_TRUE( server.setOwnerState( 0, handledInput, stateBytes ) );
        server.sendSnapshots();

        // 클라이언트 틱 — 입력을 보내고 같은 입력으로 예측한다. 바닥은 이 입력이 서버에서 도는 틱(입력 틱 + 서버 차이)의 자리에 둔다.
        ++clientTick;
        const uint8 inputByte = Scene::makeInputByte( clientTick );
        SW_EXPECT_TRUE( client.sendInput( clientTick, vector<uint8>{ inputByte } ) );
        const std::optional<int64> serverTickOffset = client.findServerTickOffset();
        if ( serverTickOffset.has_value() )
            Scene::alignPlatform( clientManager, static_cast<int64>( clientTick ) + *serverTickOffset );
        pClientPawn->setMoveVelocity( Scene::makeMoveVelocity( inputByte ) );
        test::updateFrames( clientManager, 1 );
        listPredicted.push_back( pClientPawn->getWorldPosition() );
        client.update( 1.0f / 60.0f );

        // 서버가 새 소유자 상태를 보냈으면 그 입력 틱에 예측한 자리와 견준다.
        const NetOwnerState* pOwner = client.findOwnerState();
        const bool           bFresh = pOwner != nullptr && pOwner->_bPresent == SW_TRUE && pOwner->_inputTick > lastCompared && pOwner->_inputTick <= clientTick;
        if ( bFresh )
        {
            lastCompared = pOwner->_inputTick;
            if ( pOwner->_inputTick >= Scene::kCompareStartTick )
            {
                const float32 error = ( listPredicted[pOwner->_inputTick] - Scene::readPosition( pOwner->_buffer ) ).getLength();
                maxError            = MathUtil::max( maxError, error );
                ++compareCount;
            }
        }
    }
    serverManager.endPlay();
    clientManager.endPlay();

    SW_EXPECT_TRUE( compareCount > 150 );                   // 접속 뒤 비교할 소유자 상태가 계속 왔다
    SW_EXPECT_TRUE( maxServerX > 3.0f );                    // 캐릭터가 바닥에 실려 멀리 갔다(바닥을 놓쳤다면 제자리다)
    SW_EXPECT_TRUE( maxServerSwing > 0.5f );                // 바닥 위에서 걸었다
    SW_EXPECT_TRUE( maxError < Scene::kPositionTolerance ); // 예측이 서버와 보정 없이 맞는다
}
