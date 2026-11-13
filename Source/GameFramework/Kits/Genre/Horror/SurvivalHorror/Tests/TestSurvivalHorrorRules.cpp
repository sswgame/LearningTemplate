#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorEncounter.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorSession.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/GridInventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

#include <utility>

// 생존 공포 키트의 규칙: 카탈로그, 정신력과 손전등, 격자 가방 · 상자 · 조합, 세이브 제한, 열쇠 · 다이얼 · 순서 퍼즐, 문서와 단서 보드, 턴제 초자연 전투, 상태 왕복과 꾸민 바이트 거절. 키트 GF_SurvivalHorror(nogpu).
// 꾸민 바이트는 형식을 맞춰 손으로 쓴 뒤(정상 값이 읽히는 것을 함께 본다) 범위 밖 값만 바꿔 넣는다.

using namespace sw;

namespace
{
    constexpr const utf8* kSurvivalHorrorXML = R"(<SurvivalHorrorCatalog>
  <Rules maxHealth="100" maxSanity="100" darknessDrain="10" sanityRegen="5" sanityRegenDelay="2" repeatSightingScale="0.5" hallucinationThreshold="0.3" maxAimSway="2" maxBattery="10" batteryDrain="2" gridWidth="4" gridHeight="2"/>
  <Item id="herb" kind="Heal" heal="25" maxStack="2"/>
  <Item id="calm" kind="Heal" sanity="30"/>
  <Item id="cell" kind="Battery" battery="5"/>
  <Item id="ammo" kind="Ammo" maxStack="10"/>
  <Item id="keyRed" kind="Key"/>
  <Item id="keySpare" kind="Key"/>
  <Item id="ribbon" kind="SaveItem" maxStack="3"/>
  <Item id="mix" kind="Heal" heal="60"/>
  <Item id="big" w="3" h="2"/>
  <Combine id="mixHerbs" a="herb" b="herb" out="mix"/>
  <Combine id="makeBig" a="ammo" b="calm" out="big"/>
  <Monster id="zombie" sanityLoss="20"/>
  <KeyLock id="redDoor" key="keyRed" flag="redOpen"/>
  <KeyLock id="spareDoor" key="keySpare" flag="spareOpen" consumeKey="false"/>
  <DialLock id="safe" code="3 1 4" flag="safeOpen" attempts="2"/>
  <DialLock id="free" code="1" flag="freeOpen"/>
  <Sequence id="bells" steps="low,high" flag="bellsDone" mistakeSanity="5"/>
  <Document id="diary" title="Diary" clues="knife,gloves"/>
  <Document id="note"/>
  <Deduction id="culprit" answer="butler" links="knife-gloves" flag="solved" wrongSanity="15"/>
</SurvivalHorrorCatalog>)";

    constexpr const utf8* kSurvivalHorrorGraphXML = R"(<AreaGraph><Area id="hall"/><Area id="cellar"/><Area id="attic"/>
  <Link from="hall" to="cellar" requires="redOpen"/></AreaGraph>)";

    constexpr float32 kHorrorTolerance = 1.0e-3f;

    /** @brief 세션의 상태 바이트를 손으로 씁니다. 게이지는 지금 세션의 것을 그대로 싣습니다. 기본값은 정상 상태입니다. */
    struct SurvivalFixtureSession
    {
        vector<hashed_string>                   _listMonster{};
        vector<hashed_string>                   _listDocument{};
        vector<hashed_string>                   _listClue{};
        vector<hashed_string>                   _listSolved{};
        vector<std::pair<hashed_string, int32>> _listDial{};
        vector<std::pair<hashed_string, int32>> _listSequence{};
        vector<SurvivalHorrorClueLink>          _listLink{};
        hashed_string                           _area{ "hall" };
        float32                                 _health{ 100.0f };
        int32                                   _saveCount{ 0 };
        int32                                   _wrongCount{ 0 };
        uint8                                   _flashlight{ SW_FALSE };

        static void writeNames( Archive& outArchive, const vector<hashed_string>& listName )
        {
            outArchive << static_cast<uint32>( listName.size() );
            for ( const hashed_string& name : listName )
            {
                StateIO::writeName( outArchive, name );
            }
        }

        static void writeCounts( Archive& outArchive, const vector<std::pair<hashed_string, int32>>& listEntry )
        {
            outArchive << static_cast<uint32>( listEntry.size() );
            for ( const std::pair<hashed_string, int32>& entry : listEntry )
            {
                StateIO::writeName( outArchive, entry.first );
                outArchive << entry.second;
            }
        }

        [[nodiscard]] bool read( SurvivalHorrorSession& inoutSession ) const
        {
            Archive written;
            inoutSession.getSanity().writeState( written );
            inoutSession.getBattery().writeState( written );
            writeNames( written, _listMonster );
            writeNames( written, _listDocument );
            writeNames( written, _listClue );
            writeNames( written, _listSolved );
            writeCounts( written, _listDial );
            writeCounts( written, _listSequence );
            written << static_cast<uint32>( _listLink.size() );
            for ( const SurvivalHorrorClueLink& link : _listLink )
            {
                StateIO::writeName( written, link._first );
                StateIO::writeName( written, link._second );
            }
            StateIO::writeName( written, _area );
            written << _health;
            written << _saveCount;
            written << _wrongCount;
            written << _flashlight;
            written << static_cast<uint8>( SW_FALSE );
            Archive reading( written.getData(), written.getSize() );
            return inoutSession.readState( reading );
        }
    };

    /** @brief 한 판입니다. 지도, 플래그, 격자 가방, 상자는 빌려 줍니다. */
    struct SurvivalHorrorRulesTest
    {
        SurvivalHorrorCatalog       _catalog{};
        AreaGraph                   _graph{};
        GameFlags                   _flags{};
        GridInventory               _grid{};
        Inventory                   _box{};
        SurvivalHorrorSession       _session{};
        vector<SurvivalHorrorEvent> _listEvent{};

        SurvivalHorrorRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kSurvivalHorrorXML, "SurvivalHorrorRulesTest" ) );
            SW_REQUIRE_TRUE( _graph.loadFromXMLText( kSurvivalHorrorGraphXML, "SurvivalHorrorRulesTest" ) );
            SW_REQUIRE_TRUE( _box.initialize( nullptr, 4 ) );
            SW_REQUIRE_TRUE( open() );
        }

        bool open( const hashed_string& startArea = "hall" ) { return open( _session, startArea ); }

        bool open( SurvivalHorrorSession& outSession, const hashed_string& startArea = "hall" )
        {
            SessionStateRefs refs;
            refs._pFlags = &_flags;
            return outSession.initialize( &_catalog, &_graph, startArea, refs, _grid, _box );
        }

        /** @brief 규칙 하나를 바꿔 새 판을 엽니다. */
        template <typename TChange>
        bool reopen( TChange&& change )
        {
            SurvivalHorrorRules rules = _catalog.getRules();
            change( rules );
            _catalog.setRules( rules );
            return open();
        }

        /** @brief 쌓인 이벤트를 `_listEvent` 뒤로 모으고 @p kind 의 수를 셉니다. */
        int32 countEvents( SurvivalHorrorEvent::Kind kind )
        {
            _session.pollEvents( _listEvent );
            return VectorUtil::countBy<&SurvivalHorrorEvent::_kind>( _listEvent, kind );
        }

        /** @brief 아이템을 넣고 그 슬롯 번호를 반환합니다. */
        int32 give( const hashed_string& itemID, int32 count = 1 )
        {
            SW_EXPECT_EQUAL( count, _grid.addItem( itemID, count ) );
            for ( const GridItem& item : _grid.getItems() )
            {
                if ( item._itemID == itemID )
                    return item._instanceID;
            }
            return -1;
        }
    };

    /** @brief 초자연 전투의 재료입니다. */
    struct SurvivalHorrorTest
    {
        SurvivalHorrorMonsterDef _monster{};

        SurvivalHorrorTest()
        {
            _monster._id        = "ghoul";
            _monster._speed     = 1.0f;
            _monster._toughness = 3;
            _monster._damage    = 1;
            _monster._horror    = 0;
        }

        /** @brief 빠른 조사자 하나입니다. 먼저 움직입니다. */
        static SurvivalHorrorInvestigator makeInvestigator( int32 actorID, int32 health = 5, int32 strength = 2 )
        {
            SurvivalHorrorInvestigator investigator;
            investigator._speed    = 2.0f;
            investigator._actorID  = actorID;
            investigator._health   = health;
            investigator._strength = strength;
            return investigator;
        }

        /** @brief 전투의 상태 바이트를 손으로 씁니다. 차례는 @p order 를 그대로 싣습니다. */
        static bool readFixture( SurvivalHorrorEncounter& inoutEncounter, const hashed_string& monsterID, const SurvivalHorrorInvestigator& investigator, const TurnOrder& order, int32 toughness, SurvivalHorrorEncounterState state )
        {
            Archive written;
            StateIO::writeName( written, monsterID );
            written << static_cast<uint32>( 1 );
            written << investigator._speed;
            written << investigator._actorID;
            written << investigator._health;
            written << investigator._sanity;
            written << investigator._will;
            written << investigator._strength;
            written << investigator._bDefeated;
            order.writeState( written );
            const GameRandom random;
            StateIO::writeRandom( written, random );
            written << toughness;
            written << static_cast<int32>( 0 );
            written << static_cast<uint8>( state );
            Archive reading( written.getData(), written.getSize() );
            return inoutEncounter.readState( reading );
        }

        /** @brief 조사자 @p actorID 와 괴물이 든 차례입니다. */
        static TurnOrder makeOrder( int32 actorID )
        {
            TurnOrder order;
            order.initialize( TurnOrderMode::Rounds, 7u );
            order.addActor( actorID, 2.0f );
            order.addActor( SurvivalHorrorEncounter::kMonsterActorID, 1.0f );
            return order;
        }
    };
} // namespace

