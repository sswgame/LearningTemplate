#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostEncounter.h"
#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostMansion.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

// 유령 사냥 키트의 규칙: 카탈로그, 손전등 원뿔과 스트로브, 청소기 줄다리기와 서지, 저택(불 켜기, 열쇠 문, 가구, 부), 상태 왕복과 꾸민 바이트 거절. 키트 GF_GhostHunt(nogpu).
// 꾸민 바이트는 형식을 맞춰 손으로 쓴 뒤(정상 값이 읽히는 것을 함께 본다) 범위 밖 값만 바꿔 넣는다.

using namespace sw;

namespace
{
    constexpr const utf8* kGhostHuntXML = R"(<GhostHunt currency="Souls">
  <Flashlight range="8" angle="0.5" strobeRange="10" strobeAngle="0.7" strobeCharge="1"/>
  <Vacuum range="4" stages="10 20" alignThreshold="0.7" alignBonus="2" surgeFill="1" surgeDamage="50" dragSpeed="2" dragReduction="0.5"/>
  <Ghost id="goob" hp="40" hideTime="1" appearTime="1" attackTime="1" attackDamage="8" stunTime="3" pull="1" fleeInterval="1" coins="10"/>
  <Ghost id="shy" hp="40" hideTime="1" appearTime="1" attackTime="1" stunTime="3" strobeOnly="true" coins="0"/>
  <Room id="foyer" ghosts="goob" key="parlorKey" lightFlag="lit.foyer"/>
  <Room id="parlor" ghosts="goob,goob" lightFlag="lit.parlor"/>
  <Room id="hall" lightFlag="lit.hall"/>
  <Door id="parlorDoor" key="parlorKey" flag="doorParlor"/>
  <Door id="openDoor" flag="doorOpen"/>
  <Furniture id="dresser" room="foyer" loot="dresserLoot" search="Shake"/>
  <Furniture id="curtain" room="foyer" search="Vacuum"/>
  <Furniture id="desk" room="parlor"/>
  <Boo id="booA" room="foyer" furniture="dresser" hp="30" escapeTime="5"/>
  <Boo id="booB" room="hall" hp="10" escapeTime="5"/>
</GhostHunt>)";

    constexpr const utf8* kGhostHuntLootXML = R"(<LootCatalog><Table id="dresserLoot"><Always item="gold" chance="1"/></Table></LootCatalog>)";

    constexpr const utf8* kGhostHuntGraphXML = R"(<AreaGraph><Area id="foyer"/><Area id="parlor"/><Area id="hall"/>
  <Link from="foyer" to="parlor" requires="doorParlor"/><Link from="foyer" to="hall"/></AreaGraph>)";

    constexpr float32 kGhostTolerance = 1.0e-3f;

    /** @brief 싸움의 상태 바이트를 손으로 씁니다. 기본값은 숨은 유령 하나인 정상 상태입니다. */
    struct GhostForgedEncounter
    {
        hashed_string _kind{ "goob" };
        float3        _flee{};
        float32       _hp{ 40.0f };
        float32       _strobe{ 0.0f };
        float32       _surge{ 0.0f };
        uint32        _id{ 1 };
        uint32        _suctionTarget{ 0 };
        uint32        _nextID{ 2 };
        int32         _stage{ 0 };
        uint8         _state{ static_cast<uint8>( GhostState::Hidden ) };
        uint8         _bTwice{ SW_FALSE }; ///< 같은 번호의 유령을 둘 쓴다

        void write( Archive& outArchive ) const
        {
            const GameRandom random;
            StateIO::writeRandom( outArchive, random );
            const uint32 count = _bTwice == SW_TRUE ? 2u : 1u;
            outArchive << count;
            for ( uint32 index = 0; index < count; ++index )
            {
                StateIO::writeName( outArchive, _kind );
                outArchive << float3{};
                outArchive << _flee;
                outArchive << _hp;
                StateIO::writeCountdown( outArchive, Countdown( 1.0f ) );
                outArchive << _id;
                outArchive << _state;
            }
            outArchive << _strobe;
            outArchive << _surge;
            outArchive << _suctionTarget;
            outArchive << _nextID;
            outArchive << _stage;
        }

        [[nodiscard]] bool read( GhostEncounter& inoutEncounter ) const
        {
            Archive written;
            write( written );
            Archive reading( written.getData(), written.getSize() );
            return inoutEncounter.readState( reading );
        }
    };

    /** @brief 부 하나의 상태 바이트입니다. 기본값은 가구에 숨은 `booA` 입니다. */
    struct GhostForgedBoo
    {
        hashed_string _room{ "foyer" };
        hashed_string _furniture{ "dresser" };
        float32       _hp{ 30.0f };
        uint8         _state{ static_cast<uint8>( HidingGhostState::Hiding ) };

        void write( Archive& outArchive ) const
        {
            StateIO::writeName( outArchive, _room );
            StateIO::writeName( outArchive, _furniture );
            outArchive << _hp;
            StateIO::writeCountdown( outArchive, Countdown{} );
            outArchive << _state;
        }
    };

    /** @brief 싸움 하나입니다. */
    struct GhostHuntTest
    {
        GhostCatalog       _catalog{};
        GhostEncounter     _encounter{};
        vector<GhostEvent> _listEvent{};

        GhostHuntTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kGhostHuntXML, "GhostHuntTest" ) );
            SW_REQUIRE_TRUE( _encounter.initialize( &_catalog, 3u ) );
        }

        /** @brief 쌓인 이벤트를 `_listEvent` 뒤로 모으고 @p type 의 수를 셉니다. */
        int32 countEvents( GhostEventKind type )
        {
            _encounter.pollEvents( _listEvent );
            return VectorUtil::countBy<&GhostEvent::_type>( _listEvent, type );
        }

        /** @brief 유령을 앞 2 m 에 내어 공격 동작까지 시간을 흘립니다. */
        uint32 spawnAttacking( const hashed_string& kind )
        {
            const uint32 ghostID = _encounter.spawnGhost( kind, float3{ 0.0f, 0.0f, 2.0f } );
            _encounter.update( 1.0f );
            _encounter.update( 1.0f );
            return ghostID;
        }

        /** @brief 유령을 기절시켜 흡입을 시작합니다. 도망 방향은 플레이어(원점)에서 멀어지는 +Z 입니다. */
        uint32 startSucking()
        {
            const uint32 ghostID = spawnAttacking( "goob" );
            SW_EXPECT_EQUAL( 1, _encounter.shineBeam( float3{}, float3{ 0.0f, 0.0f, 1.0f } ) );
            SW_EXPECT_TRUE( _encounter.startSuction( ghostID, float3{} ) );
            return ghostID;
        }
    };

    /** @brief 저택 한 채입니다. 플래그, 가방, 지갑, 그래프, 전리품은 빌려 줍니다. */
    struct GhostHuntRulesTest
    {
        GhostCatalog              _catalog{};
        LootCatalog               _loot{};
        AreaGraph                 _graph{};
        GameFlags                 _flags{};
        Inventory                 _bag{};
        Wallet                    _wallet{};
        GhostMansion              _mansion{};
        vector<GhostMansionEvent> _listEvent{};

        GhostHuntRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kGhostHuntXML, "GhostHuntRulesTest" ) );
            SW_REQUIRE_TRUE( _loot.loadFromXMLText( kGhostHuntLootXML, "GhostHuntRulesTest" ) );
            SW_REQUIRE_TRUE( _graph.loadFromXMLText( kGhostHuntGraphXML, "GhostHuntRulesTest" ) );
            SW_REQUIRE_TRUE( _bag.initialize( nullptr, 4 ) );
            SW_REQUIRE_TRUE( open( _mansion, 5u ) );
        }

        bool open( GhostMansion& outMansion, uint32 seed )
        {
            SessionStateRefs refs;
            refs._pFlags     = &_flags;
            refs._pInventory = &_bag;
            refs._pWallet    = &_wallet;
            return outMansion.initialize( &_catalog, &_loot, &_graph, refs, seed );
        }

        /** @brief 쌓인 이벤트를 `_listEvent` 뒤로 모으고 @p type 의 수를 셉니다. */
        int32 countEvents( GhostMansionEventKind type )
        {
            _mansion.pollEvents( _listEvent );
            return VectorUtil::countBy<&GhostMansionEvent::_type>( _listEvent, type );
        }

        /** @brief 지금 방의 유령을 모두 잡습니다. 잡은 이벤트는 다음 `update` 나 방 옮기기가 처리합니다. */
        void catchRoomGhosts()
        {
            _mansion.update( 1.0f );
            _mansion.update( 1.0f );
            GhostEncounter& encounter = _mansion.getEncounter();
            (void)encounter.shineBeam( float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 0.0f, 1.0f } );
            const vector<GhostInstance> listGhost = encounter.getGhosts();
            for ( const GhostInstance& ghost : listGhost )
            {
                SW_EXPECT_TRUE( encounter.startSuction( ghost._id, ghost._position ) );
                const GhostSuctionTick tick = encounter.updateSuction( float3{}, 5.0f );
                SW_EXPECT_EQUAL( SW_TRUE, tick._bCaught );
            }
        }

        /** @brief 저택의 상태 바이트를 손으로 쓴 뒤 읽습니다. 싸움은 지금 것을 그대로 싣습니다. */
        bool readForged( const GhostForgedBoo& boo, const hashed_string& currentRoom, uint32 booCount = 2u, uint32 searchedCount = 3u, uint8 searched = SW_FALSE )
        {
            Archive written;
            _mansion.getEncounter().writeState( written );
            const GameRandom random;
            StateIO::writeRandom( written, random );
            written << booCount;
            boo.write( written );
            for ( uint32 index = 1; index < booCount; ++index )
            {
                GhostForgedBoo other;
                other._room      = "hall";
                other._furniture = hashed_string{};
                other._hp        = 10.0f;
                other.write( written );
            }
            written << searchedCount;
            for ( uint32 index = 0; index < searchedCount; ++index )
            {
                written << searched;
            }
            StateIO::writeName( written, currentRoom );
            written << static_cast<uint32>( 5 ); // 씨앗
            written << static_cast<uint32>( 0 ); // 맡아 둔 열쇠
            Archive reading( written.getData(), written.getSize() );
            return _mansion.readState( reading );
        }
    };
} // namespace

