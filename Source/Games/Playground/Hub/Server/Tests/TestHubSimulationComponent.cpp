// HubSimulationComponentTest — 허브 서버는 세션이 접속 티켓을 요구할 때만 선다(세션 설정이 씬 넷 링크의 `_bTicketRequired` 로 온다). 공장은 세는 가짜다(nogpu, 연결 없음).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Foundation/Subsystem/Tests/SceneNetLinkTestBinding.h"

#include "Games/Playground/Hub/HubSimulationComponent.h"

using namespace sw;

namespace
{
    class FakeHubServer final : public IHubServer
    {
    public:
        explicit FakeHubServer( int32& shutdownCount )
            : _shutdownCount{ shutdownCount }
        {
        }

        void            update( float32 ) override {}
        void            shutdown() override { ++_shutdownCount; }
        void            collectAvatars( vector<HubAvatarSession>& outListSession ) const override { outListSession.clear(); }
        HubActionResult moveAvatar( uint32, const AbsolutePosition&, StepSeconds, bool ) override { return HubActionResult::Ok; }

    private:
        int32& _shutdownCount;
    };

    class CountingHubServerFactory final : public IHubServerFactory
    {
    public:
        int32 _createCount{ 0 };
        int32 _shutdownCount{ 0 };

        unique_ptr<IHubServer> createServer( INetPeers&, NetMessageRouter&, const HubRules&, const StreamingCellGridDef& ) override
        {
            ++_createCount;
            return make_unique<FakeHubServer>( _shutdownCount );
        }
    };

    /** @brief 서버 링크를 올린 월드에서 허브 오브젝트를 시작하고, 시작한 뒤의 역할 값(`Hub.NetRole`)을 돌려줍니다. @p bCloseSession 이면 시작한 뒤 세션을 닫고 한 프레임 더 돈 값입니다. */
    HubNetRole startHubAndReadRole( bool bTicketRequired, CountingHubServerFactory& factory, bool bCloseSession = false )
    {
        HubRules rules;
        rules._listLevelExperience = { 100 };
        rules._moveSpeed           = 6.0f;
        rules._rideSpeed           = 12.0f;
        rules._listAvatarStart.push_back( RespawnSpawnPoint{} );
        rules._remoteCallBurstCount         = 20;
        rules._remoteCallRefillMilliseconds = 50;
        rules._listPlacement.push_back( HubPlacement{} );

        SubsystemScope scope;
        SW_EXPECT_TRUE( scope.bind<IHubServerFactory>( &factory ) );
        game::ScopedGameScope   scopedScope( scope );
        GameObjectManager       manager;
        SceneNetLinkTestBinding link( manager, NetLinkRole::Server, bTicketRequired );
        HubSimulationComponent* pHub                                                                   = manager.createGameObject( hashed_string( "HubServer" ) )->addComponent<HubSimulationComponent>();
        *HubSimulationComponent::StaticType()->findProperty( "_rules" )->getValuePtr<HubRules>( pHub ) = rules;
        manager.beginPlay();
        manager.update( 1.0f / 60.0f );
        SW_EXPECT_EQUAL( 1, pHub->getRulesValid() );
        if ( bCloseSession )
        {
            link.closeSession();
            manager.update( 1.0f / 60.0f );
        }
        return pHub->getNetRole();
    }
} // namespace

/** @brief [HubSimulationComponentTest] 티켓을 요구하는 서버 세션에서는 허브 서버가 한 번 선다 */
SW_TEST_CASE( HubSimulationComponentTest, TicketSessionStartsTheHubServer )
{
    CountingHubServerFactory factory;
    SW_EXPECT_EQUAL( HubNetRole::Server, startHubAndReadRole( true, factory ) );
    SW_EXPECT_EQUAL( 1, factory._createCount );
}

/** @brief [HubSimulationComponentTest] 플레이 중에 세션이 닫히면 허브 서버가 내려간다(저장하고 놓는다) — 닫힌 창구로 복제를 이어 가지 않는다 */
SW_TEST_CASE( HubSimulationComponentTest, ClosedSessionShutsDownTheHubServer )
{
    CountingHubServerFactory factory;
    SW_EXPECT_EQUAL( HubNetRole::Alone, startHubAndReadRole( true, factory, true ) );
    SW_EXPECT_EQUAL( 1, factory._createCount );
    SW_EXPECT_EQUAL( 1, factory._shutdownCount );
}

/** @brief [HubSimulationComponentTest] 티켓을 요구하지 않는 서버 세션(개발 · 리슨 호스트)에서는 허브 서버가 서지 않고 오류도 없다 — 서버 코드(공장)를 찾지 않는다 */
SW_TEST_CASE( HubSimulationComponentTest, SessionWithoutTicketsHasNoHubServer )
{
    CountingHubServerFactory factory;
    SW_EXPECT_EQUAL( HubNetRole::Alone, startHubAndReadRole( false, factory ) );
    SW_EXPECT_EQUAL( 0, factory._createCount );
}