// ── 카탈로그 ──────────────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorTest] 원소마다 읽는다. 조합은 재료 순서를 가리지 않고, 격자 모양은 아이템 정의에서 온다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogReadsEveryElement )
{
    SurvivalHorrorCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSurvivalHorrorXML, "SurvivalHorrorTest" ) );

    SW_EXPECT_EQUAL( 4, catalog.getRules()._gridWidth );
    SW_REQUIRE_TRUE( catalog.findItem( "herb" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findItem( "herb" )->_kind == SurvivalHorrorItemKind::Heal );
    SW_EXPECT_NEAR( 25.0f, catalog.findItem( "herb" )->_healAmount, kHorrorTolerance );
    SW_REQUIRE_TRUE( catalog.findKeyLock( "spareDoor" ) != nullptr );
    SW_EXPECT_FALSE( catalog.findKeyLock( "spareDoor" )->_bConsumeKey );
    SW_EXPECT_TRUE( catalog.findKeyLock( "redDoor" )->_bConsumeKey );
    SW_REQUIRE_TRUE( catalog.findDialLock( "safe" ) != nullptr );
    SW_EXPECT_EQUAL( size_t{ 3 }, catalog.findDialLock( "safe" )->_listDigit.size() );
    SW_EXPECT_EQUAL( 4, catalog.findDialLock( "safe" )->_listDigit[2] );
    SW_REQUIRE_TRUE( catalog.findDeduction( "culprit" ) != nullptr );
    SW_REQUIRE_EQUAL( size_t{ 1 }, catalog.findDeduction( "culprit" )->_listRequiredLink.size() );
    SW_EXPECT_TRUE( catalog.findDeduction( "culprit" )->_listRequiredLink[0].isSame( "gloves", "knife" ) );

    const RecipeDef* pRecipe = catalog.findCombine( "calm", "ammo" );
    SW_REQUIRE_TRUE( pRecipe != nullptr );
    SW_EXPECT_TRUE( pRecipe->_id == hashed_string( "makeBig" ) );
    SW_EXPECT_TRUE( catalog.findCombine( "herb", "herb" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findCombine( "calm", "calm" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findCombine( "herb", "calm" ) == nullptr );

    GridInventory grid;
    SW_REQUIRE_TRUE( grid.initialize( catalog.makeShapeLookup(), 3, 2 ) );
    SW_EXPECT_EQUAL( 1, grid.addItem( "big", 1 ) );
    SW_EXPECT_EQUAL( 0, grid.countFreeCells() );
    SW_EXPECT_EQUAL( 0, grid.addItem( "nothing", 1 ) ); // 모르는 아이템은 놓을 수 없다
}

/**
 * @brief [SurvivalHorrorTest] 결함: 손으로 읽는 원소는 `add` 로 앞 정의를 말없이 덮어썼다(주석은 "뒤의 것을 버린다"). 다이얼에서 겹친 id 를 거절한다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDuplicateDialLock )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><DialLock id="a" code="1" flag="first"/><DialLock id="a" code="2" flag="second"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_REQUIRE_TRUE( catalog.findDialLock( "a" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findDialLock( "a" )->_flag == hashed_string( "first" ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 순서 퍼즐도 같다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDuplicateSequence )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Sequence id="a" steps="x" flag="first"/><Sequence id="a" steps="y" flag="second"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_EXPECT_TRUE( catalog.findSequence( "a" )->_flag == hashed_string( "first" ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 문서도 같다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDuplicateDocument )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Document id="a" clues="x"/><Document id="a" clues="y,z"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, catalog.findDocument( "a" )->_listClue.size() );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 추리도 같다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDuplicateDeduction )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Deduction id="a" answer="first"/><Deduction id="a" answer="second"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_EXPECT_TRUE( catalog.findDeduction( "a" )->_answer == hashed_string( "first" ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 조합도 같다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDuplicateCombine )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Combine id="a" a="x" b="y" out="first"/><Combine id="a" a="p" b="q" out="second"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_EXPECT_TRUE( catalog.findCombine( "x", "y" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findCombine( "p", "q" ) == nullptr );
}

/**
 * @brief [SurvivalHorrorTest] 스키마 읽기 원소의 겹친 id 도 거절한다(`GameDataXML::readInto`)
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDuplicateItem )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Item id="a" w="2"/><Item id="a" w="3"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_EXPECT_EQUAL( 2, catalog.findItem( "a" )->_width );
}

/**
 * @brief [SurvivalHorrorTest] 다이얼 번호가 깨지면 그 자물쇠를 넣지 않는다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesBadDialDigit )
{
    SW_EXPECT_LOG( "SurvivalHorrorCatalog.DialLockBadDigit" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><DialLock id="a" code="1 x" flag="f"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
    SW_EXPECT_TRUE( catalog.findDialLock( "a" ) == nullptr );
}

/**
 * @brief [SurvivalHorrorTest] 번호 없는 다이얼은 풀 수 없으므로 넣지 않는다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesDialWithoutCode )
{
    SW_EXPECT_LOG( "SurvivalHorrorCatalog.DialLockNeedsCode" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><DialLock id="a" flag="f"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
}

/**
 * @brief [SurvivalHorrorTest] 걸음 없는 순서 퍼즐은 넣지 않는다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesSequenceWithoutSteps )
{
    SW_EXPECT_LOG( "SurvivalHorrorCatalog.SequenceNeedsStepsB" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Sequence id="a" flag="f"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
}

/**
 * @brief [SurvivalHorrorTest] 모양이 틀린 단서 연결은 그 추리를 넣지 않는다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesMalformedLink )
{
    SW_EXPECT_LOG( "SurvivalHorrorCatalog.DeductionBadLinkExpected" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Deduction id="a" answer="x" links="knife"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 단서를 자기 자신과 잇는 연결을 받았다. 보드는 그런 연결을 받지 않으므로 그 추리는 풀 수 없었다 — 거절한다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesSelfLink )
{
    SW_EXPECT_LOG( "SurvivalHorrorCatalog.DeductionBadLinkExpected" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Deduction id="a" answer="x" links="knife-knife"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
}

/**
 * @brief [SurvivalHorrorTest] 자물쇠가 가리키는 열쇠는 뒤에 적혀도 되고, 끝까지 없으면 로드 실패다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogReportsLockWithUnknownKey )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><KeyLock id="a" key="later" flag="f"/><KeyLock id="b" key="nothing" flag="g"/><Item id="later"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
}

/**
 * @brief [SurvivalHorrorTest] 모르는 원소와 범위 밖 값은 오류이다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesUnknownElement )
{
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( "<SurvivalHorrorCatalog><Zombie/></SurvivalHorrorCatalog>", "SurvivalHorrorTest" ) );
}

/**
 * @brief [SurvivalHorrorTest] 범위 밖 격자 크기의 아이템은 넣지 않는다
 */
SW_TEST_CASE( SurvivalHorrorTest, CatalogRefusesOutOfRangeItem )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SurvivalHorrorCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<SurvivalHorrorCatalog><Item id="a" w="9"/></SurvivalHorrorCatalog>)", "SurvivalHorrorTest" ) );
}