// ── 카탈로그 ──────────────────────────────────────────────────────────

/**
 * @brief [GhostHuntTest] 원소마다 읽는다. 뒤지는 방법은 적으면 그것만, 적지 않으면 둘 다이다
 */
SW_TEST_CASE_F( GhostHuntTest, CatalogReadsEveryElement )
{
    SW_EXPECT_TRUE( _catalog.getCurrency() == hashed_string( "Souls" ) );
    SW_EXPECT_NEAR( 8.0f, _catalog.getFlashlight()._range, kGhostTolerance );
    SW_EXPECT_NEAR( 1.0f, _catalog.getFlashlight()._strobeChargeTime, kGhostTolerance );
    SW_REQUIRE_EQUAL( size_t{ 2 }, _catalog.getVacuum()._listStagePower.size() );
    SW_EXPECT_NEAR( 20.0f, _catalog.getVacuum()._listStagePower[1], kGhostTolerance );

    SW_REQUIRE_TRUE( _catalog.findGhost( "shy" ) != nullptr );
    SW_EXPECT_TRUE( _catalog.findGhost( "shy" )->_bStrobeOnly );
    SW_EXPECT_TRUE( _catalog.findGhost( "nobody" ) == nullptr );
    SW_REQUIRE_TRUE( _catalog.findRoom( "parlor" ) != nullptr );
    SW_EXPECT_EQUAL( size_t{ 2 }, _catalog.findRoom( "parlor" )->_listGhost.size() );
    SW_REQUIRE_TRUE( _catalog.findDoor( "parlorDoor" ) != nullptr );
    SW_EXPECT_TRUE( _catalog.findDoor( "parlorDoor" )->_key == hashed_string( "parlorKey" ) );

    const GhostFurnitureDef* pDresser = _catalog.findFurniture( "dresser" );
    const GhostFurnitureDef* pDesk    = _catalog.findFurniture( "desk" );
    SW_REQUIRE_TRUE( pDresser != nullptr && pDesk != nullptr );
    SW_EXPECT_FALSE( pDresser->_bVacuum );
    SW_EXPECT_TRUE( pDresser->_bShake );
    SW_EXPECT_TRUE( pDesk->_bVacuum && pDesk->_bShake );
    SW_EXPECT_EQUAL( 1, _catalog.findBooIndex( "booB" ).value() );
    SW_EXPECT_FALSE( _catalog.findBooIndex( "nobody" ).has_value() );
}

/**
 * @brief [GhostHuntTest] 빈 표는 읽은 것이 없어 실패이다
 */
SW_TEST_CASE( GhostHuntTest, CatalogEmptyRootFails )
{
    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( "<GhostHunt/>", "GhostHuntTest" ) );
}

