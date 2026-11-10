// GameInstanceTest — 게임 인스턴스 수명 계약: 프로세스당 하나, 실패한 기동의 한 길 되감기(게임 범위 · 보안 제공자 · 파생 훅), 두 번 불러도 되는 종료,
// 게임 범위 서비스 충돌의 초기화 실패 전파. 세이브 슬롯은 메모리 로컬 저장소라 사용자 데이터 폴더를 쓰지 않는다(nogpu, 디스크 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Net/GameNetSession.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/MemoryLocalStore.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace
{
    /** @brief 게임 범위에 거는 시험 서비스입니다(같은 타입을 두 번 걸어 충돌을 만든다). */
    struct LifecycleTestService
    {
        int32 _value;
    };

    /** @brief 파생 훅이 무엇을 하는지입니다. */
    enum class LifecycleTestStep : uint8
    {
        Succeed,      ///< `onInitialize` 가 통과한다
        FailDerived,  ///< `onInitialize` 가 false 다(파생 초기화 실패)
        BindTwice,    ///< `onInitialize` 가 같은 타입을 두 번 건다(게임 범위 서비스 충돌)
        FailSettings, ///< gamesettings 를 못 읽는다(아무것도 걸기 전 실패)
    };

    /** @brief 수명 훅을 세는 시험용 게임 인스턴스입니다. 팩 파일 대신 글로 gamesettings 를 읽고, 세이브 슬롯은 메모리 저장소다. */
    class LifecycleTestGame final : public sw::GameInstance
    {
    public:
        LifecycleTestGame( sw::MemoryLocalDatabase& database, LifecycleTestStep step )
            : GameInstance( sw::make_unique<sw::GameLocalStore>( sw::make_unique<sw::MemoryLocalStore>( &database, sw::LocalSealContext{} ) ) )
            , _service{ 7 }
            , _otherService{ 8 }
            , _initializeCount{ 0 }
            , _shutdownCount{ 0 }
            , _step{ step }
        {
        }

        uint32                    getInitializeCount() const { return _initializeCount; }
        uint32                    getShutdownCount() const { return _shutdownCount; }
        const sw::SubsystemScope& getOwnScope() { return getGameScope(); }

    protected:
        bool loadGameSettings() override
        {
            if ( _step == LifecycleTestStep::FailSettings )
                return false;
            return _packConfig._data.loadFromXMLText( "<GameSettings><startMap>game/empty/maps/editortest.scene.xml</startMap><defaultLanguage>ko_kr</defaultLanguage></GameSettings>", "LifecycleTestGame" );
        }
        bool onInitialize() override
        {
            ++_initializeCount;
            switch ( _step )
            {
                case LifecycleTestStep::Succeed:
                    return bindScopedService( &_service );
                case LifecycleTestStep::FailDerived:
                    return false;
                case LifecycleTestStep::BindTwice:
                    return bindScopedService( &_service ) && bindScopedService( &_otherService );
                case LifecycleTestStep::FailSettings:
                    return true; // 여기까지 오지 않는다
            }
            return false;
        }
        void onShutdown() override { ++_shutdownCount; }

    private:
        LifecycleTestService _service;
        LifecycleTestService _otherService;
        uint32               _initializeCount;
        uint32               _shutdownCount;
        LifecycleTestStep    _step;
    };

    /** @brief 지금 게임 범위와 넷 세션의 보안 제공자입니다 — 인스턴스가 서고 진 뒤에 같아야 하는 프로세스 상태다. */
    struct ProcessGameState
    {
        sw::SubsystemScope*       _pGameScope;
        sw::INetSecurityProvider* _pSecurity;

        static ProcessGameState capture()
        {
            sw::GameNetSession* const pNetSession = sw::game::findSubsystem<sw::GameNetSession>();
            return ProcessGameState{ sw::game::findGameScope(), pNetSession != nullptr ? pNetSession->getConnectSecurity().findProvider() : nullptr };
        }
        bool equals( const ProcessGameState& other ) const { return _pGameScope == other._pGameScope && _pSecurity == other._pSecurity; }
    };
} // namespace

