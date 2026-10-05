/**
 * @file TestKitComposition.cpp
 * @brief 장르 키트 둘(Farming · CreatureLife)을 한 씬 · 한 공유 상태에 섞는 조립 시험입니다 — 틱 순서 · 키트 사이 흐름 · 다른 실행의 세이브 · 핫 리로드 왕복.
 * @details 밭이 순무를 키워 팔면 번 돈이 공유 지갑에 들고, 마을이 같은 틱에 그 돈으로 과수원을 심어 생물의 부탁(공유 일지)이 끝난다.
 *          공유 상태(`GameStateComponent`)와 두 디렉터가 한 오브젝트에 그 순서로 붙어 시계 → 밭 → 마을 순서로 매 틱 돈다.
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
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Quest/QuestCatalog.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureTown.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Simulation/Farming/FarmField.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /** @brief 조립 시험의 카탈로그입니다 — 디렉터는 되살릴 때 새로 서므로 포인터를 들지 않고 여기서 얻는다(게임에서는 게임 인스턴스가 로컬 서비스로 든다). */
    struct KitCompositionCatalogs
    {
        static constexpr const utf8* kCropXml       = R"(
<CropCatalog>
  <Crop id="turnip" name="Turnip" seed="turnip_seed" produce="turnip" days="2" seasons="Spring" seedPrice="10" sellPrice="30"/>
</CropCatalog>
)";
        static constexpr const utf8* kCreatureXml   = R"(
<CreatureLifeCatalog>
  <Habitat id="meadow"><Key symbol="G" object="grass"/><Row cells="GG"/></Habitat>
  <Habitat id="orchard"><Key symbol="T" object="tree"/><Row cells="TT"/></Habitat>
  <Species id="sprout" habitats="meadow" phases="Dawn,Day" weathers="sunny" chance="1" requests="meadow_request"/>
  <Appeal diversity="10" creature="5" friendship="0.1"><Tier name="Camp" min="0"/></Appeal>
</CreatureLifeCatalog>
)";
        static constexpr const utf8* kFriendshipXml = R"(
<ReputationCatalog>
  <Faction id="creature.sprout" min="0" max="1000" start="0"><Tier name="Stranger" min="0"/><Tier name="Friend" min="50"/></Faction>
</ReputationCatalog>
)";
        static constexpr const utf8* kQuestXml      = R"(
<QuestCatalog>
  <Quest id="meadow_request">
    <Stage id="plant" next="done"><Objective kind="Habitat" target="orchard" count="1"/></Stage>
    <Stage id="done" complete="true"/>
  </Quest>