/**
 * @brief [GhostHuntTest] 결함: 손으로 읽는 방은 `add` 로 앞 정의를 말없이 덮어썼다(주석은 "뒤의 것을 버린다"). 겹친 id 를 거절한다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesDuplicateRoom )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    GhostCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<GhostHunt><Room id="a" lightFlag="first"/><Room id="a" lightFlag="second"/></GhostHunt>)", "GhostHuntTest" ) );
    SW_REQUIRE_TRUE( catalog.findRoom( "a" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findRoom( "a" )->_lightFlag == hashed_string( "first" ) );
}

/**
 * @brief [GhostHuntTest] 결함: 가구도 같다 — 겹친 id 를 거절한다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesDuplicateFurniture )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    GhostCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<GhostHunt><Furniture id="a" room="first"/><Furniture id="a" room="second"/></GhostHunt>)", "GhostHuntTest" ) );
    SW_REQUIRE_TRUE( catalog.findFurniture( "a" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findFurniture( "a" )->_room == hashed_string( "first" ) );
}

/**
 * @brief [GhostHuntTest] 필수 칸이 빠진 방은 넣지 않는다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesRoomWithoutLightFlag )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Room id="a"/></GhostHunt>)", "GhostHuntTest" ) );
    SW_EXPECT_TRUE( catalog.findRoom( "a" ) == nullptr );
}

/**
 * @brief [GhostHuntTest] 범위 밖 체력의 유령은 넣지 않는다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesOutOfRangeGhost )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Ghost id="a" hp="0"/></GhostHunt>)", "GhostHuntTest" ) );
    SW_EXPECT_TRUE( catalog.findGhost( "a" ) == nullptr );
}

/**
 * @brief [GhostHuntTest] 모르는 원소는 오류이다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesUnknownElement )
{
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( "<GhostHunt><Poltergeist/></GhostHunt>", "GhostHuntTest" ) );
}

/**
 * @brief [GhostHuntTest] 방이 가리키는 유령은 뒤에 적혀도 되고, 끝까지 없으면 로드 실패다(필수 참조 — `engine/settings/datacatalogreferences.xml`)
 */
SW_TEST_CASE( GhostHuntTest, CatalogReportsRoomWithUnknownGhost )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Room id="a" ghosts="later,nobody" lightFlag="f"/><Ghost id="later"/></GhostHunt>)", "GhostHuntTest" ) );
    GhostCatalog clean;
    SW_EXPECT_TRUE( clean.loadFromXMLText( R"(<GhostHunt><Room id="a" ghosts="later" lightFlag="f"/><Ghost id="later"/></GhostHunt>)", "GhostHuntTest" ) );
}

/**
 * @brief [GhostHuntTest] 결함: 없는 가구에 숨은 부를 말없이 받아 찾을 길이 없었다. 가구는 뒤에 적혀도 되고, 끝까지 없으면 로드 실패다
 */
SW_TEST_CASE( GhostHuntTest, CatalogReportsBooInUnknownFurniture )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Boo id="a" furniture="later"/><Boo id="b" furniture="nothing"/><Furniture id="later"/></GhostHunt>)", "GhostHuntTest" ) );
}

/**
 * @brief [GhostHuntTest] 모르는 뒤지기 이름은 오류이고 그 가구를 넣지 않는다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesUnknownSearchMode )
{
    SW_EXPECT_LOG( "GhostCatalog.FurnitureUnknownSearchMode" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Furniture id="a" search="Kick"/></GhostHunt>)", "GhostHuntTest" ) );
    SW_EXPECT_TRUE( catalog.findFurniture( "a" ) == nullptr );
}

/**
 * @brief [GhostHuntTest] 결함: 구분자만 적으면 어느 방법으로도 뒤질 수 없는 가구가 되었다 — 거절한다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesEmptySearchMode )
{
    SW_EXPECT_LOG( "GhostCatalog.FurnitureNeedsSearchMode" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Furniture id="a" search=","/></GhostHunt>)", "GhostHuntTest" ) );
}

/**
 * @brief [GhostHuntTest] 결함: 빈 단계 목록은 흡입 피해가 0 이라 유령을 잡을 수 없었다 — 거절한다
 */
SW_TEST_CASE( GhostHuntTest, CatalogRefusesVacuumWithoutStages )
{
    SW_EXPECT_LOG( "GhostCatalog.VacuumNeedsStages" );
    GhostCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<GhostHunt><Vacuum stages=""/></GhostHunt>)", "GhostHuntTest" ) );
    SW_EXPECT_FALSE( catalog.getVacuum()._listStagePower.empty() );
}

// ── 싸움 ──────────────────────────────────────────────────────────────

/**
 * @brief [GhostHuntTest] 시작 거절: 카탈로그 없음
 */
SW_TEST_CASE( GhostHuntTest, EncounterRefusesNoCatalog )
{
    SW_EXPECT_LOG( "GhostEncounter.RefusedInitialize" );
    GhostEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( nullptr, 1u ) );
    SW_EXPECT_EQUAL( 0u, encounter.spawnGhost( "goob", float3{} ) );
}

/**
 * @brief [GhostHuntTest] 결함: 시드 0 은 난수의 단언에 걸렸다 — 오류로 거절한다
 */
SW_TEST_CASE_F( GhostHuntTest, EncounterRefusesSeedZero )
{
    SW_EXPECT_LOG( "GhostEncounter.RefusedInitialize" );
    GhostEncounter encounter;
    SW_EXPECT_FALSE( encounter.initialize( &_catalog, 0u ) );
}

/**
 * @brief [GhostHuntTest] 번호는 1 부터이고 모르는 종류는 0 이다. 치우면 번호가 다시 시작한다
 */
SW_TEST_CASE_F( GhostHuntTest, SpawnAssignsIDs )
{
    SW_EXPECT_EQUAL( 1u, _encounter.spawnGhost( "goob", float3{} ) );
    SW_EXPECT_EQUAL( 2u, _encounter.spawnGhost( "shy", float3{} ) );
    SW_EXPECT_EQUAL( 0u, _encounter.spawnGhost( "nobody", float3{} ) );
    SW_EXPECT_EQUAL( 2, _encounter.countRemaining() );
    SW_EXPECT_TRUE( _encounter.findGhost( 3u ) == nullptr );
    SW_EXPECT_TRUE( _encounter.findGhost( 0u ) == nullptr );
    _encounter.setGhostPosition( 1u, float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, _encounter.findGhost( 1u )->_position._x, kGhostTolerance );
    _encounter.clear();
    SW_EXPECT_EQUAL( 0, _encounter.countRemaining() );
    SW_EXPECT_EQUAL( 1u, _encounter.spawnGhost( "goob", float3{} ) );
}

/**
 * @brief [GhostHuntTest] 숨음 → 나타남 → 공격(끝에 맞는다) → 숨음. 0 이하 시간은 흐르지 않는다
 */
