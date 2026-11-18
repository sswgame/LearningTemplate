#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

// DestructionNetSessionComponentTest — 씬의 넷 링크(서버)에 붙은 파괴 세션이 틱 뒤 큐에서 프레임마다 한 번 복제를 돌리는지 본다(틱은 델타만 적는다).

using namespace sw;

namespace
{
    /** @brief 아무 데도 보내지 않는 서버 쪽 상대입니다(연결 없음). */
    class SilentServerPeers final : public INetPeers
    {
    public:
        bool               sendMessage( int32, NetChannelType, const uint8*, int32 ) override { return false; }
        bool               trySendMessage( int32, NetChannelType, const uint8*, int32 ) override { return false; }
        bool               sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return false; }
        int32              broadcast( NetChannelType, const uint8*, int32, int32 ) override { return 0; }
        void               disconnect( int32 ) override {}
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return true; }
        NetConnectionState getConnectionState( int32 ) const override { return NetConnectionState::Disconnected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 ) const override { return 0; }
        optional<int32>    getClientIndex() const override { return nullopt; }
        int32              getConnectedCount() const override { return 0; }
        void               collectConnected( vector<int32>& outListConnection ) const override { outListConnection.clear(); }
        int32              getMaxBytesPerSecond() const override { return 0; }
        bool               isConnectionSaturated( int32 ) const override { return false; }
        void               addSentKindStats( NetMessageKindStatsTable& ) const override {}
    };
} // namespace

/**
 * @brief [DestructionNetSessionComponentTest] 서버 세션은 프레임마다 정확히 한 번 복제를 돌린다 — 틱이 아니라 틱 뒤 큐에서(남의 파괴 컴포넌트를 쓰는 일이다)
 */
SW_TEST_CASE( DestructionNetSessionComponentTest, ServerSessionUpdatesOncePerFrameAfterTheTick )
{
    SilentServerPeers peers;
    NetMessageRouter  router;
    GameObjectManager manager;
    NetLinkValue      link;
    link._pPeers  = &peers;
    link._pRouter = &router;
    link._role    = NetLinkRole::Server;
    SW_REQUIRE_TRUE( manager.getSubsystem<SceneNetLink>().bind( link ) );
    DestructionNetSessionComponent* pComponent = manager.createGameObject( hashed_string( "DestructionNet" ) )->addComponent<DestructionNetSessionComponent>();
    manager.beginPlay();
    SW_REQUIRE_TRUE( pComponent->getSession().isActive() );

    const float32 deltaTime = constant::time::kDefaultFixedStepSeconds; // 컴포넌트의 기본 틱 간격과 같다 — 첫 틱 빈도 검사를 지난다
    manager.update( deltaTime );                                        // 첫 프레임은 틱 그룹 바꿈이 든다
    const uint32 startTick = pComponent->getSession().getServerTick();
    for ( uint32 frame = 1; frame <= 3; ++frame )
    {
        manager.update( deltaTime );
        SW_EXPECT_EQUAL( startTick + frame, pComponent->getSession().getServerTick() );
    }
    SW_EXPECT_TRUE( pComponent->getSession().isActive() );
    manager.endPlay();
}