</QuestCatalog>
)";

        CropCatalog         _crops;
        CreatureLifeCatalog _creatures;
        ReputationCatalog   _friendship;
        QuestCatalog        _quests;
        uint8               _bLoaded{ SW_FALSE };

        static KitCompositionCatalogs& get()
        {
            static KitCompositionCatalogs s_catalogs;
            if ( s_catalogs._bLoaded == SW_FALSE )
            {
                s_catalogs._crops.setKnownSeasons( { "Spring", "Summer", "Fall", "Winter" } );
                const bool bLoaded = s_catalogs._crops.loadFromXmlText( kCropXml, "KitCompositionTest" ) &&
                                     s_catalogs._creatures.loadFromXmlText( kCreatureXml, "KitCompositionTest" ) &&
                                     s_catalogs._friendship.loadFromXmlText( kFriendshipXml, "KitCompositionTest" ) &&
                                     s_catalogs._quests.loadFromXmlText( kQuestXml, "KitCompositionTest" );
                s_catalogs._bLoaded = bLoaded ? SW_TRUE : SW_FALSE;
            }
            return s_catalogs;
        }

        /** @brief 공유 상태 설정 — 하루는 실제 24 초(1 초 = 게임 1 시간), 계절 넷 × 28 일. */
        GameStateSettings makeStateSettings() const
        {
            GameStateSettings settings;
            settings._clock._listSeason    = { "Spring", "Summer", "Fall", "Winter" };
            settings._clock._secondsPerDay = 24.0f;
            settings._clock._daysPerSeason = 28;
            settings._pQuestCatalog        = &_quests;
            settings._pReputationCatalog   = &_friendship; // 호감도는 공유 평판의 세력 creature.<종>(U14)
            return settings;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 밭 키트 디렉터 — 순무 두 칸을 키우고, 거둔 것을 바로 팔아 공유 지갑에 넣고 공유 플래그 `farm.harvested` 를 센다. 하루 넘김은 공유 시계가 알린다. */
    class KitCompositionFarmDirector : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = 0x4D46434Bu; ///< 'KCFM'
        static constexpr uint32 kStateVersion = 1;
        static constexpr int32  kFieldWidth   = 2;

        const TypeInfo*  getTypeInfo() const override { return StaticType(); }
        const FarmField& getField() const { return _field; }

        void writeState( Archive& outArchive ) const override
        {
            StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
            Archive body;
            _field.writeState( body );
            StateArchiveUtil::writeSection( outArchive, FarmField::kStateTag, FarmField::kStateVersion, body );
        }

    protected:
        bool startGame() override
        {
            GameStateComponent*           pState   = GameStateComponent::findOnOwner( *this );
            const KitCompositionCatalogs& catalogs = KitCompositionCatalogs::get();
            if ( pState == nullptr || catalogs._bLoaded == SW_FALSE )
                return false;
            (void)pState->initialize( catalogs.makeStateSettings() ); // 이 오브젝트의 첫 디렉터가 판을 연다
            _field.initialize( kFieldWidth, 1, &catalogs._crops );
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                replant( x, pState->getClock().getSeasonName() );
            }
            return true;
        }

        bool readState( Archive& archive ) override
        {
            uint32     tag     = 0;
            uint32     version = 0;
            Archive    body;
            const bool bFrameRead = StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) && StateArchiveUtil::readSection( archive, tag, version, body );
            if ( bFrameRead == false || tag != FarmField::kStateTag || version != FarmField::kStateVersion )
                return false;
            FarmField field;
            field.initialize( kFieldWidth, 1, &KitCompositionCatalogs::get()._crops );
            const bool bRead = field.readState( body ) && body.getRemainingBytes() == 0 && archive.getRemainingBytes() == 0;
            if ( bRead == false )
                return false;
            _field = std::move( field );
            return true;
        }

        void tickGame( float32 deltaTime ) override
        {
            (void)deltaTime;
            GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
            if ( pState == nullptr )
                return;
            for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
            {
                if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                    startNextDay( *pState );
            }
        }

        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }

    private:
        /** @brief 하루 넘김 — 밭이 자라고, 다 자란 칸을 거둬 팔고 다시 심고, 나머지는 물 준다. */
        void startNextDay( GameStateComponent& state )
        {
            const hashed_string season = state.getClock().getSeasonName(); // 계절은 공유 시계의 데이터(U12 — FarmSeason 없음)
            _field.advanceDay( season, false );
            const CropCatalog& crops = KitCompositionCatalogs::get()._crops;
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                const FarmTile* pTile = _field.findTile( x, 0 );
                if ( pTile == nullptr || pTile->_bReady == SW_FALSE )
                {
                    (void)_field.water( x, 0 );
                    continue;
                }
                hashed_string produce;
                int32         count = 0;
                if ( _field.harvest( x, 0, produce, count ) == FarmActionResult::Done && 0 < count )
                {
                    const int64 earned = static_cast<int64>( crops.findSellPrice( produce ) ) * count;
                    if ( 0 < earned )
                        state.getWallet().add( Wallet::getDefaultCurrency(), earned );
                    (void)state.getFlags().addFlag( "farm.harvested", count );
                }
                replant( x, season );
            }
        }

        void replant( int32 x, const hashed_string& season )
        {
            (void)_field.till( x, 0 );
            (void)_field.water( x, 0 );
            (void)_field.plant( x, 0, "turnip_seed", season );
        }

        FarmField _field;
    };

    inline const TypeInfo* KitCompositionFarmDirector::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<KitCompositionFarmDirector>, hashed_string( "KitCompositionFarmDirector" ),
                                          hashed_string( "sw::KitCompositionFarmDirector" ), sizeof( KitCompositionFarmDirector ),
                                          hashed_string( "sw::GameDirectorComponent" ) );
    }
} // namespace sw

