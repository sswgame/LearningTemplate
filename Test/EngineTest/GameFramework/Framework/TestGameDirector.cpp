/**
 * @file TestGameDirector.cpp
 * @brief 디렉터 베이스(`GameDirectorComponent`)와 게임 인스턴스의 상태 컴포넌트 등록(`GameInstanceBase::registerDirector`) 시험입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Framework/GameInstanceBase.h"
#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include "TestFramework/TestFramework.h"

#include <atomic>

namespace sw
{
    /**
     * @brief 디렉터 베이스의 훅을 세는 모의 디렉터입니다. 상태는 `_value` 하나이고, 플러시마다 모습 오브젝트 하나를 코드로 세웁니다(`trackSpawned`).
     */
    class MockGameDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = 0x544B434Du; ///< 'MCKT'
        static constexpr uint32 kStateVersion = 1;

        int32                            _value{ 0 };              ///< PROPERTY 가 아닌 판 상태
        int32                            _startCount{ 0 };         ///< `startGame` 횟수
        int32                            _respawnCount{ 0 };       ///< 모습 전부를 세운 플러시 횟수
        int32                            _despawnCount{ 0 };       ///< `onViewsDespawned` 횟수
        int32                            _restoredCount{ 0 };      ///< 읽어 적용한 복원 횟수
        int32                            _rejectedCount{ 0 };      ///< 읽지 못한 복원 횟수
        int32                            _tickCount{ 0 };          ///< `tickGame` 횟수
        int32                            _pendingSpawnCount{ 0 };  ///< 틱이 쌓은 스폰 요청(다음 플러시가 세운다)
        GameObjectHandle                 _lastView{};              ///< 마지막으로 세운 모습
        std::atomic<int32>               _ruleTickCount{ 0 };      ///< `tickGame` 횟수(다른 워커가 읽는다)
        std::atomic<int32>               _watchMismatchCount{ 0 }; ///< 지켜보는 디렉터가 이번 틱 규칙을 아직 돌지 않았던 횟수
        const MockGameDirectorComponent* _pWatched{ nullptr };     ///< 이 디렉터보다 먼저 돌아야 하는 디렉터

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        void writeState( Archive& outArchive ) const override
        {
            StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
            outArchive << _value;
        }

        /** @brief 이 디렉터가 읽는 상태 바이트입니다. */
        static vector<uint8> makeStateBytes( int32 value )
        {
            Archive archive;
            StateArchiveUtil::writeHeader( archive, kStateTag, kStateVersion );
            archive << value;
            vector<uint8> bytes;
            archive.writeData( bytes );
            return bytes;
        }

    protected:
        bool startGame() override
        {
            ++_startCount;
            _value = 1;
            return true;
        }

        bool readState( Archive& archive ) override
        {
            if ( StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) == false )
                return false;
            int32 value = 0;
            archive >> value;
            if ( archive.isError() || archive.getRemainingBytes() != 0 )
                return false;
            _value = value;
            return true;
        }

        void onStateRestored( bool bRestored ) override { ++( bRestored ? _restoredCount : _rejectedCount ); }

        void tickGame( float32 deltaTime ) override
        {
            (void)deltaTime;
            ++_tickCount;
            const int32 ruleTick = _ruleTickCount.fetch_add( 1 ) + 1;
            if ( _pWatched != nullptr && _pWatched->_ruleTickCount.load() != ruleTick )
                _watchMismatchCount.fetch_add( 1 );
        }

        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            int32 spawnCount = _pendingSpawnCount;
            if ( bRespawnViews )
            {
                ++_respawnCount;
                spawnCount += 1;
            }
            for ( int32 spawnIndex = 0; spawnIndex < spawnCount; ++spawnIndex )
            {
                GameObject* pView = manager.createGameObject( hashed_string( "MockDirectorView" ) );
                if ( pView == nullptr )
                    continue;
                trackSpawned( *pView );
                _lastView = pView->getHandle();
            }
            _pendingSpawnCount = 0;
        }

        void onViewsDespawned() override { ++_despawnCount; }
        bool hasPendingSpawn() const override { return _pendingSpawnCount > 0; }

    public:
        /** @brief 다음 플러시가 세울 모습을 하나 쌓습니다(틱 안에서 세우지 않는 디렉터의 모양). */
        void requestView() { ++_pendingSpawnCount; }
    };

    inline const TypeInfo* MockGameDirectorComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockGameDirectorComponent>, hashed_string( "MockGameDirectorComponent" ),
                                          hashed_string( "sw::MockGameDirectorComponent" ), sizeof( MockGameDirectorComponent ),
                                          hashed_string( "sw::GameDirectorComponent" ) );
    }
} // namespace sw