SW_TEST_CASE_F( GhostHuntTest, GhostCycles )
{
    const uint32 ghostID = _encounter.spawnGhost( "goob", float3{} );
    _encounter.update( 0.0f );
    _encounter.update( -1.0f );
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Hidden );
    _encounter.update( 1.0f );
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Visible );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::Appeared ) );
    _encounter.update( 1.0f );
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Attacking );
    _encounter.update( 1.0f );
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Hidden );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::AttackLanded ) );
    SW_EXPECT_NEAR( 8.0f, _listEvent.back()._amount, kGhostTolerance );
}

/**
 * @brief [GhostHuntTest] 원뿔은 거리와 각을 본다. 스트로브가 더 멀고 넓다(높이는 보지 않는다)
 */
SW_TEST_CASE_F( GhostHuntTest, ConeChecksRangeAndAngle )
{
    const float3 eye{};
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    SW_EXPECT_TRUE( _encounter.isInCone( eye, forward, float3{ 0.0f, 0.0f, 5.0f }, false ) );
    SW_EXPECT_TRUE( _encounter.isInCone( eye, forward, float3{ 0.0f, 3.0f, 5.0f }, false ) );
    SW_EXPECT_FALSE( _encounter.isInCone( eye, forward, float3{ 0.0f, 0.0f, 9.0f }, false ) );
    SW_EXPECT_TRUE( _encounter.isInCone( eye, forward, float3{ 0.0f, 0.0f, 9.0f }, true ) );
    SW_EXPECT_FALSE( _encounter.isInCone( eye, forward, float3{ 3.0f, 0.0f, 5.0f }, false ) );
    SW_EXPECT_TRUE( _encounter.isInCone( eye, forward, float3{ 3.0f, 0.0f, 5.0f }, true ) );
    SW_EXPECT_FALSE( _encounter.isInCone( eye, forward, float3{ 0.0f, 0.0f, -5.0f }, true ) );
}

/**
 * @brief [GhostHuntTest] 보통 손전등은 공격 중인 유령만 기절시키고, 스트로브 전용 유령은 기절시키지 못한다
 */
SW_TEST_CASE_F( GhostHuntTest, BeamStunsOnlyAttackers )
{
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    const uint32 hiddenID = _encounter.spawnGhost( "goob", float3{ 0.0f, 0.0f, 2.0f } );
    SW_EXPECT_EQUAL( 0, _encounter.shineBeam( float3{}, forward ) );
    _encounter.update( 1.0f );
    SW_EXPECT_EQUAL( 0, _encounter.shineBeam( float3{}, forward ) ); // 나타났을 뿐이다
    _encounter.update( 1.0f );
    SW_EXPECT_EQUAL( 0, _encounter.shineBeam( float3{}, float3{ 0.0f, 0.0f, -1.0f } ) ); // 뒤를 비췄다
    SW_EXPECT_EQUAL( 1, _encounter.shineBeam( float3{}, forward ) );
    SW_EXPECT_TRUE( _encounter.findGhost( hiddenID )->_state == GhostState::Stunned );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::Stunned ) );

    _encounter.clear();
    (void)spawnAttacking( "shy" );
    SW_EXPECT_EQUAL( 0, _encounter.shineBeam( float3{}, forward ) );
}

/**
 * @brief [GhostHuntTest] 스트로브는 다 모아야 터지고, 덜 모아 터뜨리면 헛번쩍으로 비운다. 나타난 유령과 스트로브 전용 유령도 기절시킨다
 */
SW_TEST_CASE_F( GhostHuntTest, StrobeNeedsFullCharge )
{
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    const uint32 shyID = spawnAttacking( "shy" );
    _encounter.chargeStrobe( 0.5f );
    SW_EXPECT_NEAR( 0.5f, _encounter.getStrobeCharge(), kGhostTolerance );
    SW_EXPECT_EQUAL( 0, _encounter.releaseStrobe( float3{}, forward ) );
    SW_EXPECT_NEAR( 0.0f, _encounter.getStrobeCharge(), kGhostTolerance );

    _encounter.chargeStrobe( -1.0f );
    SW_EXPECT_NEAR( 0.0f, _encounter.getStrobeCharge(), kGhostTolerance );
    _encounter.chargeStrobe( 9.0f );
    SW_EXPECT_NEAR( 1.0f, _encounter.getStrobeCharge(), kGhostTolerance ); // 최대에서 멈춘다
    SW_EXPECT_EQUAL( 1, _encounter.releaseStrobe( float3{}, forward ) );
    SW_EXPECT_TRUE( _encounter.findGhost( shyID )->_state == GhostState::Stunned );
}

/**
 * @brief [GhostHuntTest] 기절은 시간이 지나면 풀려 다시 나타난다
 */
SW_TEST_CASE_F( GhostHuntTest, StunWearsOff )
{
    const uint32 ghostID = spawnAttacking( "goob" );
    SW_EXPECT_EQUAL( 1, _encounter.shineBeam( float3{}, float3{ 0.0f, 0.0f, 1.0f } ) );
    _encounter.update( 2.0f );
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Stunned );
    _encounter.update( 1.0f );
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Visible );
}

/**
 * @brief [GhostHuntTest] 흡입 시작: 기절한 유령만, 거리 안에서, 한 번에 하나이다
 */
SW_TEST_CASE_F( GhostHuntTest, SuctionStartGuards )
{
    const uint32 ghostID = spawnAttacking( "goob" );
    SW_EXPECT_FALSE( _encounter.startSuction( ghostID, float3{} ) ); // 아직 공격 중이다
    SW_EXPECT_EQUAL( 1, _encounter.shineBeam( float3{}, float3{ 0.0f, 0.0f, 1.0f } ) );
    SW_EXPECT_FALSE( _encounter.startSuction( 99u, float3{} ) );
    SW_EXPECT_FALSE( _encounter.startSuction( ghostID, float3{ 0.0f, 0.0f, 9.0f } ) ); // 거리 밖이다
    SW_EXPECT_TRUE( _encounter.startSuction( ghostID, float3{} ) );
    SW_EXPECT_EQUAL( ghostID, _encounter.getSuctionTarget() );
    SW_EXPECT_FALSE( _encounter.startSuction( ghostID, float3{} ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::SuctionStarted ) );
}

/**
 * @brief [GhostHuntTest] 반대로 당기면 보너스 피해, 서지 게이지, 덜 끌림이다. 같은 쪽으로 당기면 기본 피해에 그대로 끌린다
 */