namespace sw
{
    /**
     * @brief 마을 키트 디렉터 — 시마다 방문을 굴리고, 생물이 오면 부탁을 받고, 공유 지갑에 과수원 값이 있으면 나무 두 그루를 심는다.
     * @details 같은 오브젝트에서 밭 뒤에 돌아 밭이 **이번 틱에** 번 돈을 본다. 시작 배치(풀 두 칸)는 공유 일지에 서식지 수를 알리므로 새 판일 때만 놓는다.
     */
    class KitCompositionTownDirector : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = 0x4E54434Bu; ///< 'KCTN'
        static constexpr uint32 kStateVersion = 1;
        static constexpr int32  kTownSize     = 6;
        static constexpr int64  kOrchardPrice = 50;

        const TypeInfo*     getTypeInfo() const override { return StaticType(); }
        const CreatureTown& getTown() const { return _town; }

        void writeState( Archive& outArchive ) const override
        {
            StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
            Archive body;
            _town.writeState( body );
            StateArchiveUtil::writeSection( outArchive, CreatureTown::kStateTag, CreatureTown::kStateVersion, body );
        }

    protected:
        bool startGame() override
        {
            GameStateComponent*           pState   = GameStateComponent::findOnOwner( *this );
            const KitCompositionCatalogs& catalogs = KitCompositionCatalogs::get();
            if ( pState == nullptr || catalogs._bLoaded == SW_FALSE )
                return false;
            (void)pState->initialize( catalogs.makeStateSettings() ); // 이미 열렸으면 아무것도 하지 않는다
            initializeTown( _town, *pState );
            if ( pState->isFreshGame() )
            {
                (void)_town.setObject( 0, 0, "grass" );
                (void)_town.setObject( 1, 0, "grass" );
            }
            return true;
        }

        bool readState( Archive& archive ) override
        {
            GameStateComponent* pState  = GameStateComponent::findOnOwner( *this );
            uint32              tag     = 0;
            uint32              version = 0;
            Archive             body;
            const bool          bFrameRead = pState != nullptr && StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) &&
                                    StateArchiveUtil::readSection( archive, tag, version, body );
            if ( bFrameRead == false || tag != CreatureTown::kStateTag || version != CreatureTown::kStateVersion )
                return false;
            CreatureTown town;
            initializeTown( town, *pState );
            const bool bRead = town.readState( body ) && body.getRemainingBytes() == 0 && archive.getRemainingBytes() == 0;
            if ( bRead == false )
                return false;
            _town = std::move( town );
            return true;
        }

        void tickGame( float32 deltaTime ) override
        {
            (void)deltaTime;
            GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
            if ( pState == nullptr )
                return;
            const WorldClock& clock = pState->getClock();
            for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
            {
                if ( clockEvent._kind == WorldClockEvent::Kind::HourChanged )
                    (void)_town.attractVisitorsAt( clock.getDay(), clockEvent._value, clock.getDayPhase(), "sunny" );
                else if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                    _town.advanceDay();
            }
            const bool bCanAsk = _town.findCreature( "sprout" ) != nullptr && pState->getQuestLog().getStatus( "meadow_request" ) == QuestStatus::NotStarted;
            if ( bCanAsk )
                (void)_town.startRequest( "sprout", "meadow_request" );
            // 밭이 이번 틱에 번 돈도 여기서 보인다 — 같은 오브젝트에서 밭이 먼저 돈다.
            Wallet&    wallet         = pState->getWallet();
            const bool bCanBuyOrchard = _town.countHabitats( "orchard" ) == 0 && wallet.canAfford( Wallet::getDefaultCurrency(), kOrchardPrice );
            if ( bCanBuyOrchard && wallet.trySpend( Wallet::getDefaultCurrency(), kOrchardPrice ) )
            {
                (void)_town.setObject( 0, 2, "tree" );
                (void)_town.setObject( 1, 2, "tree" );
            }
        }

        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }

    private:
        static void initializeTown( CreatureTown& outTown, GameStateComponent& state )
        {
            const KitCompositionCatalogs& catalogs = KitCompositionCatalogs::get();
            outTown.initialize( &catalogs._creatures, state.makeRefs(), kTownSize, kTownSize, CreatureTownSettings{} ); // U14 뒤 모양 — 평판 카탈로그는 공유 상태가
        }

        CreatureTown _town;
    };

    inline const TypeInfo* KitCompositionTownDirector::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<KitCompositionTownDirector>, hashed_string( "KitCompositionTownDirector" ),
                                          hashed_string( "sw::KitCompositionTownDirector" ), sizeof( KitCompositionTownDirector ),
                                          hashed_string( "sw::GameDirectorComponent" ) );
    }
} // namespace sw