using namespace sw;

namespace
{
    /**
     * @brief 게임 서비스에 씬 매니저만 겁니다 — 어서션이 빠져나가도 풀리게 RAII 로 둡니다.
     * @details `EngineTest/GameTestUtil.h` 의 같은 가드는 `sw::test` 를 들여와 시험 매크로의 `test::` 와 이름이 부딪힌다.
     */
    struct ScopedDirectorSceneService
    {
        explicit ScopedDirectorSceneService( SceneManager& manager )
        {
            ModuleService service{};
            service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::SceneManager )] = &manager;
            game::bindGameService( service );
        }
        ~ScopedDirectorSceneService() { game::unbindGameService(); }

        ScopedDirectorSceneService( const ScopedDirectorSceneService& )            = delete;
        ScopedDirectorSceneService& operator=( const ScopedDirectorSceneService& ) = delete;
    };

    /** @brief 활성 씬 하나와 그 씬을 가리키는 게임 서비스입니다 — `GameInstanceBase` 가 활성 씬의 매니저를 여기서 찾습니다. */
    struct GameDirectorTestScene
    {
        SceneManager                           _sceneManager;
        Scene*                                 _pScene;
        unique_ptr<ScopedDirectorSceneService> _pBinding;

        GameDirectorTestScene()
            : _sceneManager{}
            , _pScene{ nullptr }
            , _pBinding{}
        {
            _pScene   = _sceneManager.createEmptyActiveScene( "GameDirectorProbe" );
            _pBinding = make_unique<ScopedDirectorSceneService>( _sceneManager );
        }

        GameObjectManager* getManager() const { return _pScene != nullptr ? _pScene->getObjectManager() : nullptr; }

        /** @brief 디렉터 하나를 단 오브젝트를 세웁니다(플레이 시작 전). */
        MockGameDirectorComponent* createDirector( const utf8* pName ) const
        {
            GameObject* pObject = getManager()->createGameObject( hashed_string( pName ) );
            return pObject != nullptr ? pObject->addComponent<MockGameDirectorComponent>() : nullptr;
        }
    };

    /** @brief 모의 디렉터를 상태 스냅샷에 올린 게임 인스턴스입니다. */
    class MockDirectorGameInstance : public GameInstanceBase
    {
    public:
        MockDirectorGameInstance() { registerDirector<MockGameDirectorComponent>(); }
    };
} // namespace

/**
 * @brief [GameDirectorTest] 디렉터 베이스의 수명 — 시작이 첫 플러시로 모습을 세우고, 걷으면 다음 틱이 다시 세우며, 틱이 쌓은 스폰은 틱 뒤에 선다
 */