SW_TEST_CASE_F( GhostHuntTest, SuctionTugOfWar )
{
    const uint32 ghostID = startSucking();

    const GhostSuctionTick aligned = _encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 0.25f );
    SW_EXPECT_EQUAL( SW_TRUE, aligned._bAligned );
    SW_EXPECT_NEAR( 1.0f, aligned._alignment, kGhostTolerance );
    SW_EXPECT_NEAR( 5.0f, aligned._damage, kGhostTolerance );   // 10 x 0.25 x 2
    SW_EXPECT_NEAR( 0.25f, aligned._drag._z, kGhostTolerance ); // 2 x 1 x 0.25 x 0.5
    SW_EXPECT_NEAR( 0.25f, _encounter.getSurgeGauge(), kGhostTolerance );
    SW_EXPECT_NEAR( 35.0f, _encounter.findGhost( ghostID )->_hp, kGhostTolerance );

    const GhostSuctionTick against = _encounter.updateSuction( float3{ 0.0f, 0.0f, 1.0f }, 0.25f );
    SW_EXPECT_EQUAL( SW_FALSE, against._bAligned );
    SW_EXPECT_NEAR( 2.5f, against._damage, kGhostTolerance );
    SW_EXPECT_NEAR( 0.5f, against._drag._z, kGhostTolerance );
    SW_EXPECT_NEAR( 0.25f, _encounter.getSurgeGauge(), kGhostTolerance );

    const GhostSuctionTick idle = _encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 0.0f );
    SW_EXPECT_NEAR( 0.0f, idle._damage, kGhostTolerance ); // 0 시간은 아무 일도 없다
}

/**
 * @brief [GhostHuntTest] 서지는 게이지가 차야 쓰고, 잡으면 흡입이 끝나며 동전을 떨어뜨린다
 */
SW_TEST_CASE_F( GhostHuntTest, SurgeNeedsFullGaugeAndCatches )
{
    const uint32 ghostID = startSucking();
    SW_EXPECT_FALSE( _encounter.triggerSurge() );
    (void)_encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 1.0f ); // 10 x 1 x 2 = 20, 게이지가 찬다
    SW_EXPECT_NEAR( 1.0f, _encounter.getSurgeGauge(), kGhostTolerance );
    SW_EXPECT_TRUE( _encounter.triggerSurge() );

    const GhostInstance* pGhost = _encounter.findGhost( ghostID );
    SW_EXPECT_TRUE( pGhost->_state == GhostState::Caught );
    SW_EXPECT_NEAR( 0.0f, pGhost->_hp, kGhostTolerance );
    SW_EXPECT_EQUAL( 0u, _encounter.getSuctionTarget() );
    SW_EXPECT_EQUAL( 0, _encounter.countRemaining() );
    SW_EXPECT_FALSE( _encounter.triggerSurge() );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::Surge ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::Caught ) );
    SW_EXPECT_EQUAL( 10, _listEvent.back()._coins );

    _encounter.update( 99.0f ); // 잡힌 유령은 깨어나지 않는다
    SW_EXPECT_TRUE( _encounter.findGhost( ghostID )->_state == GhostState::Caught );
}

/**
 * @brief [GhostHuntTest] 흡입을 끊으면 체력을 남긴 채 달아나 숨는다
 */
SW_TEST_CASE_F( GhostHuntTest, StopSuctionLetsGhostEscape )
{
    const uint32 ghostID = startSucking();
    (void)_encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 0.5f );
    _encounter.stopSuction();
    const GhostInstance* pGhost = _encounter.findGhost( ghostID );
    SW_EXPECT_TRUE( pGhost->_state == GhostState::Hidden );
    SW_EXPECT_NEAR( 30.0f, pGhost->_hp, kGhostTolerance );
    SW_EXPECT_EQUAL( 0u, _encounter.getSuctionTarget() );
    SW_EXPECT_NEAR( 0.0f, _encounter.getSurgeGauge(), kGhostTolerance );
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::Escaped ) );
    _encounter.stopSuction(); // 흡입이 없으면 아무 일도 없다
    SW_EXPECT_EQUAL( 1, countEvents( GhostEventKind::Escaped ) );
}

/**
 * @brief [GhostHuntTest] 강화 단계는 있는 단계로 자르고 흡입 세기를 바꾼다
 */
SW_TEST_CASE_F( GhostHuntTest, VacuumStageClamps )
{
    SW_EXPECT_NEAR( 10.0f, _encounter.computeVacuumPower(), kGhostTolerance );
    _encounter.setVacuumStage( 1 );
    SW_EXPECT_NEAR( 20.0f, _encounter.computeVacuumPower(), kGhostTolerance );
    _encounter.setVacuumStage( 9 );
    SW_EXPECT_EQUAL( 1, _encounter.getVacuumStage() );
    _encounter.setVacuumStage( -3 );
    SW_EXPECT_EQUAL( 0, _encounter.getVacuumStage() );
}

/**
 * @brief [GhostHuntTest] 같은 시드와 같은 입력이면 같은 싸움이다(도망 방향은 시드 난수이다)
 */
SW_TEST_CASE_F( GhostHuntTest, SameSeedSameFight )
{
    GhostEncounter other;
    SW_REQUIRE_TRUE( other.initialize( &_catalog, 3u ) );
    for ( GhostEncounter* pEncounter : { &_encounter, &other } )
    {
        const uint32 ghostID = pEncounter->spawnGhost( "goob", float3{ 0.0f, 0.0f, 2.0f } );
        pEncounter->update( 2.0f );
        pEncounter->update( 2.0f );
        (void)pEncounter->shineBeam( float3{}, float3{ 0.0f, 0.0f, 1.0f } );
        SW_REQUIRE_TRUE( pEncounter->startSuction( ghostID, float3{} ) );
        for ( int32 tick = 0; tick < 5; ++tick )
        {
            (void)pEncounter->updateSuction( float3{ 1.0f, 0.0f, 0.0f }, 0.5f ); // 간격마다 도망 방향을 다시 고른다
        }
    }
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( _encounter, other ) );
}

/**
 * @brief [GhostHuntTest] 왕복: 흡입 중인 싸움을 그대로 잇는다. 이벤트는 비운다. 카탈로그가 없으면 거절한다
 */
SW_TEST_CASE_F( GhostHuntTest, EncounterStateRoundTrip )
{
    const uint32 ghostID = startSucking();
    (void)_encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 0.25f );
    _encounter.chargeStrobe( 0.25f );
    _encounter.setVacuumStage( 1 );

    GhostEncounter restored;
    SW_REQUIRE_TRUE( restored.initialize( &_catalog, 9u ) );
    SW_REQUIRE_TRUE( StateBytesTestUtil::roundTrips( _encounter, restored ) );
    SW_EXPECT_EQUAL( ghostID, restored.getSuctionTarget() );
    SW_EXPECT_EQUAL( 1, restored.getVacuumStage() );
    SW_EXPECT_NEAR( 0.25f, restored.getStrobeCharge(), kGhostTolerance );
    vector<GhostEvent> listEvent;
    restored.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    // 복원한 쪽도 같은 싸움을 잇는다.
    (void)_encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 1.0f );
    (void)restored.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, 1.0f );
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( _encounter, restored ) );

    GhostEncounter noCatalog;
    Archive        written;
    _encounter.writeState( written );
    Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( reading ) );
}

