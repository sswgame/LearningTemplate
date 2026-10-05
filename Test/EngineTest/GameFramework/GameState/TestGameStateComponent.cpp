/**
 * @file TestGameStateComponent.cpp
 * @brief 공유 판 상태(`GameStateComponent`)의 순서 · 여는 규칙 · 구간 세이브 시험입니다.
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
#include "GameFramework/Base/GameState/GameStateComponent.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /**
     * @brief 공유 상태를 여는 · 읽는 순서를 적는 모의 디렉터의 몸입니다(리플렉션은 두 잎이 단다).
     * @details 매 틱 공유 플래그 `probe` 를 하나 올리고 그 값과 그때의 시계 시각을 적는다 — 같은 오브젝트의 두 디렉터는 늘 1 차이로 번갈아 본다.
     */
    class GameStateProbeDirector : public GameDirectorComponent
    {
    public:
        static constexpr uint32 kStateTag     = 0x42505347u; ///< 'GSPB'
        static constexpr int64  kStartingGold = 100;

        GameStateInitResult _initResult{ GameStateInitResult::AlreadyInitialized };
        int32               _lastSeen{ 0 };     ///< 이번 틱에 올린 뒤 본 `probe`
        float32             _lastHour{ -1.0f }; ///< 이번 틱에 본 시계 시각

        void writeState( Archive& outArchive ) const override { StateArchiveUtil::writeHeader( outArchive, kStateTag, 1 ); }

    protected:
        bool startGame() override
        {
            GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
            if ( pState == nullptr )
                return false;
            GameStateSettings settings;
            settings._clock._secondsPerDay = 24.0f; // 실제 1 초 = 게임 1 시간
            _initResult                    = pState->initialize( settings );
            if ( _initResult == GameStateInitResult::Fresh )
                pState->getWallet().add( "Gold", kStartingGold );
            return true;
        }
        bool readState( Archive& archive ) override { return StateArchiveUtil::readHeader( archive, kStateTag, 1 ) && archive.getRemainingBytes() == 0; }
        void tickGame( float32 deltaTime ) override
        {
            (void)deltaTime;
            GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
            if ( pState == nullptr )
                return;
            _lastSeen = pState->getFlags().addFlag( "probe", 1 );
            _lastHour = pState->getClock().getHour();
        }
        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 같은 오브젝트에 먼저 붙는 탐침입니다. */
    class GameStateProbeFirstDirector : public GameStateProbeDirector
    {
    public:
        REFLECT_BODY();
        const TypeInfo* getTypeInfo() const override { return StaticType(); }
    };

    inline const TypeInfo* GameStateProbeFirstDirector::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<GameStateProbeFirstDirector>, hashed_string( "GameStateProbeFirstDirector" ),
                                          hashed_string( "sw::GameStateProbeFirstDirector" ), sizeof( GameStateProbeFirstDirector ),
                                          hashed_string( "sw::GameDirectorComponent" ) );
    }
} // namespace sw

namespace sw
{
    /** @brief 같은 오브젝트에 나중에 붙는 탐침입니다. */
    class GameStateProbeSecondDirector : public GameStateProbeDirector
    {
    public:
        REFLECT_BODY();
        const TypeInfo* getTypeInfo() const override { return StaticType(); }
    };

    inline const TypeInfo* GameStateProbeSecondDirector::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<GameStateProbeSecondDirector>, hashed_string( "GameStateProbeSecondDirector" ),
                                          hashed_string( "sw::GameStateProbeSecondDirector" ), sizeof( GameStateProbeSecondDirector ),
                                          hashed_string( "sw::GameDirectorComponent" ) );
    }
} // namespace sw

namespace sw
{
    /** @brief 공유 상태와 두 탐침을 상태 스냅숏에 올린 게임 인스턴스입니다 — 공유 상태가 먼저입니다. */
    class GameStateProbeGameInstance : public GameInstanceBase
    {
    public:
        GameStateProbeGameInstance()
        {
            registerStatefulComponent<GameStateComponent>();
            registerDirector<GameStateProbeFirstDirector>();
            registerDirector<GameStateProbeSecondDirector>();
        }
    };
} // namespace sw

using namespace sw;