namespace sw
{
    /** @brief 공유 상태 · 두 디렉터를 상태 스냅숏에 올린 게임 인스턴스입니다 — 공유 상태가 먼저입니다. */
    class KitCompositionGameInstance : public GameInstanceBase
    {
    public:
        KitCompositionGameInstance()
        {
            registerStatefulComponent<GameStateComponent>();
            registerDirector<KitCompositionFarmDirector>();
            registerDirector<KitCompositionTownDirector>();
        }
    };
} // namespace sw

using namespace sw;

namespace
{
    struct KitCompositionTestInternal
    {
        static constexpr float32 kTickSeconds  = 0.5f; ///< 게임 30 분
        static constexpr int32   kHalfRunTicks = 92;   ///< 6 시에서 46 시간 — 2 일 4 시
        static constexpr int32   kHarvestTicks = 84;   ///< 2 일 0 시 — 첫 수확의 틱
    };

    /** @brief 게임 서비스에 씬 매니저만 겁니다(파일 지역 — `TestGameDirector.cpp` 의 가드와 같은 일). */
    struct ScopedCompositionSceneService
    {
        explicit ScopedCompositionSceneService( SceneManager& manager )
        {
            ModuleService service{};
            service.arrServices[internal::toRawServiceId( internal::ModuleServiceId::SceneManager )] = &manager;
            game::bindGameService( service );
        }
        ~ScopedCompositionSceneService() { game::unbindGameService(); }

        ScopedCompositionSceneService( const ScopedCompositionSceneService& )            = delete;
        ScopedCompositionSceneService& operator=( const ScopedCompositionSceneService& ) = delete;
    };

    /** @brief 활성 씬 하나 + 공유 상태 · 밭 · 마을을 (그 순서로) 붙인 오브젝트 하나입니다. */
    struct KitCompositionTestScene
    {
        SceneManager                              _sceneManager;
        Scene*                                    _pScene;
        unique_ptr<ScopedCompositionSceneService> _pBinding;

        KitCompositionTestScene()
            : _sceneManager{}
            , _pScene{ nullptr }
            , _pBinding{}
        {
            _pScene                  = _sceneManager.createEmptyActiveScene( "KitComposition" );
            _pBinding                = make_unique<ScopedCompositionSceneService>( _sceneManager );
            GameObject* pStateObject = getManager()->createGameObject( hashed_string( "VillageState" ) );
            (void)pStateObject->addComponent<GameStateComponent>();
            (void)pStateObject->addComponent<KitCompositionFarmDirector>();
            (void)pStateObject->addComponent<KitCompositionTownDirector>();
        }

        GameObjectManager* getManager() const { return _pScene != nullptr ? _pScene->getObjectManager() : nullptr; }
    };