/**
 * @brief [GhostHuntTest] 결함: 체력 · 번호 · 게이지 · 강화 단계의 범위와 흡입 짝을 보지 않았다. 흡입 대상 없이 흡입 중인 유령은 `update` 가 건너뛰어 영영 멈추고, 겹친 번호나 다음 번호 이하의 번호는 새 유령과 겹친다 — 거절한다
 */
SW_TEST_CASE_F( GhostHuntTest, EncounterReadRefusesForgedBytes )
{
    GhostForgedEncounter good;
    SW_REQUIRE_TRUE( good.read( _encounter ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 1, _encounter.countRemaining() );

    GhostForgedEncounter forged = good;
    forged._kind                = "nobody";
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged     = good;
    forged._hp = 41.0f;
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._hp = -1.0f;
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._hp = 0.0f; // 체력 0 인데 잡히지 않았다
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged        = good;
    forged._state = static_cast<uint8>( GhostState::Caught ); // 체력이 남았는데 잡혔다
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._hp = 0.0f;
    SW_EXPECT_TRUE( forged.read( _encounter ) ); // 잡힌 유령은 체력이 0 이다
    forged._state = 9;
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged     = good;
    forged._id = 0;
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged     = good;
    forged._id = 2; // 다음 번호와 겹친다
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged         = good;
    forged._bTwice = SW_TRUE;
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged       = good;
    forged._flee = float3{ 5.0f, 0.0f, 0.0f };
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged        = good;
    forged._stage = 2;
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._stage = -1;
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged         = good;
    forged._strobe = 1.5f;
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._strobe = -0.5f;
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged        = good;
    forged._surge = 1.5f;
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._surge = -0.5f;
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    forged                = good;
    forged._suctionTarget = 7; // 없는 유령이다
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._suctionTarget = 1; // 흡입 중이 아니다
    SW_EXPECT_FALSE( forged.read( _encounter ) );
    forged._state = static_cast<uint8>( GhostState::Sucking );
    SW_EXPECT_TRUE( forged.read( _encounter ) );
    forged._suctionTarget = 0; // 대상 없이 흡입 중이다
    SW_EXPECT_FALSE( forged.read( _encounter ) );

    // 잘린 바이트와, 거절한 뒤 상태가 그대로인 것을 본다.
    SW_REQUIRE_TRUE( good.read( _encounter ) );
    Archive written;
    good.write( written );
    Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( _encounter.readState( truncated ) );
    SW_EXPECT_EQUAL( 1, _encounter.countRemaining() );
}

// ── 저택 ──────────────────────────────────────────────────────────────

/**
 * @brief [GhostHuntRulesTest] 시작 거절: 카탈로그 없음
 */
SW_TEST_CASE( GhostHuntRulesTest, MansionRefusesNoCatalog )
{
    SW_EXPECT_LOG( "GhostEncounter.RefusedInitialize" );
    GhostMansion mansion;
    SW_EXPECT_FALSE( mansion.initialize( nullptr, nullptr, nullptr, SessionStateRefs{}, 1u ) );
    SW_EXPECT_EQUAL( -1, mansion.enterRoom( "foyer" ) );
}

/**
 * @brief [GhostHuntRulesTest] 결함: 저택은 시드와 상수를 섞은 값이 0 이면 난수의 단언에 걸렸다. 갈라진 씨앗은 0 이 되지 않는다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, DerivedSeedIsNeverZero )
{
    SW_EXPECT_EQUAL( GameRandom::kDefaultSeed, GameRandom::deriveSeed( 0xB00B00u, 0xB00B00u ) );
    SW_EXPECT_TRUE( GameRandom::deriveSeed( 1u, 2u ) != 0u );
    GhostMansion mansion;
    SW_EXPECT_TRUE( open( mansion, 0xB00B00u ) );
}

/**
 * @brief [GhostHuntRulesTest] 들어가면 유령이 숨은 채로 나온다. 유령 없는 방은 들어서면 밝고, 없는 방은 -1 이다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, EnterRoomSpawnsGhosts )
{
    SW_EXPECT_EQUAL( -1, _mansion.enterRoom( "nowhere" ) );
    SW_EXPECT_TRUE( _mansion.getCurrentRoom().empty() );
    SW_EXPECT_EQUAL( 2, _mansion.enterRoom( "parlor" ) );
    SW_EXPECT_TRUE( _mansion.getCurrentRoom() == hashed_string( "parlor" ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, _mansion.getEncounter().getGhosts().size() );
    SW_EXPECT_TRUE( _graph.isVisited( "parlor" ) );
    SW_EXPECT_FALSE( _mansion.isRoomLit( "parlor" ) );

    SW_EXPECT_EQUAL( 0, _mansion.enterRoom( "hall" ) );
    SW_EXPECT_TRUE( _mansion.isRoomLit( "hall" ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::RoomLit ) );
    SW_EXPECT_TRUE( _mansion.getEncounter().getGhosts().empty() ); // 앞 방의 유령은 치웠다
    SW_EXPECT_TRUE( _mansion.isRoomLit( "corridor" ) );            // 정의 없는 방은 늘 밝다
}

/**
 * @brief [GhostHuntRulesTest] 다 잡으면 불이 켜지고 열쇠와 동전을 준다. 밝은 방은 다시 나오지 않고, 나갔다 온 어두운 방은 다시 나온다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, ClearingRoomLightsItAndGivesKey )
{
    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) == 1 );
    catchRoomGhosts();
    _mansion.update( 0.01f );

    SW_EXPECT_TRUE( _mansion.isRoomLit( "foyer" ) );
    SW_EXPECT_TRUE( _flags.hasFlag( "lit.foyer" ) );
    SW_EXPECT_EQUAL( 1, _bag.getItemCount( "parlorKey" ) );
    SW_EXPECT_EQUAL( int64{ 10 }, _wallet.getBalance( "Souls" ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::RoomLit ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::KeyAwarded ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::CoinsCollected ) );
    vector<GhostEvent> listGhostEvent;
    _mansion.pollGhostEvents( listGhostEvent );
    SW_EXPECT_EQUAL( 1, VectorUtil::countBy<&GhostEvent::_type>( listGhostEvent, GhostEventKind::Caught ) );

    SW_EXPECT_EQUAL( 0, _mansion.enterRoom( "foyer" ) );
    SW_EXPECT_EQUAL( 2, _mansion.enterRoom( "parlor" ) );
    SW_EXPECT_EQUAL( 2, _mansion.enterRoom( "parlor" ) );
}

/**
 * @brief [GhostHuntRulesTest] 결함: 흡입은 틱 사이에 끝나므로, 잡고 바로 방을 옮기면 `clear` 가 잡은 이벤트를 지워 동전과 불과 열쇠가 사라지고 그 방은 영영 밝아지지 않았다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, LeavingRightAfterCatchKeepsTheCatch )
{
    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) == 1 );
    catchRoomGhosts();
    SW_EXPECT_EQUAL( 0, _mansion.enterRoom( "hall" ) );

    SW_EXPECT_TRUE( _mansion.isRoomLit( "foyer" ) );
    SW_EXPECT_EQUAL( int64{ 10 }, _wallet.getBalance( "Souls" ) );
    SW_EXPECT_EQUAL( 1, _bag.getItemCount( "parlorKey" ) );
    vector<GhostEvent> listGhostEvent;
    _mansion.pollGhostEvents( listGhostEvent );
    SW_EXPECT_EQUAL( 1, VectorUtil::countBy<&GhostEvent::_type>( listGhostEvent, GhostEventKind::Caught ) );
}

/**
 * @brief [GhostHuntRulesTest] 일부만 잡으면 불이 켜지지 않는다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, PartialCatchKeepsRoomDark )
{
    SW_REQUIRE_TRUE( _mansion.enterRoom( "parlor" ) == 2 );
    _mansion.update( 1.0f );
    _mansion.update( 1.0f );
    GhostEncounter& encounter = _mansion.getEncounter();
    (void)encounter.shineBeam( float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 0.0f, 1.0f } );
    const uint32 firstID = encounter.getGhosts().front()._id;
    SW_REQUIRE_TRUE( encounter.startSuction( firstID, float3{} ) );
    (void)encounter.updateSuction( float3{}, 5.0f );
    _mansion.update( 0.01f );
    SW_EXPECT_FALSE( _mansion.isRoomLit( "parlor" ) );
    SW_EXPECT_EQUAL( int64{ 10 }, _wallet.getBalance( "Souls" ) );
}

/**
 * @brief [GhostHuntRulesTest] 가방이 가득 차도 열쇠를 잃지 않는다 - 맡아 두었다가 자리가 나면 방을 옮길 때 준다. 저장해도 남는다(헤더에 적힌 동작 — 보고의 판단 항목이다)
 */
SW_TEST_CASE_F( GhostHuntRulesTest, FullBagKeepsTheKeyForLater )
{
    SW_REQUIRE_TRUE( _bag.initialize( nullptr, 0 ) );
    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) == 1 );
    catchRoomGhosts();
    _mansion.update( 0.01f );
    SW_EXPECT_TRUE( _mansion.isRoomLit( "foyer" ) );
    SW_EXPECT_EQUAL( 0, countEvents( GhostMansionEventKind::KeyAwarded ) );
    SW_EXPECT_EQUAL( 0, _bag.getItemCount( "parlorKey" ) );

    SW_REQUIRE_TRUE( _bag.initialize( nullptr, 4 ) ); // 자리가 났다
    (void)_mansion.enterRoom( "foyer" );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::KeyAwarded ) );
    SW_EXPECT_EQUAL( 1, _bag.getItemCount( "parlorKey" ) );

    (void)_mansion.enterRoom( "foyer" ); // 한 번만 준다
    SW_EXPECT_EQUAL( 1, _bag.getItemCount( "parlorKey" ) );
}