SW_TEST_CASE( GameDirectorTest, StartsFlushesDespawnsAndRespawns )
{
    RegisterMockComponents();
    MockGameDirectorComponent::StaticType();
    GameDirectorTestScene      scene;
    GameObjectManager*         pManager  = scene.getManager();
    MockGameDirectorComponent* pDirector = scene.createDirector( "Director" );
    SW_ASSERT_NOT_NULL( pManager );
    SW_ASSERT_NOT_NULL( pDirector );

    pManager->beginPlay();
    SW_EXPECT_TRUE( pDirector->isStarted() );
    SW_EXPECT_EQUAL( 1, pDirector->_startCount );
    SW_EXPECT_EQUAL( 1, pDirector->_respawnCount ); // 틱 밖의 시작 — 플러시가 그 자리에서 돈다
    const GameObjectHandle firstView = pDirector->_lastView;
    SW_EXPECT_TRUE( pManager->resolveGameObject( firstView ) != nullptr );

    pDirector->requestView();
    pManager->tick( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1, pDirector->_tickCount );
    SW_EXPECT_EQUAL( 1, pDirector->_respawnCount ); // 서 있으니 다시 세우지 않는다
    SW_EXPECT_TRUE( pDirector->_lastView != firstView );
    SW_EXPECT_TRUE( pManager->resolveGameObject( pDirector->_lastView ) != nullptr );

    const GameObjectHandle secondView = pDirector->_lastView;
    pDirector->despawnViews();
    SW_EXPECT_EQUAL( 1, pDirector->_despawnCount );
    SW_EXPECT_TRUE( pManager->resolveGameObject( firstView ) == nullptr );
    SW_EXPECT_TRUE( pManager->resolveGameObject( secondView ) == nullptr );

    pManager->tick( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 2, pDirector->_respawnCount ); // 걷혀 있었다 — 틱 뒤에 다시 세웠다
    SW_EXPECT_TRUE( pManager->resolveGameObject( pDirector->_lastView ) != nullptr );

    pManager->endPlay();
    SW_EXPECT_FALSE( pDirector->isStarted() );
}

/**
 * @brief [GameDirectorTest] 복원 바이트 — 시작 전에 받으면 들고 있다가 판을 연 뒤 적용하고, 시작 뒤에 받으면 바로 적용해 모습을 걷는다. 맞지 않는 바이트는 알리고 판을 둔다
 */
SW_TEST_CASE( GameDirectorTest, RestoreStateIsHeldUntilStartAndAppliedAfter )
{
    RegisterMockComponents();
    MockGameDirectorComponent::StaticType();
    GameDirectorTestScene      scene;
    GameObjectManager*         pManager  = scene.getManager();
    MockGameDirectorComponent* pDirector = scene.createDirector( "Director" );
    SW_ASSERT_NOT_NULL( pDirector );

    pDirector->restoreState( MockGameDirectorComponent::makeStateBytes( 42 ) );
    SW_EXPECT_EQUAL( 0, pDirector->_restoredCount ); // 판이 열리기 전 — 들고 있다
    pManager->beginPlay();
    SW_EXPECT_EQUAL( 1, pDirector->_restoredCount );
    SW_EXPECT_EQUAL( 42, pDirector->_value ); // `startGame` 의 1 위에 덮였다

    pDirector->restoreState( MockGameDirectorComponent::makeStateBytes( 7 ) );
    SW_EXPECT_EQUAL( 2, pDirector->_restoredCount );
    SW_EXPECT_EQUAL( 7, pDirector->_value );
    SW_EXPECT_EQUAL( 2, pDirector->_despawnCount ); // 시작 때의 적용 하나 + 지금 하나 — 지금 상태대로 다시 세운다

    vector<uint8> badBytes = MockGameDirectorComponent::makeStateBytes( 9 );
    badBytes.push_back( 0 ); // 남는 바이트
    pDirector->restoreState( std::move( badBytes ) );
    SW_EXPECT_EQUAL( 1, pDirector->_rejectedCount );
    SW_EXPECT_EQUAL( 7, pDirector->_value );
    pManager->endPlay();
}

/**
 * @brief [GameDirectorTest] `registerDirector` 한 줄 — 상태 저장 전에 디렉터의 상태를 싣고 세운 모습을 걷으며(스냅샷에 실리지 않는다), 복원 뒤 다시 만든 디렉터에 돌려준다
 */