using namespace sw;

/** @brief [GameInstanceTest] 선 인스턴스는 게임 범위를 지금 범위로 걸고, 종료는 그것을 되돌린다. 종료를 두 번 불러도 파생 훅은 한 번이다 */
SW_TEST_CASE( GameInstanceTest, StartsAndShutsDownOnceAndRestoresTheProcess )
{
    const ProcessGameState before = ProcessGameState::capture();
    MemoryLocalDatabase    database;
    LifecycleTestGame      game{ database, LifecycleTestStep::Succeed };
    SW_REQUIRE_TRUE( game.initialize( nullptr ) );
    SW_EXPECT_TRUE( game::findGameScope() == &game.getOwnScope() );
    SW_EXPECT_NOT_NULL( game::findSubsystem<GameSettings>() );
    SW_EXPECT_NOT_NULL( game::findSubsystem<LifecycleTestService>() );
    SW_EXPECT_EQUAL( 1u, game.getInitializeCount() );

    game.shutdown();
    game.shutdown(); // 두 번째 종료는 아무것도 하지 않는다
    SW_EXPECT_EQUAL( 1u, game.getShutdownCount() );
    SW_EXPECT_TRUE( ProcessGameState::capture().equals( before ) );
    SW_EXPECT_NULL( game::findSubsystem<LifecycleTestService>() );
}

/** @brief [GameInstanceTest] 파생 초기화가 실패하면 세운 것을 한 길로 거둔다 — 게임 범위 · 보안 제공자가 앞으로 돌아가고, 들어간 파생 훅은 짝으로 한 번 내린다 */
SW_TEST_CASE( GameInstanceTest, FailedDerivedInitializeRollsEverythingBack )
{
    SW_EXPECT_LOG( "GameInstance.GameInstanceRolledBack" );
    const ProcessGameState before = ProcessGameState::capture();
    MemoryLocalDatabase    database;
    LifecycleTestGame      game{ database, LifecycleTestStep::FailDerived };
    SW_EXPECT_FALSE( game.initialize( nullptr ) );
    SW_EXPECT_EQUAL( 1u, game.getInitializeCount() );
    SW_EXPECT_EQUAL( 1u, game.getShutdownCount() );
    SW_EXPECT_TRUE( ProcessGameState::capture().equals( before ) );
    SW_EXPECT_NULL( game::findSubsystem<GameSettings>() );

    // 실패한 인스턴스가 남긴 것이 없다 — 새 인스턴스가 정상으로 선다.
    LifecycleTestGame next{ database, LifecycleTestStep::Succeed };
    SW_REQUIRE_TRUE( next.initialize( nullptr ) );
    next.shutdown();
    SW_EXPECT_TRUE( ProcessGameState::capture().equals( before ) );
}

/** @brief [GameInstanceTest] 아무것도 걸기 전에 멈춘 기동(gamesettings 없음)은 파생 훅을 부르지 않고 범위도 걸지 않는다 */
SW_TEST_CASE( GameInstanceTest, FailureBeforeTheScopeTouchesNothing )
{
    SW_EXPECT_LOG( "GameInstance.GameInstanceCannotStart4" );
    SW_EXPECT_LOG( "GameInstance.GameInstanceRolledBack" );
    const ProcessGameState before = ProcessGameState::capture();
    MemoryLocalDatabase    database;
    LifecycleTestGame      game{ database, LifecycleTestStep::FailSettings };
    SW_EXPECT_FALSE( game.initialize( nullptr ) );
    SW_EXPECT_EQUAL( 0u, game.getInitializeCount() );
    SW_EXPECT_EQUAL( 0u, game.getShutdownCount() );
    SW_EXPECT_TRUE( ProcessGameState::capture().equals( before ) );
}