/**
 * @brief [GhostHuntRulesTest] 지금 있는 방의 가구만 뒤질 수 있다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, SearchNeedsToBeInTheRoom )
{
    sw::ItemStackList loot;
    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) >= 0 );
    SW_EXPECT_TRUE( _mansion.searchFurniture( "desk", sw::GhostSearchMode::Vacuum, loot ) == sw::GhostSearchResult::WrongRoom );
}

/**
 * @brief [GhostHuntRulesTest] 문: 열쇠가 없으면 열리지 않고, 열면 열쇠 하나를 쓰며 그래프의 길이 열린다. 열쇠 없는 문은 그냥 열린다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, DoorsUseKeysAndFlags )
{
    SW_EXPECT_TRUE( _mansion.unlockDoor( "nothing" ) == GhostDoorResult::UnknownDoor );
    SW_EXPECT_TRUE( _mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::NeedKey );
    SW_EXPECT_FALSE( _graph.canTraverse( "foyer", "parlor", _flags ) );

    SW_EXPECT_EQUAL( 1, _bag.addItem( "parlorKey", 1 ) );
    SW_EXPECT_TRUE( _mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::Opened );
    SW_EXPECT_EQUAL( 0, _bag.getItemCount( "parlorKey" ) );
    SW_EXPECT_TRUE( _graph.canTraverse( "foyer", "parlor", _flags ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::DoorOpened ) );
    SW_EXPECT_TRUE( _mansion.unlockDoor( "parlorDoor" ) == GhostDoorResult::AlreadyOpen );

    SW_EXPECT_TRUE( _mansion.unlockDoor( "openDoor" ) == GhostDoorResult::Opened );
    SW_EXPECT_TRUE( _flags.hasFlag( "doorOpen" ) );
}

/**
 * @brief [GhostHuntRulesTest] 가구: 방법이 맞아야 하고, 부가 숨었으면 부가 먼저 나오고, 전리품은 한 번만 준다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, FurnitureSearchRules )
{
    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) >= 0 );
    ItemStackList loot;
    SW_EXPECT_TRUE( _mansion.searchFurniture( "nothing", GhostSearchMode::Vacuum, loot ) == GhostSearchResult::UnknownFurniture );
    SW_EXPECT_TRUE( _mansion.searchFurniture( "dresser", GhostSearchMode::Vacuum, loot ) == GhostSearchResult::WrongMode );
    SW_EXPECT_TRUE( _mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::BooFound );
    SW_EXPECT_TRUE( loot.isEmpty() );
    SW_EXPECT_FALSE( _mansion.isSearched( "dresser" ) );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::BooRevealed ) );

    SW_EXPECT_TRUE( _mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::Found );
    SW_EXPECT_EQUAL( 1, loot.getItemCount( "gold" ) );
    SW_EXPECT_TRUE( _mansion.isSearched( "dresser" ) );
    SW_EXPECT_TRUE( _mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::AlreadySearched );
    SW_EXPECT_EQUAL( 1, loot.getItemCount( "gold" ) );

    SW_EXPECT_TRUE( _mansion.searchFurniture( "curtain", GhostSearchMode::Vacuum, loot ) == GhostSearchResult::Found ); // 전리품 표가 없다
    SW_EXPECT_EQUAL( 1, loot.getTotalCount() );
    SW_EXPECT_FALSE( _mansion.isSearched( "nothing" ) );
}

/**
 * @brief [GhostHuntRulesTest] 부 잡기: 들킨 부만 맞고, 체력이 다 되면 잡힌다. 잡힌 부는 더 맞지 않는다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, BooCatchRules )
{
    SW_EXPECT_FALSE( _mansion.damageBoo( "nobody", 99.0f ) );
    SW_EXPECT_FALSE( _mansion.damageBoo( "booB", 99.0f ) ); // 아직 숨어 있다
    SW_EXPECT_TRUE( _mansion.findBoo( "nobody" ) == nullptr );

    SW_EXPECT_EQUAL( 0, _mansion.enterRoom( "hall" ) ); // 가구 없이 방에 숨은 부는 들어서면 나온다
    SW_REQUIRE_TRUE( _mansion.findBoo( "booB" ) != nullptr );
    SW_EXPECT_TRUE( _mansion.findBoo( "booB" )->_state == HidingGhostState::Revealed );

    SW_EXPECT_FALSE( _mansion.damageBoo( "booB", -5.0f ) );
    SW_EXPECT_FALSE( _mansion.damageBoo( "booB", 4.0f ) );
    SW_EXPECT_NEAR( 6.0f, _mansion.findBoo( "booB" )->_hp, kGhostTolerance );
    SW_EXPECT_TRUE( _mansion.damageBoo( "booB", 6.0f ) );
    SW_EXPECT_TRUE( _mansion.findBoo( "booB" )->_state == HidingGhostState::Caught );
    SW_EXPECT_EQUAL( 1, _mansion.countCaughtBoos() );
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::BooCaught ) );
    SW_EXPECT_FALSE( _mansion.damageBoo( "booB", 1.0f ) );
}

/**
 * @brief [GhostHuntRulesTest] 시간 안에 못 잡은 부는 옆 방으로 달아나, 다른 부가 숨지 않은 가구에 숨는다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, BooEscapesToNeighbourRoom )
{
    SW_EXPECT_EQUAL( 0, _mansion.enterRoom( "hall" ) );
    _mansion.update( 4.0f );
    SW_EXPECT_TRUE( _mansion.findBoo( "booB" )->_state == HidingGhostState::Revealed );
    _mansion.update( 1.0f );

    const HidingGhostRuntime* pBoo = _mansion.findBoo( "booB" );
    SW_EXPECT_TRUE( pBoo->_state == HidingGhostState::Hiding );
    SW_EXPECT_TRUE( pBoo->_room == hashed_string( "foyer" ) );        // 이웃은 현관 하나이다
    SW_EXPECT_TRUE( pBoo->_furniture == hashed_string( "curtain" ) ); // 서랍장에는 booA 가 있다
    SW_EXPECT_EQUAL( 1, countEvents( GhostMansionEventKind::BooEscaped ) );

    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) >= 0 );
    ItemStackList loot;
    SW_EXPECT_TRUE( _mansion.searchFurniture( "curtain", GhostSearchMode::Vacuum, loot ) == GhostSearchResult::BooFound );
}

/**
 * @brief [GhostHuntRulesTest] 왕복: 싸움, 부, 뒤진 가구, 지금 방을 잇는다. 같은 입력이면 같은 판이다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, MansionStateRoundTrip )
{
    SW_REQUIRE_TRUE( _mansion.enterRoom( "foyer" ) == 1 );
    ItemStackList loot;
    SW_REQUIRE_TRUE( _mansion.searchFurniture( "curtain", GhostSearchMode::Vacuum, loot ) == GhostSearchResult::Found );
    SW_REQUIRE_TRUE( _mansion.searchFurniture( "dresser", GhostSearchMode::Shake, loot ) == GhostSearchResult::BooFound );
    _mansion.update( 1.0f );

    GhostMansion restored;
    SW_REQUIRE_TRUE( open( restored, 77u ) );
    SW_REQUIRE_TRUE( StateBytesTestUtil::roundTrips( _mansion, restored ) );
    SW_EXPECT_TRUE( restored.getCurrentRoom() == hashed_string( "foyer" ) );
    SW_EXPECT_TRUE( restored.isSearched( "curtain" ) );
    SW_EXPECT_TRUE( restored.findBoo( "booA" )->_state == HidingGhostState::Revealed );
    SW_EXPECT_EQUAL( size_t{ 1 }, restored.getEncounter().getGhosts().size() );
    vector<GhostMansionEvent> listEvent;
    restored.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    _mansion.update( 9.0f );
    restored.update( 9.0f );
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( _mansion, restored ) );
}

/**
 * @brief [GhostHuntRulesTest] 결함: 부의 체력과 방 · 가구 이름, 지금 방을 보지 않았다 — 음수나 최대 초과 체력, 없는 방과 가구를 거절한다. 수가 다른 부와 가구, 깨진 뒤짐 표시도 거절하고 상태는 그대로이다
 */