    /** @brief 판을 견줄 숫자들입니다. */
    struct KitCompositionDigest
    {
        int64       _gold{ 0 };
        float32     _hour{ 0.0f };
        int32       _day{ 0 };
        int32       _harvested{ 0 };
        int32       _creatureCount{ 0 };
        int32       _friendship{ 0 };
        int32       _orchardCount{ 0 };
        int32       _stateCount{ 0 }; ///< 씬의 공유 상태 수 — 되살린 뒤에도 하나여야 한다
        uint32      _cropCount{ 0 };
        uint32      _readyCount{ 0 };
        QuestStatus _requestStatus{ QuestStatus::NotStarted };
    };

    void registerKitCompositionTypes()
    {
        RegisterMockComponents();
        (void)KitCompositionFarmDirector::StaticType();
        (void)KitCompositionTownDirector::StaticType();
    }

    void runCompositionTicks( GameObjectManager& manager, int32 tickCount )
    {
        for ( int32 tickIndex = 0; tickIndex < tickCount; ++tickIndex )
        {
            manager.tick( KitCompositionTestInternal::kTickSeconds );
        }
    }

    /** @brief 씬의 판을 숫자로 읽습니다. 공유 상태 · 두 디렉터를 못 찾으면 false 입니다. */
    bool makeCompositionDigest( GameObjectManager& manager, KitCompositionDigest& outDigest )
    {
        GameStateComponent* pState     = nullptr;
        int32               stateCount = 0;
        manager.forEachComponentOfType<GameStateComponent>( [&pState, &stateCount]( GameStateComponent* pComponent )
        {
            pState = pState == nullptr ? pComponent : pState;
            ++stateCount;
        } );
        if ( pState == nullptr || pState->getOwner() == nullptr )
            return false;
        const KitCompositionFarmDirector* pFarm = pState->getOwner()->getComponent<KitCompositionFarmDirector>();
        const KitCompositionTownDirector* pTown = pState->getOwner()->getComponent<KitCompositionTownDirector>();
        if ( pFarm == nullptr || pTown == nullptr )
            return false;
        outDigest._gold          = pState->getWallet().getBalance( Wallet::getDefaultCurrency() );
        outDigest._hour          = pState->getClock().getHour();
        outDigest._day           = pState->getClock().getDay();
        outDigest._harvested     = pState->getFlags().getFlag( "farm.harvested" );
        outDigest._creatureCount = static_cast<int32>( pTown->getTown().getCreatures().size() );
        outDigest._friendship    = pTown->getTown().getFriendship( "sprout" );
        outDigest._orchardCount  = pTown->getTown().countHabitats( "orchard" );
        outDigest._stateCount    = stateCount;
        outDigest._cropCount     = pFarm->getField().getCropCount();
        outDigest._readyCount    = pFarm->getField().getReadyCount();
        outDigest._requestStatus = pState->getQuestLog().getStatus( "meadow_request" );
        return true;
    }
} // namespace

/**
 * @brief [KitCompositionTest] 밭이 번 돈으로 마을이 같은 틱에 과수원을 사 부탁이 끝난다 — 시계 → 밭 → 마을 순서, 공유 지갑 · 공유 일지, 일지 알림은 게임 몫으로 남는다
 */