namespace
{
    /** @brief 게임 서비스에 씬 매니저만 겁니다(`TestGameDirector.cpp` 의 같은 가드 — 파일 지역). */
    struct ScopedGameStateSceneService
    {
        explicit ScopedGameStateSceneService( SceneManager& manager )
        {
            ModuleService service{};
            service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::SceneManager )] = &manager;
            game::bindGameService( service );
        }
        ~ScopedGameStateSceneService() { game::unbindGameService(); }

        ScopedGameStateSceneService( const ScopedGameStateSceneService& )            = delete;
        ScopedGameStateSceneService& operator=( const ScopedGameStateSceneService& ) = delete;
    };

    /** @brief 활성 씬 하나 + 공유 상태 · 두 탐침을 (그 순서로) 붙인 오브젝트 하나입니다. */
    struct GameStateTestScene
    {
        SceneManager                            _sceneManager;
        Scene*                                  _pScene;
        unique_ptr<ScopedGameStateSceneService> _pBinding;
        GameObject*                             _pStateObject;

        GameStateTestScene()
            : _sceneManager{}
            , _pScene{ nullptr }
            , _pBinding{}
            , _pStateObject{ nullptr }
        {
            _pScene       = _sceneManager.createEmptyActiveScene( "GameStateProbe" );
            _pBinding     = make_unique<ScopedGameStateSceneService>( _sceneManager );
            _pStateObject = getManager()->createGameObject( hashed_string( "GameState" ) );
            (void)_pStateObject->addComponent<GameStateComponent>();
            (void)_pStateObject->addComponent<GameStateProbeFirstDirector>();
            (void)_pStateObject->addComponent<GameStateProbeSecondDirector>();
        }

        GameObjectManager* getManager() const { return _pScene != nullptr ? _pScene->getObjectManager() : nullptr; }
    };

    /** @brief 씬의 (첫) 공유 상태입니다 — 되살린 뒤에는 오브젝트가 새로 섰다. */
    GameStateComponent* findSceneGameState( GameObjectManager& manager )
    {
        GameStateComponent* pFound = nullptr;
        manager.forEachComponentOfType<GameStateComponent>( [&pFound]( GameStateComponent* pComponent )
        {
            if ( pFound == nullptr )
                pFound = pComponent;
        } );
        return pFound;
    }

    void registerGameStateProbeTypes()
    {
        RegisterMockComponents();
        (void)GameStateProbeFirstDirector::StaticType();
        (void)GameStateProbeSecondDirector::StaticType();
    }
} // namespace

/**
 * @brief [GameStateComponentTest] 같은 오브젝트에서는 공유 상태(시계) → 첫 디렉터 → 둘째 디렉터 순서로 매 틱 돈다 — 첫 디렉터만 판을 열고 시작값은 한 번만 들어간다
 */
SW_TEST_CASE( GameStateComponentTest, DirectorsOnTheStateObjectTickInAttachOrderAfterTheClock )
{
    registerGameStateProbeTypes();
    GameStateTestScene scene;
    GameObjectManager* pManager = scene.getManager();
    SW_ASSERT_NOT_NULL( pManager );
    pManager->beginPlay();

    GameStateComponent*           pState  = scene._pStateObject->getComponent<GameStateComponent>();
    GameStateProbeFirstDirector*  pFirst  = scene._pStateObject->getComponent<GameStateProbeFirstDirector>();
    GameStateProbeSecondDirector* pSecond = scene._pStateObject->getComponent<GameStateProbeSecondDirector>();
    SW_ASSERT_NOT_NULL( pState );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );
    SW_EXPECT_TRUE( pFirst->_initResult == GameStateInitResult::Fresh );
    SW_EXPECT_TRUE( pSecond->_initResult == GameStateInitResult::AlreadyInitialized );
    SW_EXPECT_TRUE( pState->isFreshGame() );
    SW_EXPECT_EQUAL( GameStateProbeDirector::kStartingGold, pState->getWallet().getBalance( "Gold" ) ); // 둘째가 다시 넣지 않았다

    for ( int32 tickIndex = 1; tickIndex <= 3; ++tickIndex )
    {
        pManager->tick( 1.0f );
        SW_EXPECT_EQUAL( tickIndex * 2 - 1, pFirst->_lastSeen );
        SW_EXPECT_EQUAL( tickIndex * 2, pSecond->_lastSeen );                                         // 늘 첫 디렉터 바로 뒤
        SW_EXPECT_NEAR_EQUAL( 6.0f + static_cast<float32>( tickIndex ), pFirst->_lastHour, 1.0e-3f ); // 시계가 디렉터보다 먼저 흘렀다
    }
    pManager->endPlay();
}