SW_TEST_CASE_F( GhostHuntRulesTest, MansionReadRefusesForgedBytes )
{
    const GhostForgedBoo good;
    SW_REQUIRE_TRUE( readForged( good, "foyer" ) ); // 형식은 맞다
    SW_EXPECT_TRUE( _mansion.getCurrentRoom() == hashed_string( "foyer" ) );
    SW_EXPECT_TRUE( readForged( good, hashed_string{} ) ); // 아직 방에 들어가지 않았다
    SW_REQUIRE_TRUE( readForged( good, "foyer" ) );

    SW_EXPECT_FALSE( readForged( good, "nowhere" ) );

    GhostForgedBoo forged = good;
    forged._hp            = 31.0f;
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );
    forged._hp = -1.0f;
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );
    forged._hp = 0.0f; // 체력 0 인데 잡히지 않았다
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );

    forged        = good;
    forged._state = static_cast<uint8>( HidingGhostState::Caught ); // 체력이 남았는데 잡혔다
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );
    forged._state = 9;
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );

    forged       = good;
    forged._room = "nowhere";
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );
    forged            = good;
    forged._furniture = "nothing";
    SW_EXPECT_FALSE( readForged( forged, "foyer" ) );

    SW_EXPECT_FALSE( readForged( good, "foyer", 1u ) );
    SW_EXPECT_FALSE( readForged( good, "foyer", 2u, 2u ) );
    SW_EXPECT_FALSE( readForged( good, "foyer", 2u, 3u, 2 ) );
    SW_EXPECT_TRUE( _mansion.getCurrentRoom() == hashed_string( "foyer" ) );
}