// ── 세션 시작 ─────────────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorRulesTest] 시작 거절: 카탈로그 없음
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, InitializeRefusesNoCatalog )
{
    SW_EXPECT_LOG( "SurvivalHorrorSession.SurvivalHorrorSessionRefusedNoCatalog" );
    SurvivalHorrorSession session;
    SW_EXPECT_FALSE( session.initialize( nullptr, &_graph, "hall", SessionStateRefs{}, _grid, _box ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 그래프에 없는 방에서 시작해도 받았고, 그러면 어느 길도 지날 수 없어 판이 멈췄다 — 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, InitializeRefusesUnknownStartArea )
{
    SW_EXPECT_LOG( "SurvivalHorrorSession.UnknownStartArea" );
    SurvivalHorrorSession session;
    SW_EXPECT_FALSE( open( session, "nowhere" ) );
    SW_EXPECT_TRUE( _session.getCurrentArea() == hashed_string( "hall" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 범위 밖 게이지 규칙은 오류이고 세션은 그대로이다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, InitializeRefusesBadGauge )
{
    SW_EXPECT_LOG( "ResourceGauge.ResourceGaugeRefusedSettings" );
    _session.applyDamage( 10.0f );
    SW_EXPECT_FALSE( reopen( []( SurvivalHorrorRules& rules )
    { rules._maxSanity = 0.0f; } ) );
    SW_EXPECT_NEAR( 90.0f, _session.getHealth(), kHorrorTolerance );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 격자 크기가 거절돼도 빌린 포인터와 카탈로그를 먼저 바꿔 두어 "세션은 그대로" 가 거짓이었다 — 받아들여진 뒤에 바꾼다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, InitializeRefusesBadGridKeepsSession )
{
    SW_EXPECT_LOG( "GridInventory.GridInventoryRefusedXCells" );
    SurvivalHorrorSession session;
    SurvivalHorrorRules   rules = _catalog.getRules();
    rules._gridWidth            = 0;
    _catalog.setRules( rules );
    SW_EXPECT_FALSE( open( session ) );
    Archive written;
    _session.writeState( written );
    Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( session.readState( reading ) ); // 카탈로그를 받지 않았다
}

/**
 * @brief [SurvivalHorrorRulesTest] 시작하면 체력 · 정신력 · 배터리가 가득 차고 시작 방에 들어가며, 다시 열면 앞 판을 지운다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, InitializeFillsAndResets )
{
    SW_EXPECT_NEAR( 100.0f, _session.getHealth(), kHorrorTolerance );
    SW_EXPECT_NEAR( 100.0f, _session.getSanity().getValue(), kHorrorTolerance );
    SW_EXPECT_NEAR( 10.0f, _session.getBattery().getValue(), kHorrorTolerance );
    SW_EXPECT_TRUE( _graph.isVisited( "hall" ) );
    SW_EXPECT_EQUAL( 4, _grid.getWidth() );

    _session.applyDamage( 40.0f );
    _session.addClue( "knife" );
    (void)_session.trySave();
    SW_REQUIRE_TRUE( open() );
    SW_EXPECT_NEAR( 100.0f, _session.getHealth(), kHorrorTolerance );
    SW_EXPECT_FALSE( _session.hasClue( "knife" ) );
    SW_EXPECT_EQUAL( 0, _session.getSaveCount() );
}

// ── 정신력과 손전등 ───────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorRulesTest] 어둠은 깎고, 밝은 곳은 지연 뒤에 찬다. 0 이하 시간은 흐르지 않는다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, DarknessDrainsAndLightRegens )
{
    _session.update( 1.0f, true );
    SW_EXPECT_NEAR( 90.0f, _session.getSanity().getValue(), kHorrorTolerance );
    _session.update( 0.0f, true );
    _session.update( -1.0f, true );
    SW_EXPECT_NEAR( 90.0f, _session.getSanity().getValue(), kHorrorTolerance );

    _session.update( 1.0f, false ); // 지연 2 초 안이다
    SW_EXPECT_NEAR( 90.0f, _session.getSanity().getValue(), kHorrorTolerance );
    _session.update( 1.5f, false ); // 지연 뒤 0.5 초 x 5
    SW_EXPECT_NEAR( 92.5f, _session.getSanity().getValue(), kHorrorTolerance );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 회복은 어둠에서도 돌았다. 회복 지연이 걸음보다 짧으면 어둠의 감소에서 회복만큼 덜 깎였다 — 밝은 곳에서만 돈다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, DarknessDoesNotRegen )
{
    SW_REQUIRE_TRUE( reopen( []( SurvivalHorrorRules& rules )
    { rules._sanityRegenDelay = 0.0f; } ) );
    _session.update( 1.0f, true );
    SW_EXPECT_NEAR( 90.0f, _session.getSanity().getValue(), kHorrorTolerance );
    _session.update( 1.0f, false );
    SW_EXPECT_NEAR( 95.0f, _session.getSanity().getValue(), kHorrorTolerance );
}

/**
 * @brief [SurvivalHorrorRulesTest] 손전등은 어둠을 막고 배터리를 쓴다. 다 되면 꺼지고 그 걸음부터 어둠이 깎는다. 배터리 아이템으로 다시 켤 수 있다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, FlashlightUsesBatteryAndDies )
{
    SW_REQUIRE_TRUE( _session.trySetFlashlight( true ) );
    SW_EXPECT_TRUE( _session.isFlashlightOn() );
    _session.update( 1.0f, true );
    SW_EXPECT_NEAR( 8.0f, _session.getBattery().getValue(), kHorrorTolerance );
    SW_EXPECT_NEAR( 100.0f, _session.getSanity().getValue(), kHorrorTolerance );

    _session.update( 4.0f, true );
    SW_EXPECT_FALSE( _session.isFlashlightOn() );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::FlashlightDied ) );
    SW_EXPECT_NEAR( 60.0f, _session.getSanity().getValue(), kHorrorTolerance );
    SW_EXPECT_FALSE( _session.trySetFlashlight( true ) );
    SW_EXPECT_TRUE( _session.trySetFlashlight( false ) );

    SW_REQUIRE_TRUE( _session.tryUseItem( give( "cell" ) ) );
    SW_EXPECT_NEAR( 5.0f, _session.getBattery().getValue(), kHorrorTolerance );
    SW_EXPECT_TRUE( _session.trySetFlashlight( true ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 괴물: 처음은 전부, 다음부터는 배율만큼 깎인다. 모르는 괴물은 0 이다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, WitnessingMonsters )
{
    SW_EXPECT_NEAR( 20.0f, _session.witnessMonster( "zombie" ), kHorrorTolerance );
    SW_EXPECT_NEAR( 10.0f, _session.witnessMonster( "zombie" ), kHorrorTolerance );
    SW_EXPECT_NEAR( 0.0f, _session.witnessMonster( "nothing" ), kHorrorTolerance );
    SW_EXPECT_NEAR( 70.0f, _session.getSanity().getValue(), kHorrorTolerance );
    SW_EXPECT_EQUAL( 2, countEvents( SurvivalHorrorEvent::Kind::SanityLost ) );
    SW_EXPECT_TRUE( _listEvent.back()._id == hashed_string( "zombie" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 정신력이 임계 아래면 환각이고 조준이 흔들린다. 회복하면 끝난다. 0 아래로 내려가지 않는다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, HallucinationFollowsSanity )
{
    SW_EXPECT_NEAR( 1.0f, _session.computeAimSwayScale(), kHorrorTolerance );
    SW_EXPECT_FALSE( _session.isHallucinating() );
    _session.loseSanity( 75.0f, "test" );
    SW_EXPECT_TRUE( _session.isHallucinating() );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::HallucinationStarted ) );
    SW_EXPECT_NEAR( 2.5f, _session.computeAimSwayScale(), kHorrorTolerance );

    SW_REQUIRE_TRUE( _session.tryUseItem( give( "calm" ) ) );
    SW_EXPECT_FALSE( _session.isHallucinating() );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::HallucinationEnded ) );

    _session.loseSanity( 999.0f, "test" );
    SW_EXPECT_NEAR( 0.0f, _session.getSanity().getValue(), kHorrorTolerance );
    SW_EXPECT_NEAR( 3.0f, _session.computeAimSwayScale(), kHorrorTolerance );
}

// ── 가방, 상자, 조합 ─────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorRulesTest] 회복은 최대에서 멈추고 하나를 쓴다. 쓸 수 없는 아이템과 없는 슬롯은 false 이다. 음수 피해는 아무 일도 없다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, UsingItems )
{
    const int32 herb = give( "herb", 2 );
    _session.applyDamage( 50.0f );
    _session.applyDamage( -5.0f );
    SW_EXPECT_NEAR( 50.0f, _session.getHealth(), kHorrorTolerance );
    SW_REQUIRE_TRUE( _session.tryUseItem( herb ) );
    SW_EXPECT_NEAR( 75.0f, _session.getHealth(), kHorrorTolerance );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "herb" ) );
    _session.applyDamage( -1.0f );
    SW_REQUIRE_TRUE( _session.tryUseItem( herb ) );
    SW_EXPECT_NEAR( 100.0f, _session.getHealth(), kHorrorTolerance );
    SW_EXPECT_EQUAL( 0, _grid.getItemCount( "herb" ) );

    SW_EXPECT_FALSE( _session.tryUseItem( herb ) );
    SW_EXPECT_FALSE( _session.tryUseItem( give( "keyRed" ) ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "keyRed" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 죽은 뒤에 약을 먹으면 되살아났다. 체력이 0 이면 아이템을 쓸 수 없고, 약도 그대로 있다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, DeadCannotUseItems )
{
    const int32 herb = give( "herb" );
    _session.applyDamage( 999.0f );
    SW_EXPECT_TRUE( _session.isDead() );
    SW_EXPECT_FALSE( _session.tryUseItem( herb ) );
    SW_EXPECT_TRUE( _session.isDead() );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "herb" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 탄약은 모두 있을 때만 쓴다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, ConsumingAmmo )
{
    (void)give( "ammo", 3 );
    SW_EXPECT_FALSE( _session.tryConsumeAmmo( "ammo", 4 ) );
    SW_EXPECT_EQUAL( 3, _grid.getItemCount( "ammo" ) );
    SW_EXPECT_TRUE( _session.tryConsumeAmmo( "ammo", 2 ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "ammo" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 상자: 있는 만큼만 넣고 꺼낸다. 깨진 개수와 없는 슬롯은 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, ItemBoxMovesItems )
{
    const int32 ammo = give( "ammo", 3 );
    SW_EXPECT_FALSE( _session.storeInBox( ammo, 0 ) );
    SW_EXPECT_FALSE( _session.storeInBox( ammo, -1 ) );
    SW_EXPECT_FALSE( _session.storeInBox( ammo, 4 ) );
    SW_EXPECT_FALSE( _session.storeInBox( 999, 1 ) );
    SW_EXPECT_TRUE( _session.storeInBox( ammo, 3 ) );
    SW_EXPECT_EQUAL( 3, _box.getItemCount( "ammo" ) );
    SW_EXPECT_EQUAL( 0, _grid.getItemCount( "ammo" ) );

    SW_EXPECT_EQUAL( 0, _session.takeFromBox( "ammo", -1 ) );
    SW_EXPECT_EQUAL( 0, _session.takeFromBox( "herb", 1 ) );
    SW_EXPECT_EQUAL( 3, _session.takeFromBox( "ammo", 9 ) );
    SW_EXPECT_EQUAL( 0, _box.getItemCount( "ammo" ) );
    SW_EXPECT_EQUAL( 3, _grid.getItemCount( "ammo" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 조합: 같은 슬롯의 둘도 된다. 하나뿐이면 안 된다. 레시피가 없으면 아무것도 바꾸지 않는다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, CombiningItems )
{
    const int32 herb = give( "herb", 2 );
    SW_EXPECT_FALSE( _session.combineItems( herb, 999 ) );
    SW_EXPECT_FALSE( _session.combineItems( herb, give( "keyRed" ) ) );
    SW_EXPECT_EQUAL( 2, _grid.getItemCount( "herb" ) );

    SW_EXPECT_TRUE( _session.combineItems( herb, herb ) );
    SW_EXPECT_EQUAL( 0, _grid.getItemCount( "herb" ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "mix" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::Combined ) );

    const int32 lone = give( "herb" );
    SW_EXPECT_FALSE( _session.combineItems( lone, lone ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "herb" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결과를 둘 자리가 없으면 없던 일로 한다(재료가 그대로 돌아온다)
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, CombineRollsBackWhenOutputDoesNotFit )
{
    // 4 x 2 격자의 (1, 1) 에 열쇠가 있어 3 x 2 결과가 들어갈 자리가 없다.
    SW_REQUIRE_TRUE( _grid.placeItem( "keyRed", 1, 1, 1, false ) >= 0 );
    const int32 ammo = give( "ammo" );
    const int32 calm = give( "calm" );
    SW_EXPECT_FALSE( _session.combineItems( ammo, calm ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "ammo" ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "calm" ) );
    SW_EXPECT_EQUAL( 0, _grid.getItemCount( "big" ) );
    SW_EXPECT_EQUAL( 0, countEvents( SurvivalHorrorEvent::Kind::Combined ) );
}

// ── 세이브 ────────────────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorRulesTest] 세이브 제한 없음: 언제나 되고 수를 센다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SaveUnlimited )
{
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::Ok );
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::Ok );
    SW_EXPECT_EQUAL( 2, _session.getSaveCount() );
    SW_EXPECT_EQUAL( 2, countEvents( SurvivalHorrorEvent::Kind::Saved ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 횟수 제한: 정한 수까지만 된다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SaveLimited )
{
    SW_REQUIRE_TRUE( reopen( []( SurvivalHorrorRules& rules )
    {
        rules._saveMode = SurvivalHorrorSaveMode::Limited;
        rules._maxSaves = 1;
    } ) );
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::Ok );
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::NoSavesLeft );
    SW_EXPECT_EQUAL( 1, _session.getSaveCount() );
}

/**
 * @brief [SurvivalHorrorRulesTest] 잉크 리본: 세이브마다 하나를 쓰고, 없으면 안 된다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SaveInkRibbon )
{
    SW_REQUIRE_TRUE( reopen( []( SurvivalHorrorRules& rules )
    { rules._saveMode = SurvivalHorrorSaveMode::InkRibbon; } ) );
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::NoSaveItem );
    (void)give( "keyRed" );
    (void)give( "ribbon" );
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::Ok );
    SW_EXPECT_EQUAL( 0, _grid.getItemCount( "ribbon" ) );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "keyRed" ) );
    SW_EXPECT_TRUE( _session.trySave() == SurvivalHorrorSaveResult::NoSaveItem );
}

// ── 퍼즐, 지도 ───────────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorRulesTest] 열쇠: 없으면 못 열고, 열면 플래그가 서서 길이 열린다. 쓰는 열쇠만 없어진다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, KeyLocksOpenPaths )
{
    SW_EXPECT_TRUE( _session.useKey( "nothing" ) == SurvivalHorrorPuzzleResult::Unknown );
    SW_EXPECT_TRUE( _session.useKey( "redDoor" ) == SurvivalHorrorPuzzleResult::MissingItem );
    SW_EXPECT_FALSE( _session.tryMoveTo( "cellar" ) );
    SW_EXPECT_TRUE( _session.getCurrentArea() == hashed_string( "hall" ) );

    (void)give( "keyRed" );
    SW_EXPECT_TRUE( _session.useKey( "redDoor" ) == SurvivalHorrorPuzzleResult::Solved );
    SW_EXPECT_EQUAL( 0, _grid.getItemCount( "keyRed" ) );
    SW_EXPECT_TRUE( _flags.hasFlag( "redOpen" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::DoorUnlocked ) );
    SW_EXPECT_TRUE( _session.useKey( "redDoor" ) == SurvivalHorrorPuzzleResult::AlreadySolved );

    SW_EXPECT_TRUE( _session.tryMoveTo( "cellar" ) );
    SW_EXPECT_TRUE( _session.getCurrentArea() == hashed_string( "cellar" ) );
    SW_EXPECT_TRUE( _graph.isVisited( "cellar" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::AreaEntered ) );
    SW_EXPECT_FALSE( _session.tryMoveTo( "attic" ) ); // 이어지지 않았다
    SW_EXPECT_FALSE( _session.tryMoveTo( "nowhere" ) );

    (void)give( "keySpare" );
    SW_EXPECT_TRUE( _session.useKey( "spareDoor" ) == SurvivalHorrorPuzzleResult::Solved );
    SW_EXPECT_EQUAL( 1, _grid.getItemCount( "keySpare" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 플래그를 빌리지 않은 세션은 조건 있는 길이 닫힌 채이다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, NoFlagsKeepsConditionalPathsClosed )
{
    _flags.setFlag( "redOpen", 1 );
    SurvivalHorrorSession session;
    SW_REQUIRE_TRUE( session.initialize( &_catalog, &_graph, "hall", SessionStateRefs{}, _grid, _box ) );
    SW_EXPECT_FALSE( session.tryMoveTo( "cellar" ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 다이얼: 틀린 수가 상한에 닿으면 잠겨 맞는 번호도 받지 않는다. 상한이 없으면 계속 시도한다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, DialLockAttempts )
{
    const vector<int32> listRight{ 3, 1, 4 };
    const vector<int32> listWrong{ 3, 1, 5 };
    SW_EXPECT_TRUE( _session.enterDialCode( "nothing", listRight ) == SurvivalHorrorPuzzleResult::Unknown );
    SW_EXPECT_TRUE( _session.enterDialCode( "safe", listWrong ) == SurvivalHorrorPuzzleResult::Wrong );
    SW_EXPECT_TRUE( _session.enterDialCode( "safe", vector<int32>{} ) == SurvivalHorrorPuzzleResult::LockedOut );
    SW_EXPECT_TRUE( _session.enterDialCode( "safe", listRight ) == SurvivalHorrorPuzzleResult::LockedOut );
    SW_EXPECT_FALSE( _flags.hasFlag( "safeOpen" ) );

    const vector<int32> listFreeRight{ 1 };
    for ( int32 attempt = 0; attempt < 5; ++attempt )
    {
        SW_EXPECT_TRUE( _session.enterDialCode( "free", listWrong ) == SurvivalHorrorPuzzleResult::Wrong );
    }
    SW_EXPECT_TRUE( _session.enterDialCode( "free", listFreeRight ) == SurvivalHorrorPuzzleResult::Solved );
    SW_EXPECT_TRUE( _flags.hasFlag( "freeOpen" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::PuzzleSolved ) );
    SW_EXPECT_TRUE( _session.enterDialCode( "free", listWrong ) == SurvivalHorrorPuzzleResult::AlreadySolved );
}

/**
 * @brief [SurvivalHorrorRulesTest] 순서 퍼즐: 틀리면 처음부터이고 벌칙을 받는다. 끝까지 맞히면 풀린다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SequencePuzzleSteps )
{
    SW_EXPECT_TRUE( _session.pressSequenceStep( "nothing", "low" ) == SurvivalHorrorPuzzleResult::Unknown );
    SW_EXPECT_TRUE( _session.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Progress );
    SW_EXPECT_TRUE( _session.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Wrong );
    SW_EXPECT_NEAR( 95.0f, _session.getSanity().getValue(), kHorrorTolerance );
    SW_EXPECT_TRUE( _session.pressSequenceStep( "bells", "high" ) == SurvivalHorrorPuzzleResult::Wrong ); // 처음부터이다
    SW_EXPECT_TRUE( _session.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Progress );
    SW_EXPECT_TRUE( _session.pressSequenceStep( "bells", "high" ) == SurvivalHorrorPuzzleResult::Solved );
    SW_EXPECT_TRUE( _flags.hasFlag( "bellsDone" ) );
    SW_EXPECT_TRUE( _session.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::AlreadySolved );
}

/**
 * @brief [SurvivalHorrorRulesTest] 문서: 처음 읽을 때만 단서가 생긴다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, DocumentsGiveClues )
{
    SW_EXPECT_FALSE( _session.readDocument( "nothing" ) );
    SW_EXPECT_TRUE( _session.readDocument( "diary" ) );
    SW_EXPECT_TRUE( _session.hasClue( "knife" ) );
    SW_EXPECT_TRUE( _session.hasClue( "gloves" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::DocumentRead ) );
    SW_EXPECT_EQUAL( 2, countEvents( SurvivalHorrorEvent::Kind::ClueGained ) );
    SW_EXPECT_FALSE( _session.readDocument( "diary" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::DocumentRead ) );
    _session.addClue( hashed_string{} ); // 빈 단서는 없다
    SW_EXPECT_EQUAL( 2, countEvents( SurvivalHorrorEvent::Kind::ClueGained ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 보드: 가진 단서끼리만, 자기 자신과 겹침 없이 잇는다. 순서는 상관없다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, ClueBoardLinks )
{
    SW_EXPECT_FALSE( _session.linkClues( "knife", "gloves" ) ); // 아직 없다
    SW_REQUIRE_TRUE( _session.readDocument( "diary" ) );
    SW_EXPECT_FALSE( _session.linkClues( "knife", "knife" ) );
    SW_EXPECT_FALSE( _session.linkClues( "knife", "pantry" ) );
    SW_EXPECT_TRUE( _session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_FALSE( _session.linkClues( "gloves", "knife" ) );
    SW_EXPECT_TRUE( _session.isLinked( "gloves", "knife" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, _session.getClueLinks().size() );
    SW_EXPECT_TRUE( _session.unlinkClues( "gloves", "knife" ) );
    SW_EXPECT_FALSE( _session.unlinkClues( "gloves", "knife" ) );
    SW_EXPECT_TRUE( _session.getClueLinks().empty() );
}

/**
 * @brief [SurvivalHorrorRulesTest] 추리: 단서가 모자라면 벌칙 없이 돌려보낸다. 답이 맞아도 연결이 없으면 틀리고 벌칙을 받으며 보드를 지운다. 연결과 답이 모두 맞아야 풀린다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, DeductionNeedsCluesLinksAndAnswer )
{
    SW_EXPECT_TRUE( _session.submitDeduction( "nothing", "butler" ) == SurvivalHorrorPuzzleResult::Unknown );
    SW_EXPECT_TRUE( _session.submitDeduction( "culprit", "butler" ) == SurvivalHorrorPuzzleResult::MissingItem );
    SW_EXPECT_NEAR( 100.0f, _session.getSanity().getValue(), kHorrorTolerance );

    SW_REQUIRE_TRUE( _session.readDocument( "diary" ) );
    SW_EXPECT_TRUE( _session.submitDeduction( "culprit", "butler" ) == SurvivalHorrorPuzzleResult::Wrong ); // 찍어서 맞히기를 막는다
    SW_EXPECT_NEAR( 85.0f, _session.getSanity().getValue(), kHorrorTolerance );
    SW_EXPECT_EQUAL( 1, _session.getWrongDeductionCount() );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::DeductionFailed ) );

    SW_REQUIRE_TRUE( _session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_TRUE( _session.submitDeduction( "culprit", "maid" ) == SurvivalHorrorPuzzleResult::Wrong );
    SW_EXPECT_TRUE( _session.getClueLinks().empty() ); // 틀리면 지운다

    SW_REQUIRE_TRUE( _session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_TRUE( _session.submitDeduction( "culprit", "butler" ) == SurvivalHorrorPuzzleResult::Solved );
    SW_EXPECT_TRUE( _flags.hasFlag( "solved" ) );
    SW_EXPECT_EQUAL( 1, countEvents( SurvivalHorrorEvent::Kind::DeductionSolved ) );
    SW_EXPECT_TRUE( _session.submitDeduction( "culprit", "maid" ) == SurvivalHorrorPuzzleResult::AlreadySolved );
}

/**
 * @brief [SurvivalHorrorRulesTest] 규칙으로 끄면 틀린 뒤에도 보드를 남긴다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, WrongDeductionCanKeepLinks )
{
    SW_REQUIRE_TRUE( reopen( []( SurvivalHorrorRules& rules )
    { rules._bClearLinksOnWrong = false; } ) );
    SW_REQUIRE_TRUE( _session.readDocument( "diary" ) );
    SW_REQUIRE_TRUE( _session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_TRUE( _session.submitDeduction( "culprit", "maid" ) == SurvivalHorrorPuzzleResult::Wrong );
    SW_EXPECT_EQUAL( size_t{ 1 }, _session.getClueLinks().size() );
}

// ── 세션 왕복 ─────────────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorRulesTest] 왕복: 진행을 모두 잇고 같은 바이트가 나온다. 이벤트는 비운다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SessionStateRoundTrip )
{
    (void)_session.witnessMonster( "zombie" );
    SW_REQUIRE_TRUE( _session.readDocument( "diary" ) );
    SW_REQUIRE_TRUE( _session.linkClues( "knife", "gloves" ) );
    SW_REQUIRE_TRUE( _session.enterDialCode( "safe", vector<int32>{ 9 } ) == SurvivalHorrorPuzzleResult::Wrong );
    SW_REQUIRE_TRUE( _session.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Progress );
    (void)give( "keyRed" );
    SW_REQUIRE_TRUE( _session.useKey( "redDoor" ) == SurvivalHorrorPuzzleResult::Solved );
    SW_REQUIRE_TRUE( _session.tryMoveTo( "cellar" ) );
    SW_REQUIRE_TRUE( _session.trySetFlashlight( true ) );
    _session.applyDamage( 10.0f );
    (void)_session.trySave();
    _session.update( 1.0f, false );

    SurvivalHorrorSession restored;
    SW_REQUIRE_TRUE( open( restored ) );
    SW_REQUIRE_TRUE( StateBytesTestUtil::roundTrips( _session, restored ) );
    SW_EXPECT_NEAR( 90.0f, restored.getHealth(), kHorrorTolerance );
    SW_EXPECT_TRUE( restored.getCurrentArea() == hashed_string( "cellar" ) );
    SW_EXPECT_TRUE( restored.isFlashlightOn() );
    SW_EXPECT_TRUE( restored.isLinked( "knife", "gloves" ) );
    SW_EXPECT_TRUE( restored.useKey( "redDoor" ) == SurvivalHorrorPuzzleResult::AlreadySolved );
    SW_EXPECT_TRUE( restored.pressSequenceStep( "bells", "high" ) == SurvivalHorrorPuzzleResult::Solved ); // 진행을 이어 간다
    SW_EXPECT_TRUE( restored.enterDialCode( "safe", vector<int32>{ 9 } ) == SurvivalHorrorPuzzleResult::LockedOut );
    vector<SurvivalHorrorEvent> listEvent;
    SurvivalHorrorSession       fresh;
    SW_REQUIRE_TRUE( open( fresh ) );
    SW_REQUIRE_TRUE( StateBytesTestUtil::roundTrips( _session, fresh ) );
    fresh.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 푼 순서 퍼즐의 진행이 걸음 수로 남아 `readState` 의 범위를 넘었다. 순서 퍼즐을 푼 뒤의 세이브를 다시 읽지 못했다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SolvedSequenceSaveCanBeRead )
{
    SW_REQUIRE_TRUE( _session.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Progress );
    SW_REQUIRE_TRUE( _session.pressSequenceStep( "bells", "high" ) == SurvivalHorrorPuzzleResult::Solved );
    SurvivalHorrorSession restored;
    SW_REQUIRE_TRUE( open( restored ) );
    SW_EXPECT_TRUE( StateBytesTestUtil::roundTrips( _session, restored ) );
    SW_EXPECT_TRUE( restored.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::AlreadySolved );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 틀려서 처음으로 돌아간 순서 퍼즐에 값이 0 인 칸이 남아, 진행이 같은 세션이 다른 바이트를 썼다("같은 상태면 같은 바이트")
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, ResetSequenceWritesSameBytesAsUntouched )
{
    SurvivalHorrorSession other;
    SW_REQUIRE_TRUE( open( other ) );
    SW_EXPECT_TRUE( other.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Progress );
    SW_EXPECT_TRUE( other.pressSequenceStep( "bells", "low" ) == SurvivalHorrorPuzzleResult::Wrong );
    _session.loseSanity( 5.0f, "bells" ); // 틀린 벌칙만큼 맞춘다
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( other, _session ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 체력, 없는 괴물 · 문서 · 퍼즐 id, 다이얼 시도의 범위, 가지지 않은 단서의 연결, 없는 방을 받았다 — 모두 거절하고 상태는 그대로이다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SessionReadRefusesCraftedBytes )
{
    SurvivalFixtureSession good;
    good._listMonster  = { "zombie" };
    good._listDocument = { "diary" };
    good._listClue     = { "gloves", "knife" };
    good._listSolved   = { "culprit", "free", "redDoor", "bells" };
    good._listDial     = {
        { "safe", 2 }
    };
    good._listSequence = {
        { "bells", 1 }
    };
    good._listLink.push_back( SurvivalHorrorClueLink{ "knife", "gloves" } );
    good._health = 50.0f;
    SW_REQUIRE_TRUE( good.read( _session ) ); // 형식은 맞다
    SW_EXPECT_NEAR( 50.0f, _session.getHealth(), kHorrorTolerance );

    SurvivalFixtureSession fixture = good;
    fixture._health                = 101.0f;
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture._health = -1.0f;
    SW_EXPECT_FALSE( fixture.read( _session ) );

    fixture            = good;
    fixture._saveCount = -1;
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture             = good;
    fixture._wrongCount = -1;
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture             = good;
    fixture._flashlight = 2;
    SW_EXPECT_FALSE( fixture.read( _session ) );

    fixture              = good;
    fixture._listMonster = { "nothing" };
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture               = good;
    fixture._listDocument = { "nothing" };
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture             = good;
    fixture._listSolved = { "nothing" };
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture              = good;
    fixture._listMonster = { "zombie", "zombie" }; // 집합에 겹침이 없다
    SW_EXPECT_FALSE( fixture.read( _session ) );

    fixture           = good;
    fixture._listDial = {
        { "nothing", 1 }
    };
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture._listDial = {
        { "safe", -1 }
    };
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture._listDial = {
        { "safe", 3 }
    }; // 상한 2 를 넘는다
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture._listDial = {
        { "free", 99 }
    }; // 상한이 없는 자물쇠는 받는다
    SW_EXPECT_TRUE( fixture.read( _session ) );

    fixture               = good;
    fixture._listSequence = {
        { "nothing", 1 }
    };
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture._listSequence = {
        { "bells", 2 }
    }; // 걸음 수를 넘는 index 이다
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture._listSequence = {
        { "bells", -1 }
    };
    SW_EXPECT_FALSE( fixture.read( _session ) );

    fixture = good;
    fixture._listLink.push_back( SurvivalHorrorClueLink{ "gloves", "knife" } ); // 겹친다
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture = good;
    fixture._listLink.push_back( SurvivalHorrorClueLink{ "knife", "knife" } );
    SW_EXPECT_FALSE( fixture.read( _session ) );
    fixture = good;
    fixture._listLink.push_back( SurvivalHorrorClueLink{ "knife", "pantry" } ); // 가지지 않았다
    SW_EXPECT_FALSE( fixture.read( _session ) );

    fixture       = good;
    fixture._area = "nowhere";
    SW_EXPECT_FALSE( fixture.read( _session ) );

    SW_REQUIRE_TRUE( good.read( _session ) );
    SW_EXPECT_NEAR( 50.0f, _session.getHealth(), kHorrorTolerance );
}

/**
 * @brief [SurvivalHorrorRulesTest] 결함: 횟수 제한 세이브의 수가 상한을 넘어도 받았다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SessionReadRefusesSavesOverLimit )
{
    SW_REQUIRE_TRUE( reopen( []( SurvivalHorrorRules& rules )
    {
        rules._saveMode = SurvivalHorrorSaveMode::Limited;
        rules._maxSaves = 2;
    } ) );
    SurvivalFixtureSession fixture;
    fixture._saveCount = 2;
    SW_EXPECT_TRUE( fixture.read( _session ) );
    fixture._saveCount = 3;
    SW_EXPECT_FALSE( fixture.read( _session ) );
}

/**
 * @brief [SurvivalHorrorRulesTest] 시작하지 않은 세션은 읽지 않는다
 */
SW_TEST_CASE_F( SurvivalHorrorRulesTest, SessionReadRefusesUninitialized )
{
    Archive written;
    _session.writeState( written );
    Archive               reading( written.getData(), written.getSize() );
    SurvivalHorrorSession session;
    SW_EXPECT_FALSE( session.readState( reading ) );
}

// ── 초자연 전투 ───────────────────────────────────────────────────────

/**
 * @brief [SurvivalHorrorTest] 성공 눈이 범위 밖이면 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterRefusesBadSuccessFace )
{
    SW_EXPECT_LOG( "SurvivalHorrorEncounter.SurvivalHorrorEncounterRefusedSuccessFace" );
    SurvivalHorrorEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( _monster, {}, 1u, 7 ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 시드 0 은 난수의 단언에 걸렸다 — 오류로 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterRefusesSeedZero )
{
    SW_EXPECT_LOG( "SurvivalHorrorEncounter.RefusedSeedZero" );
    SurvivalHorrorEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( _monster, { makeInvestigator( 1 ) }, 0u ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 전투 난수와 차례 난수의 씨앗이 `시드 ^ 상수` 라 시드가 그 상수면 차례 쪽이 단언에 걸렸다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterSurvivesSeedEqualToMixConstant )
{
    SurvivalHorrorEncounter encounter;
    SW_EXPECT_TRUE( encounter.initialize( _monster, { makeInvestigator( 1 ) }, 0x5bd1e995u ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 겹친 조사자 번호는 차례 하나를 나눠 쓰고 한쪽이 빠지면 다른 쪽의 차례도 사라졌다 — 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterRefusesRepeatedActorID )
{
    SW_EXPECT_LOG( "SurvivalHorrorEncounter.RefusedActorID" );
    SurvivalHorrorEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( _monster, { makeInvestigator( 1 ), makeInvestigator( 1 ) }, 1u ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 괴물 번호와 같은 조사자는 괴물과 차례를 나눠 썼다 — 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterRefusesMonsterActorID )
{
    SW_EXPECT_LOG( "SurvivalHorrorEncounter.RefusedActorID" );
    SurvivalHorrorEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( _monster, { makeInvestigator( SurvivalHorrorEncounter::kMonsterActorID ) }, 1u ) );
}

/**
 * @brief [SurvivalHorrorTest] 음수 번호도 거절한다(-1 은 "차례 없음" 의 표시이다)
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterRefusesNegativeActorID )
{
    SW_EXPECT_LOG( "SurvivalHorrorEncounter.RefusedActorID" );
    SurvivalHorrorEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( _monster, { makeInvestigator( -1 ) }, 1u ) );
}

/**
 * @brief [SurvivalHorrorTest] 모든 눈이 성공이면 힘만큼 강인함을 깎아 이긴다. 빠른 쪽이 먼저 움직이고, 괴물은 그 사이에 때린다. 끝나면 빈 결과이다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterVictory )
{
    SurvivalHorrorEncounter encounter;
    SW_REQUIRE_TRUE( encounter.initialize( _monster, { makeInvestigator( 1 ) }, 7u, 1 ) );
    SW_EXPECT_TRUE( encounter.getState() == SurvivalHorrorEncounterState::Ongoing );

    const SurvivalHorrorTurnResult first = encounter.playNextTurn();
    SW_EXPECT_EQUAL( 1, first._actorID );
    SW_EXPECT_EQUAL( 2, first._horrorSuccesses );
    SW_EXPECT_EQUAL( 2, first._combatSuccesses );
    SW_EXPECT_EQUAL( 1, encounter.getMonsterToughness() );

    const SurvivalHorrorTurnResult second = encounter.playNextTurn();
    SW_EXPECT_EQUAL( SurvivalHorrorEncounter::kMonsterActorID, second._actorID );
    SW_EXPECT_EQUAL( 1, second._targetID );
    SW_EXPECT_EQUAL( 1, second._healthLoss );
    SW_EXPECT_EQUAL( 4, encounter.getInvestigators().front()._health );

    (void)encounter.playNextTurn();
    SW_EXPECT_TRUE( encounter.getState() == SurvivalHorrorEncounterState::Victory );
    SW_EXPECT_EQUAL( 0, encounter.getMonsterToughness() );
    SW_EXPECT_EQUAL( 3, encounter.getTurnCount() );

    SW_EXPECT_EQUAL( -1, encounter.playNextTurn()._actorID );
    SW_EXPECT_EQUAL( 3, encounter.getTurnCount() );
}

/**
 * @brief [SurvivalHorrorTest] 괴물은 체력이 가장 낮은 쪽을, 같으면 번호가 작은 쪽을 때린다. 모두 빠지면 진다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterMonsterTargetsWeakestUntilDefeat )
{
    _monster._damage                   = 2;
    SurvivalHorrorInvestigator slowerA = makeInvestigator( 5, 2, 0 );
    SurvivalHorrorInvestigator slowerB = makeInvestigator( 3, 2, 0 );
    slowerA._speed                     = 0.5f; // 괴물이 먼저 움직인다
    slowerB._speed                     = 0.5f;
    SurvivalHorrorEncounter encounter;
    SW_REQUIRE_TRUE( encounter.initialize( _monster, { slowerA, slowerB }, 7u, 6 ) );

    const SurvivalHorrorTurnResult first = encounter.playNextTurn();
    SW_EXPECT_EQUAL( SurvivalHorrorEncounter::kMonsterActorID, first._actorID );
    SW_EXPECT_EQUAL( 3, first._targetID ); // 체력이 같으면 번호가 작은 쪽이다
    SW_EXPECT_EQUAL( 2, first._healthLoss );

    int32 turnGuard = 0;
    while ( encounter.getState() == SurvivalHorrorEncounterState::Ongoing && turnGuard < 32 )
    {
        (void)encounter.playNextTurn();
        ++turnGuard;
    }
    SW_EXPECT_TRUE( encounter.getState() == SurvivalHorrorEncounterState::Defeat );
    for ( const SurvivalHorrorInvestigator& investigator : encounter.getInvestigators() )
    {
        SW_EXPECT_EQUAL( SW_TRUE, investigator._bDefeated );
        SW_EXPECT_EQUAL( 0, investigator._health );
    }
}

/**
 * @brief [SurvivalHorrorTest] 공포 판정에 지면 정신력을 잃고, 0 이 되면 그 차례의 전투 판정 없이 빠진다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterHorrorBreaksInvestigator )
{
    _monster._horror                        = 1;
    SurvivalHorrorInvestigator investigator = makeInvestigator( 1 );
    investigator._will                      = 0; // 주사위가 없어 공포 판정은 늘 진다
    investigator._sanity                    = 1;
    SurvivalHorrorEncounter encounter;
    SW_REQUIRE_TRUE( encounter.initialize( _monster, { investigator }, 7u, 1 ) );

    const SurvivalHorrorTurnResult result = encounter.playNextTurn();
    SW_EXPECT_EQUAL( 0, result._horrorSuccesses );
    SW_EXPECT_EQUAL( 1, result._sanityLoss );
    SW_EXPECT_EQUAL( 0, result._combatSuccesses );
    SW_EXPECT_EQUAL( 3, encounter.getMonsterToughness() );
    SW_EXPECT_TRUE( encounter.getState() == SurvivalHorrorEncounterState::Defeat );
}

/**
 * @brief [SurvivalHorrorTest] 처음부터 쓰러진 조사자는 차례가 없고, 모두 쓰러졌으면 바로 진다. 조사자가 없어도 진다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterStartsWithBrokenInvestigators )
{
    SurvivalHorrorEncounter encounter;
    SW_REQUIRE_TRUE( encounter.initialize( _monster, { makeInvestigator( 1, 0 ) }, 7u ) );
    SW_EXPECT_TRUE( encounter.getState() == SurvivalHorrorEncounterState::Defeat );
    SW_EXPECT_EQUAL( SW_TRUE, encounter.getInvestigators().front()._bDefeated );
    SW_EXPECT_EQUAL( -1, encounter.playNextTurn()._actorID );

    SW_REQUIRE_TRUE( encounter.initialize( _monster, {}, 7u ) );
    SW_EXPECT_TRUE( encounter.getState() == SurvivalHorrorEncounterState::Defeat );
}

/**
 * @brief [SurvivalHorrorTest] 같은 시드면 같은 싸움이고, 왕복한 싸움도 같게 이어 간다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterStateRoundTrip )
{
    _monster._toughness = 50;
    const vector<SurvivalHorrorInvestigator> listInvestigator{ makeInvestigator( 1 ), makeInvestigator( 2 ) };
    SurvivalHorrorEncounter                  encounter;
    SW_REQUIRE_TRUE( encounter.initialize( _monster, listInvestigator, 11u ) );
    SurvivalHorrorEncounter twin;
    SW_REQUIRE_TRUE( twin.initialize( _monster, listInvestigator, 11u ) );
    for ( int32 turn = 0; turn < 3; ++turn )
    {
        (void)encounter.playNextTurn();
        (void)twin.playNextTurn();
    }
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( encounter, twin ) );

    SurvivalHorrorEncounter restored;
    SW_REQUIRE_TRUE( restored.initialize( _monster, {}, 99u ) );
    SW_REQUIRE_TRUE( StateBytesTestUtil::roundTrips( encounter, restored ) );
    for ( int32 turn = 0; turn < 5; ++turn )
    {
        const SurvivalHorrorTurnResult expected = encounter.playNextTurn();
        const SurvivalHorrorTurnResult actual   = restored.playNextTurn();
        SW_EXPECT_EQUAL( expected._actorID, actual._actorID );
        SW_EXPECT_EQUAL( expected._combatSuccesses, actual._combatSuccesses );
    }
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( encounter, restored ) );

    // 끝난 싸움도 왕복한다.
    SurvivalHorrorEncounter finished;
    SW_REQUIRE_TRUE( finished.initialize( _monster, { makeInvestigator( 1, 0 ) }, 11u ) );
    SurvivalHorrorEncounter restoredFinished;
    SW_REQUIRE_TRUE( restoredFinished.initialize( _monster, {}, 99u ) );
    SW_EXPECT_TRUE( StateBytesTestUtil::roundTrips( finished, restoredFinished ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 음수 체력 · 주사위, 범위 밖 강인함, 체력과 맞지 않는 빠짐, 명단과 맞지 않는 차례, 강인함 · 남은 수와 맞지 않는 결과를 받았다. 명단에 없는 차례는 그 차례를 헛돌고, 남은 조사자가 차례에 없으면 영영 움직이지 못한다 — 거절한다
 */
SW_TEST_CASE_F( SurvivalHorrorTest, EncounterReadRefusesCraftedBytes )
{
    SurvivalHorrorEncounter encounter;
    SW_REQUIRE_TRUE( encounter.initialize( _monster, { makeInvestigator( 1 ) }, 7u ) );
    const SurvivalHorrorInvestigator good  = makeInvestigator( 1 );
    const TurnOrder                  order = makeOrder( 1 );
    SW_REQUIRE_TRUE( readFixture( encounter, "ghoul", good, order, 3, SurvivalHorrorEncounterState::Ongoing ) ); // 형식은 맞다

    SW_EXPECT_FALSE( readFixture( encounter, "other", good, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, order, 4, SurvivalHorrorEncounterState::Ongoing ) );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, order, -1, SurvivalHorrorEncounterState::Ongoing ) );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, order, 3, SurvivalHorrorEncounterState::Victory ) );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, order, 0, SurvivalHorrorEncounterState::Ongoing ) );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, order, 3, SurvivalHorrorEncounterState::Defeat ) );
    SW_EXPECT_TRUE( readFixture( encounter, "ghoul", good, order, 0, SurvivalHorrorEncounterState::Victory ) );

    SurvivalHorrorInvestigator fixture = good;
    fixture._health                    = -1;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture         = good;
    fixture._sanity = -1;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture       = good;
    fixture._will = -1;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture           = good;
    fixture._strength = -1;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture        = good;
    fixture._speed = 0.0f;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture            = good;
    fixture._bDefeated = 2;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture            = good;
    fixture._bDefeated = SW_TRUE; // 체력이 남았는데 빠졌다
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Defeat ) );
    fixture         = good;
    fixture._health = 0; // 체력이 없는데 남았다
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, order, 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture          = good;
    fixture._actorID = SurvivalHorrorEncounter::kMonsterActorID;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, makeOrder( fixture._actorID ), 3, SurvivalHorrorEncounterState::Ongoing ) );
    fixture          = good;
    fixture._actorID = -1;
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", fixture, makeOrder( -1 ), 3, SurvivalHorrorEncounterState::Ongoing ) );

    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, makeOrder( 2 ), 3, SurvivalHorrorEncounterState::Ongoing ) ); // 차례의 사람이 다르다
    TurnOrder noMonster;
    noMonster.initialize( TurnOrderMode::Rounds, 7u );
    noMonster.addActor( 1, 2.0f );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, noMonster, 3, SurvivalHorrorEncounterState::Ongoing ) );
    TurnOrder extra = order;
    extra.addActor( 2, 1.0f );
    SW_EXPECT_FALSE( readFixture( encounter, "ghoul", good, extra, 3, SurvivalHorrorEncounterState::Ongoing ) );
}