/** @brief [GameInstanceTest] 게임 범위에 같은 타입을 두 번 걸면 두 번째가 false 이고 그것이 초기화 실패가 된다(나중 것이 앞 것을 소리 없이 덮지 않는다) */
SW_TEST_CASE( GameInstanceTest, ScopedServiceConflictFailsInitialize )
{
    SW_EXPECT_LOG( "GameInstance.GameScopedServiceAlready" );
    SW_EXPECT_LOG( "GameInstance.GameInstanceRolledBack" );
    const ProcessGameState before = ProcessGameState::capture();
    MemoryLocalDatabase    database;
    LifecycleTestGame      game{ database, LifecycleTestStep::BindTwice };
    SW_EXPECT_FALSE( game.initialize( nullptr ) );
    SW_EXPECT_TRUE( ProcessGameState::capture().equals( before ) );
}

/** @brief [GameInstanceTest] 프로세스당 인스턴스 하나 — 하나가 도는 동안 둘째는 아무것도 걸지 않고 거절되고, 앞 것을 내린 뒤에는 선다 */
SW_TEST_CASE( GameInstanceTest, SecondInstanceIsRejectedWhileOneRuns )
{
    SW_EXPECT_LOG( "GameInstance.SecondGameInstance" );
    const ProcessGameState before = ProcessGameState::capture();
    MemoryLocalDatabase    database;
    LifecycleTestGame      first{ database, LifecycleTestStep::Succeed };
    LifecycleTestGame      second{ database, LifecycleTestStep::Succeed };
    SW_REQUIRE_TRUE( first.initialize( nullptr ) );
    SW_EXPECT_FALSE( second.initialize( nullptr ) );
    SW_EXPECT_EQUAL( 0u, second.getInitializeCount() );
    SW_EXPECT_TRUE( game::findGameScope() == &first.getOwnScope() ); // 앞 인스턴스의 지금 범위가 그대로다
    second.shutdown();                                               // 서지 못한 인스턴스의 종료는 앞 인스턴스를 건드리지 않는다
    SW_EXPECT_TRUE( game::findGameScope() == &first.getOwnScope() );

    first.shutdown();
    SW_REQUIRE_TRUE( second.initialize( nullptr ) );
    second.shutdown();
    SW_EXPECT_TRUE( ProcessGameState::capture().equals( before ) );
}

/** @brief [GameInstanceTest] 테스트 범위 위에 선 인스턴스는 그 범위를 상위로 이어 거기 걸린 서비스도 찾고, 종료하면 지금 범위를 테스트 범위로 되돌린다 */
SW_TEST_CASE( GameInstanceTest, InstanceChainsOntoTheCurrentScopeAndRestoresIt )
{
    LifecycleTestService outerService{ 3 };
    SubsystemScope       testScope;
    SW_REQUIRE_TRUE( testScope.bind( &outerService ) );
    const game::ScopedGameScope scoped{ testScope };
    MemoryLocalDatabase         database;
    LifecycleTestGame           game{ database, LifecycleTestStep::FailSettings }; // 범위를 걸기 전에 멈추는 길 — 테스트 범위가 그대로여야 한다
    SW_EXPECT_LOG( "GameInstance.GameInstanceCannotStart4" );
    SW_EXPECT_LOG( "GameInstance.GameInstanceRolledBack" );
    SW_EXPECT_FALSE( game.initialize( nullptr ) );
    SW_EXPECT_TRUE( game::findGameScope() == &testScope );

    LifecycleTestGame next{ database, LifecycleTestStep::FailDerived }; // 범위를 건 뒤에 멈추는 길
    SW_EXPECT_FALSE( next.initialize( nullptr ) );
    SW_EXPECT_TRUE( game::findGameScope() == &testScope );
    SW_EXPECT_TRUE( game::findSubsystem<LifecycleTestService>() == &outerService );
}