SW_TEST_CASE( KitCompositionTest, FarmIncomeBuysTheTownOrchardInTheSameTick )
{
    registerKitCompositionTypes();
    SW_ASSERT_TRUE( KitCompositionCatalogs::get()._bLoaded == SW_TRUE );
    KitCompositionTestScene scene;
    GameObjectManager*      pManager = scene.getManager();
    SW_ASSERT_NOT_NULL( pManager );
    pManager->beginPlay();

    runCompositionTicks( *pManager, KitCompositionTestInternal::kHarvestTicks );
    KitCompositionDigest firstHarvest;
    SW_ASSERT_TRUE( makeCompositionDigest( *pManager, firstHarvest ) );
    SW_EXPECT_EQUAL( 2, firstHarvest._day );
    SW_EXPECT_EQUAL( 2, firstHarvest._harvested );
    SW_EXPECT_EQUAL( int64{ 2 * 30 - 50 }, firstHarvest._gold ); // 판 돈이 같은 틱에 과수원 값으로 나갔다(마을이 밭 앞이면 아직 60)
    SW_EXPECT_EQUAL( 1, firstHarvest._orchardCount );
    SW_EXPECT_EQUAL( 1, firstHarvest._creatureCount );
    SW_EXPECT_TRUE( firstHarvest._requestStatus == QuestStatus::Completed );
    SW_EXPECT_EQUAL( 50, firstHarvest._friendship );

    runCompositionTicks( *pManager, 2 * KitCompositionTestInternal::kHalfRunTicks - KitCompositionTestInternal::kHarvestTicks );
    KitCompositionDigest lastDigest;
    SW_ASSERT_TRUE( makeCompositionDigest( *pManager, lastDigest ) );
    SW_EXPECT_EQUAL( 4, lastDigest._day );
    SW_EXPECT_EQUAL( 4, lastDigest._harvested );
    SW_EXPECT_EQUAL( int64{ 4 * 30 - 50 }, lastDigest._gold ); // 과수원은 한 번만
    SW_EXPECT_EQUAL( 1, lastDigest._orchardCount );

    // 부탁이 끝난 알림은 공유 일지에 남아 게임 화면이 받는다(마을이 꺼내 먹지 않았다)
    GameStateComponent* pState = nullptr;
    pManager->forEachComponentOfType<GameStateComponent>( [&pState]( GameStateComponent* pComponent )
    { pState = pComponent; } );
    SW_ASSERT_NOT_NULL( pState );
    vector<QuestEvent> listEvent;
    pState->getQuestLog().drainEvents( listEvent );
    int32 completedCount = 0;
    for ( const QuestEvent& questEvent : listEvent )
    {
        if ( questEvent._kind == QuestEvent::Kind::Completed && questEvent._questId == hashed_string( "meadow_request" ) )
            ++completedCount;
    }
    SW_EXPECT_EQUAL( 1, completedCount );
    pManager->endPlay();
}

/**
 * @brief [KitCompositionTest] 다른 실행의 세이브(오브젝트 id 가 새로 남) — 두 키트와 공유 상태가 타입 안 순서로 짝지어 되살고, 판이 그대로 이어진다
 */
SW_TEST_CASE( KitCompositionTest, SaveFromAnotherRunRestoresBothKitsAndTheSharedState )
{
    registerKitCompositionTypes();
    SW_ASSERT_TRUE( KitCompositionCatalogs::get()._bLoaded == SW_TRUE );
    KitCompositionTestScene scene;
    GameObjectManager*      pManager = scene.getManager();
    SW_ASSERT_NOT_NULL( pManager );
    pManager->beginPlay();
    runCompositionTicks( *pManager, KitCompositionTestInternal::kHalfRunTicks );
    KitCompositionDigest before;
    SW_ASSERT_TRUE( makeCompositionDigest( *pManager, before ) );

    KitCompositionGameInstance instance;
    vector<uint8>              snapshot;
    SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );
    // 다른 실행의 세이브를 흉내 — 봉투 머리(매직 4 · 판 4) 뒤의 프로세스 표 8 바이트를 바꾼다(`GameInstanceBase::deserializeState`).
    SW_ASSERT_TRUE( 16 <= snapshot.size() );
    for ( size_t byteIndex = 8; byteIndex < 16; ++byteIndex )
    {
        snapshot[byteIndex] = static_cast<uint8>( snapshot[byteIndex] ^ 0xFFu );
    }
    SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
    pManager->mergePendingAdds();
    pManager->tick( 0.0f ); // 되살린 오브젝트가 시작한다 — 시계는 흐르지 않는다

    KitCompositionDigest after;
    SW_ASSERT_TRUE( makeCompositionDigest( *pManager, after ) );
    SW_EXPECT_EQUAL( 1, after._stateCount );
    SW_EXPECT_EQUAL( before._gold, after._gold );
    SW_EXPECT_EQUAL( before._hour, after._hour );
    SW_EXPECT_EQUAL( before._day, after._day );
    SW_EXPECT_EQUAL( before._harvested, after._harvested );
    SW_EXPECT_EQUAL( before._creatureCount, after._creatureCount );
    SW_EXPECT_EQUAL( before._friendship, after._friendship );
    SW_EXPECT_EQUAL( before._orchardCount, after._orchardCount );
    SW_EXPECT_EQUAL( before._cropCount, after._cropCount );
    SW_EXPECT_EQUAL( before._readyCount, after._readyCount );
    SW_EXPECT_TRUE( before._requestStatus == after._requestStatus );

    runCompositionTicks( *pManager, KitCompositionTestInternal::kHalfRunTicks );
    KitCompositionDigest lastDigest;
    SW_ASSERT_TRUE( makeCompositionDigest( *pManager, lastDigest ) );
    SW_EXPECT_EQUAL( 4, lastDigest._day );
    SW_EXPECT_EQUAL( 4, lastDigest._harvested );
    SW_EXPECT_EQUAL( int64{ 4 * 30 - 50 }, lastDigest._gold );
    pManager->endPlay();
}