/**
 * @brief [SurvivalHorrorTest] 결함: 기반 `TurnOrder::readState` 가 겹친 번호, 대기열의 없는 번호, 0 이하 속도(타임라인이 나눈다), 음수 라운드를 받았다 — 거절한다
 */
SW_TEST_CASE( SurvivalHorrorTest, TurnOrderReadRefusesCraftedBytes )
{
    struct FixtureActor
    {
        float32 _speed{ 1.0f };
        float32 _gauge{ 0.0f };
        int32   _actorID{ 1 };
    };
    const auto read = []( const vector<FixtureActor>& listActor, const vector<int32>& listQueue, int32 round )
    {
        Archive written;
        written << static_cast<uint32>( listActor.size() );
        for ( const FixtureActor& actor : listActor )
        {
            written << actor._speed;
            written << actor._gauge;
            written << actor._actorID;
            written << static_cast<int32>( 0 );
            written << static_cast<uint32>( 0 );
        }
        written << static_cast<uint32>( listQueue.size() );
        for ( const int32 actorID : listQueue )
        {
            written << actorID;
        }
        const GameRandom random;
        StateIO::writeRandom( written, random );
        written << round;
        written << static_cast<uint8>( TurnOrderMode::Rounds );
        Archive   reading( written.getData(), written.getSize() );
        TurnOrder order;
        return order.readState( reading );
    };
    const FixtureActor actor;
    SW_EXPECT_TRUE( read( { actor }, { 1 }, 1 ) ); // 형식은 맞다
    SW_EXPECT_FALSE( read( { actor, actor }, {}, 1 ) );
    SW_EXPECT_FALSE( read( { actor }, { 2 }, 1 ) );
    SW_EXPECT_FALSE( read( { actor }, {}, -1 ) );
    FixtureActor fixture = actor;
    fixture._speed       = 0.0f;
    SW_EXPECT_FALSE( read( { fixture }, {}, 1 ) );
    fixture        = actor;
    fixture._gauge = -1.0f;
    SW_EXPECT_FALSE( read( { fixture }, {}, 1 ) );

    TurnOrder order;
    order.initialize( TurnOrderMode::Rounds, 7u );
    order.addActor( 4, 1.0f );
    SW_EXPECT_TRUE( order.hasActor( 4 ) );
    SW_EXPECT_FALSE( order.hasActor( 5 ) );
    order.removeActor( 4 );
    SW_EXPECT_FALSE( order.hasActor( 4 ) );
}