SW_TEST_CASE( GameDirectorTest, RegisteredDirectorRidesTheSnapshot )
{
    RegisterMockComponents();
    MockGameDirectorComponent::StaticType();
    GameDirectorTestScene      scene;
    GameObjectManager*         pManager  = scene.getManager();
    MockGameDirectorComponent* pDirector = scene.createDirector( "Director" );
    SW_ASSERT_NOT_NULL( pDirector );
    pManager->beginPlay();
    pDirector->_value                        = 99;
    const GameObjectHandle view              = pDirector->_lastView;
    const ComponentHandle  handle            = pDirector->getHandle();
    uint32                 objectCountBefore = 0;
    pManager->forEachGameObject( [&objectCountBefore]( GameObject* pObject )
    {
        (void)pObject;
        ++objectCountBefore;
    } );
    SW_EXPECT_EQUAL( 2u, objectCountBefore ); // 디렉터 + 모습 하나

    MockDirectorGameInstance instance;
    vector<uint8>            snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );
    SW_EXPECT_TRUE( pManager->resolveGameObject( view ) == nullptr ); // 걷었다 — 모습은 스냅샷에 실리지 않는다

    SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
    pManager->mergePendingAdds();
    pManager->tick( 1.0f / 60.0f ); // 되살린 디렉터가 시작하며(또는 다음 틱에) 지금 상태대로 다시 세운다
    MockGameDirectorComponent* pRestored = static_cast<MockGameDirectorComponent*>( pManager->resolveComponent( handle ) );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_EXPECT_EQUAL( 99, pRestored->_value );
    SW_EXPECT_EQUAL( 1, pRestored->_restoredCount );
    uint32 objectCountAfter = 0;
    pManager->forEachGameObject( [&objectCountAfter]( GameObject* pObject )
    {
        (void)pObject;
        ++objectCountAfter;
    } );
    SW_EXPECT_EQUAL( 2u, objectCountAfter ); // 옛 모습이 스냅샷으로 되살아나 새 모습과 겹치지 않는다
    SW_EXPECT_TRUE( pManager->resolveGameObject( pRestored->_lastView ) != nullptr );
    pManager->endPlay();
}

/**
 * @brief [GameDirectorTest] 다른 오브젝트의 디렉터 뒤에 — `_tickAfter` 를 건 디렉터는 매 틱 앞 디렉터가 이번 틱 규칙을 돈 뒤에 돈다(규칙 서브틱 + 선행 조건),
 *        걸지 않은 디렉터는 주 틱 그대로다
 */
SW_TEST_CASE( GameDirectorTest, TickAfterOrdersDirectorsOnDifferentObjects )
{
    RegisterMockComponents();
    MockGameDirectorComponent::StaticType();
    GameDirectorTestScene      scene;
    GameObjectManager*         pManager = scene.getManager();
    MockGameDirectorComponent* pAfter   = scene.createDirector( "AfterDirector" ); // 먼저 만든다 — 순서를 걸지 않으면 보통 먼저 돈다
    MockGameDirectorComponent* pBefore  = scene.createDirector( "BeforeDirector" );
    SW_ASSERT_NOT_NULL( pManager );
    SW_ASSERT_NOT_NULL( pAfter );
    SW_ASSERT_NOT_NULL( pBefore );
    pAfter->setTickAfter( pBefore->getOwner()->getHandle() );
    pAfter->_pWatched = pBefore;
    SW_EXPECT_TRUE( pAfter->getRuleTickHandle()._subTickId == GameDirectorComponent::kRuleSubTick );
    SW_EXPECT_TRUE( pBefore->getRuleTickHandle() == pBefore->getTickHandle() );

    pManager->beginPlay();
    for ( int32 tick = 0; tick < 100; ++tick )
        pManager->tick( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 100, pBefore->_ruleTickCount.load() );
    SW_EXPECT_EQUAL( 100, pAfter->_ruleTickCount.load() ); // 규칙은 한 틱에 한 번(주 틱과 서브틱 둘 다 돌지 않는다)
    SW_EXPECT_EQUAL( 0, pAfter->_watchMismatchCount.load() );
    pManager->endPlay();
}