/**
 * @brief [KitCompositionTest] 핫 리로드 스냅숏(같은 프로세스 — id 를 되살림)으로 판을 걷었다 세워도 끊기지 않은 판과 끝까지 같다
 */
SW_TEST_CASE( KitCompositionTest, HotReloadSnapshotContinuesLikeAnUninterruptedRun )
{
    registerKitCompositionTypes();
    SW_ASSERT_TRUE( KitCompositionCatalogs::get()._bLoaded == SW_TRUE );

    KitCompositionDigest reloaded;
    {
        KitCompositionTestScene scene;
        GameObjectManager*      pManager = scene.getManager();
        SW_ASSERT_NOT_NULL( pManager );
        pManager->beginPlay();
        runCompositionTicks( *pManager, KitCompositionTestInternal::kHalfRunTicks );
        KitCompositionGameInstance instance;
        vector<uint8>              snapshot;
        SW_ASSERT_TRUE( instance.captureSnapshot( snapshot ) );
        SW_ASSERT_TRUE( instance.restoreSnapshot( snapshot ) );
        pManager->mergePendingAdds();
        pManager->tick( 0.0f );
        runCompositionTicks( *pManager, KitCompositionTestInternal::kHalfRunTicks );
        SW_ASSERT_TRUE( makeCompositionDigest( *pManager, reloaded ) );
        pManager->endPlay();
    }

    KitCompositionDigest straight;
    {
        KitCompositionTestScene scene;
        GameObjectManager*      pManager = scene.getManager();
        SW_ASSERT_NOT_NULL( pManager );
        pManager->beginPlay();
        runCompositionTicks( *pManager, KitCompositionTestInternal::kHalfRunTicks );
        pManager->tick( 0.0f ); // 되살린 쪽과 같은 자리에 같은 빈 틱
        runCompositionTicks( *pManager, KitCompositionTestInternal::kHalfRunTicks );
        SW_ASSERT_TRUE( makeCompositionDigest( *pManager, straight ) );
        pManager->endPlay();
    }

    SW_EXPECT_EQUAL( 1, reloaded._stateCount );
    SW_EXPECT_EQUAL( straight._gold, reloaded._gold );
    SW_EXPECT_EQUAL( straight._hour, reloaded._hour );
    SW_EXPECT_EQUAL( straight._day, reloaded._day );
    SW_EXPECT_EQUAL( straight._harvested, reloaded._harvested );
    SW_EXPECT_EQUAL( straight._creatureCount, reloaded._creatureCount );
    SW_EXPECT_EQUAL( straight._friendship, reloaded._friendship );
    SW_EXPECT_EQUAL( straight._orchardCount, reloaded._orchardCount );
    SW_EXPECT_EQUAL( straight._cropCount, reloaded._cropCount );
    SW_EXPECT_EQUAL( straight._readyCount, reloaded._readyCount );
    SW_EXPECT_TRUE( straight._requestStatus == reloaded._requestStatus );
}