/**
 * @brief [GameStateComponentTest] 스냅숏에서 되살리면 공유 상태가 먼저 돌아오고, 여는 디렉터는 시작값을 다시 넣지 않는다
 */
SW_TEST_CASE( GameStateComponentTest, SnapshotRestoresSharedStateBeforeTheDirectorsSeedIt )
{
    registerGameStateProbeTypes();
    GameStateTestScene scene;
    GameObjectManager* pManager = scene.getManager();
    SW_ASSERT_NOT_NULL( pManager );
    pManager->beginPlay();
    GameStateComponent* pState = scene._pStateObject->getComponent<GameStateComponent>();
    SW_ASSERT_NOT_NULL( pState );
    pState->getWallet().add( "Gold", 150 ); // 100 + 150
    pState->getFlags().setFlag( "village.founded", 1 );
    pManager->tick( 1.0f );

    GameStateProbeGameInstance instance;
    vector<uint8>              snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );
    SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
    pManager->mergePendingAdds();
    pManager->tick( 0.0f ); // 되살린 오브젝트가 시작한다 — 시계는 흐르지 않는다

    GameStateComponent* pRestored = findSceneGameState( *pManager );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_EXPECT_FALSE( pRestored->isFreshGame() );
    SW_EXPECT_EQUAL( int64{ 250 }, pRestored->getWallet().getBalance( "Gold" ) ); // 350 이면 시작값이 덧쌓였다
    SW_EXPECT_EQUAL( 1, pRestored->getFlags().getFlag( "village.founded" ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pRestored->getClock().getHour(), 1.0e-3f );
    GameStateProbeFirstDirector* pFirst = pRestored->getOwner()->getComponent<GameStateProbeFirstDirector>();
    SW_ASSERT_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( pFirst->_initResult == GameStateInitResult::Restored );
    pManager->endPlay();
}

/**
 * @brief [GameStateComponentTest] 모르는 구간은 건너뛰고, 빠진 구간은 새 판, 판이 다른 구간도 새 판이다 — 아는 구간은 그대로 돌아온다
 */
SW_TEST_CASE( GameStateComponentTest, UnknownSectionIsSkippedAndMissingSectionStaysFresh )
{
    Wallet wallet;
    wallet.add( "Gold", 42 );
    Archive walletBody;
    wallet.writeState( walletBody );
    Archive unknownBody;
    unknownBody << uint32{ 0xCAFEu };
    GameFlags flags;
    flags.setFlag( "old", 1 );
    Archive flagsBody;
    flags.writeState( flagsBody );

    Archive archive;
    StateArchiveUtil::writeHeader( archive, GameStateComponent::kStateTag, GameStateComponent::kStateVersion );
    archive << uint32{ 3 };
    StateArchiveUtil::writeSection( archive, Wallet::kStateTag, Wallet::kStateVersion, walletBody );
    StateArchiveUtil::writeSection( archive, 0x5A5A5A5Au, 1, unknownBody );
    StateArchiveUtil::writeSection( archive, GameFlags::kStateTag, GameFlags::kStateVersion + 1, flagsBody ); // 앞으로 올 판 — 읽지 않는다
    vector<uint8> bytes;
    archive.writeData( bytes );

    GameStateComponent state;
    state.restoreState( std::move( bytes ) );
    SW_EXPECT_FALSE( state.isInitialized() );
    GameStateSettings settings;
    settings._clock._secondsPerDay = 24.0f;
    SW_EXPECT_TRUE( state.initialize( settings ) == GameStateInitResult::Restored );
    SW_EXPECT_EQUAL( int64{ 42 }, state.getWallet().getBalance( "Gold" ) );
    SW_EXPECT_EQUAL( 0, state.getFlags().getFlag( "old" ) ); // 판이 달라 새 판
    SW_EXPECT_EQUAL( 0, state.getClock().getDay() );         // 빠진 구간 — 새 판(6 시)
    SW_EXPECT_NEAR_EQUAL( 6.0f, state.getClock().getHour(), 1.0e-3f );
}
