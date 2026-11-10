#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Event/ConsumerGate.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestWorld.h"

#include <algorithm>
#include <limits>

// 횡스크롤 정복 키트의 규칙: 건설 · 일꾼 · 생산 · 수입 · 훈련, 전투(사기 · 성벽 보호), 지휘관, 부대 명령, 점령 · 성문 · 사다리 · 충차, 웨이브, 승패, 카탈로그 오류, 상태 바이트의 범위 검사.
// 고정 걸음 0.5 초는 float 로 정확해 시간 합이 떨어진다(`run` 은 걸음 수). 난수가 없어 값을 그대로 본다. 키트 GF_SideScrollConquest(nogpu).

namespace
{
    constexpr const utf8* kConquestXML = R"(<ConquestCatalog>
  <Rules fixedStep="0.5" incomeInterval="2" captureTime="1" commanderRespawn="1" waveInterval="0" basePopulation="5"/>
  <Start gold="100" wood="10"/>
  <Unit id="spear" hp="40" damage="10" range="1" attackInterval="1" speed="2" pop="1" train="1" structureScale="0.5"><Cost gold="10"/></Unit>
  <Unit id="ram" hp="100" damage="20" range="1" attackInterval="1" speed="2" structureScale="5" siege="Ram"/>
  <Unit id="ladder" hp="20" damage="0" speed="4" siege="Ladder"/>
  <Unit id="giant" hp="5000" damage="1000" range="1" attackInterval="100" speed="0" pop="100"><Cost gold="1"/></Unit>
  <Unit id="rich" hp="10"><Cost gold="9999"/></Unit>
  <Building id="mill" produces="wood" workerSlots="3" cycleTime="1" amount="2"><Cost gold="10"/></Building>
  <Building id="barracks" trains="spear,giant,rich" housing="2"><Cost gold="20"/></Building>
  <Building id="palace"><Cost gold="9999"/></Building>
  <Site id="home" x="0" owner="Player" workers="4" slots="2" housing="1"><Income gold="1"/></Site>
  <Site id="outpost" x="20"/>
  <Site id="keep" x="100" kind="Fortress" owner="Enemy" gate="50" wall="30"/>
</ConquestCatalog>)";

    /** @brief 성문 · 성벽이 없는 판입니다. */
    constexpr const utf8* kOpenXML = R"(<ConquestCatalog>
  <Rules fixedStep="0.5" captureTime="1" waveInterval="0"/>
  <Unit id="spear" hp="40" damage="10" range="1" speed="2"/>
  <Site id="home" x="0" owner="Player"/>
  <Site id="outpost" x="20"/>
  <Site id="keep" x="100" owner="Enemy"/>
</ConquestCatalog>)";

    /** @brief 플레이어 거점이 둘이고 일꾼을 서로 빌리는 판입니다. */
    constexpr const utf8* kTwoFrontXML = R"(<ConquestCatalog>
  <Rules fixedStep="0.5" captureTime="1" waveInterval="0"/>
  <Start gold="100"/>
  <Unit id="spear" hp="40" damage="10" range="1" train="1"><Cost gold="10"/></Unit>
  <Building id="mill" produces="wood" workerSlots="3"/>
  <Building id="barracks" trains="spear"/>
  <Site id="home" x="0" owner="Player" workers="2" slots="1"/>
  <Site id="farm" x="30" owner="Player" workers="2" slots="1"/>
  <Site id="keep" x="100" owner="Enemy"/>
</ConquestCatalog>)";

    constexpr const utf8* kWaveXML = R"(<ConquestCatalog>
  <Rules fixedStep="0.5" captureTime="1" waveUnit="spear" waveInterval="60" waveBaseCount="2" waveCountPerMinute="2" waveCountPerSite="1"/>
  <Unit id="spear" hp="40" damage="10" range="1"/>
  <Site id="home" x="0" owner="Player"/>
  <Site id="outpost" x="20"/>
  <Site id="keep" x="100" owner="Enemy"/>
</ConquestCatalog>)";

    constexpr const utf8* kGarrisonXML = R"(<ConquestCatalog>
  <Rules fixedStep="0.5" waveInterval="0"/>
  <Unit id="spear" hp="40"/>
  <Unit id="ram" hp="100" siege="Ram"/>
  <Site id="home" x="0" owner="Player" garrison="spear"/>
  <Site id="keep" x="100" owner="Enemy" garrison="spear:2,ram"/>
</ConquestCatalog>)";

    /** @brief 상태 바이트의 비교입니다. */
    struct ConquestTestUtil
    {
        static bool isSameState( const sw::ConquestWorld& lhs, const sw::ConquestWorld& rhs )
        {
            sw::Archive left;
            sw::Archive right;
            lhs.writeState( left );
            rhs.writeState( right );
            return left.getSize() == right.getSize() && std::equal( left.getData(), left.getData() + left.getSize(), right.getData() );
        }
    };

    /** @brief 판 상태 바이트를 손으로 씁니다(`kConquestXML` 기준). 초기값은 맞는 상태이고 케이스가 칸 하나를 망칩니다. */
    struct ConquestForgedWorld
    {
        sw::hashed_string _firstSiteID{ "home" };
        sw::hashed_string _buildingID{ "mill" };
        sw::hashed_string _queueUnit{};
        sw::hashed_string _unitName{ "spear" };
        float32           _keepGate{ 50.0f };
        float32           _keepWall{ 30.0f };
        float32           _keepCapture{ 0.0f };
        float32           _cycle{ 0.0f };
        float32           _train{ 0.0f };
        float32           _unitHealth{ 40.0f };
        float32           _gold{ 1.0f };
        float32           _commanderHealth{ 100.0f };
        float32           _moveAxis{ 0.0f };
        float32           _facing{ 1.0f };
        float32           _accumulator{ 0.0f };
        float32           _elapsed{ 0.0f };
        float32           _incomeTimer{ 0.0f };
        float32           _waveTimer{ 0.0f };
        int32             _buildingCount{ 1 };
        int32             _siteIndex{ 0 };
        int32             _workers{ 1 };
        int32             _unitCount{ 1 };
        int32             _unitID{ 1 };
        int32             _squadSlot{ 0 };
        int32             _nextUnitID{ 2 };
        uint8             _keepOwner{ static_cast<uint8>( sw::ConquestTeam::Enemy ) };
        uint8             _keepCaptureTeam{ 0 };
        uint8             _team{ static_cast<uint8>( sw::ConquestTeam::Player ) };
        uint8             _order{ 0 };
        uint8             _alive{ 1 };
        uint8             _planted{ 0 };
        uint8             _commanderAlive{ 1 };
        uint8             _victory{ 0 };
        uint8             _defeat{ 0 };

        void write( sw::Archive& written, const sw::ConquestCatalog& catalog ) const
        {
            const sw::vector<sw::ConquestSiteDef>& listSite = catalog.getSites();
            written << static_cast<uint32>( listSite.size() );
            for ( size_t index = 0; index < listSite.size(); ++index )
            {
                const sw::ConquestSiteDef& def   = listSite[index];
                const bool                 bKeep = def._id == sw::hashed_string( "keep" );
                sw::StateIO::writeName( written, index == 0 ? _firstSiteID : def._id );
                written << ( bKeep ? _keepGate : def._gateHealth );
                written << ( bKeep ? _keepWall : def._wallHealth );
                written << ( bKeep ? _keepCapture : 0.0f );
                written << ( bKeep ? _keepOwner : static_cast<uint8>( def._owner ) );
                written << ( bKeep ? _keepCaptureTeam : static_cast<uint8>( 0 ) );
            }

            written << static_cast<uint32>( _buildingCount );
            for ( int32 building = 0; building < _buildingCount; ++building )
            {
                sw::StateIO::writeName( written, _buildingID );
                written << static_cast<uint32>( _queueUnit.empty() ? 0 : 1 );
                if ( _queueUnit.empty() == false )
                    sw::StateIO::writeName( written, _queueUnit );
                written << _cycle;
                written << _train;
                written << _siteIndex;
                written << _workers;
            }

            written << static_cast<uint32>( _unitCount );
            for ( int32 unit = 0; unit < _unitCount; ++unit )
            {
                sw::StateIO::writeName( written, _unitName );
                written << 5.0f;
                written << _unitHealth;
                sw::StateIO::writeCountdown( written, sw::Countdown{} );
                written << 5.0f;
                written << 0.0f;
                written << _unitID;
                written << _squadSlot;
                written << _team;
                written << _order;
                written << _alive;
                written << _planted;
            }

            sw::StatBlock resource;
            resource.setValue( "gold", _gold );
            resource.writeState( written );

            sw::FixedStepTimer timer( 0.5f, 1.0f );
            timer._accumulator = _accumulator;
            written << 0.0f;
            written << _commanderHealth;
            sw::StateIO::writeCountdown( written, sw::Countdown{} );
            sw::StateIO::writeCountdown( written, sw::Countdown{} );
            written << _moveAxis;
            written << _facing;
            written << _commanderAlive;
            sw::StateIO::writeStepTimer( written, timer );
            written << _elapsed;
            written << _incomeTimer;
            written << _waveTimer;
            written << _nextUnitID;
            written << _victory;
            written << _defeat;
        }
    };

    /** @brief 카탈로그와 판 하나입니다. */
    struct ConquestRulesTest
    {
        sw::ConquestCatalog           _catalog{};
        sw::ConquestCatalog           _otherCatalog{};
        sw::ConquestWorld             _world{};
        sw::ConsumerLease             _lease{};
        sw::vector<sw::ConquestEvent> _listEvent{};

        ConquestRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kConquestXML, "ConquestTest" ) );
            _world.initialize( &_catalog );
            SW_REQUIRE_TRUE( _world.claimEvents( _lease ) );
        }

        /** @brief 다른 데이터로 새 판을 엽니다(케이스마다 한 번). */
        void openOther( const utf8* pXML )
        {
            SW_REQUIRE_TRUE( _otherCatalog.loadFromXMLText( pXML, "ConquestTest" ) );
            _world.initialize( &_otherCatalog );
        }

        void run( int32 stepCount )
        {
            for ( int32 step = 0; step < stepCount; ++step )
            {
                _world.update( 0.5f );
            }
        }

        int32 countEvents( sw::ConquestEvent::Kind kind )
        {
            _world.pollEvents( _lease, _listEvent );
            return sw::VectorUtil::countBy<&sw::ConquestEvent::_kind>( _listEvent, kind );
        }

        /** @brief 지금까지의 이벤트에서 @p kind 가 처음 나온 값입니다. 없으면 비어 있습니다. */
        sw::optional<int32> findFirstValue( sw::ConquestEvent::Kind kind )
        {
            _world.pollEvents( _lease, _listEvent );
            const sw::ConquestEvent* pEvent = sw::VectorUtil::findBy<&sw::ConquestEvent::_kind>( _listEvent, kind );
            return pEvent != nullptr ? sw::optional<int32>{ pEvent->_value } : sw::nullopt;
        }

        int32 spawn( const sw::hashed_string& unitID, sw::ConquestTeam team, float32 x, sw::ConquestOrder order = sw::ConquestOrder::Hold ) { return _world.spawnUnit( unitID, team, x, order ); }

        bool readForged( const ConquestForgedWorld& forged )
        {
            sw::Archive written;
            forged.write( written, _catalog );
            sw::Archive reading( written.getData(), written.getSize() );
            return _world.readState( reading );
        }
    };
} // namespace

/**
 * @brief [ConquestRulesTest] 새 판: 거점 · 시작 자원 · 인구 한도 · 일꾼 · 지휘관 위치, 주둔군(적은 부대가 아니다, 플레이어 주둔군은 부대에 든다)
 */
SW_TEST_CASE_F( ConquestRulesTest, InitialState )
{
    SW_EXPECT_EQUAL( 3u, _world.getSites().size() );
    SW_EXPECT_EQUAL( 100, _world.getResource( "gold" ) );
    SW_EXPECT_EQUAL( 10, _world.getResource( "wood" ) );
    SW_EXPECT_EQUAL( 0, _world.getResource( "iron" ) );
    SW_EXPECT_EQUAL( 1, _world.computeTerritory() );
    SW_EXPECT_EQUAL( 6, _world.computePopulationCap() );
    SW_EXPECT_EQUAL( 0, _world.computePopulation() );
    SW_EXPECT_EQUAL( 4, _world.computeFreeWorkers() );
    SW_EXPECT_NEAR( 0.0, _world.getCommander()._x, 1.0e-5 );
    SW_EXPECT_NEAR( 100.0, _world.getCommander()._health, 1.0e-5 );
    SW_EXPECT_NEAR( 50.0, _world.findSite( "keep" )->_gateHealth, 1.0e-5 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Enemy, _world.findSite( "keep" )->_owner );
    SW_EXPECT_TRUE( _world.findSite( "nope" ) == nullptr );
    SW_EXPECT_TRUE( _world.findUnit( 99 ) == nullptr );
    SW_EXPECT_FALSE( _world.isVictory() );
    SW_EXPECT_FALSE( _world.isDefeat() );

    openOther( kGarrisonXML );
    SW_EXPECT_EQUAL( 4u, _world.getUnits().size() );
    SW_EXPECT_EQUAL( 1, _world.getSquadSize() );
    SW_EXPECT_EQUAL( 3, sw::VectorUtil::countBy<&sw::ConquestUnit::_team>( _world.getUnits(), sw::ConquestTeam::Enemy ) );
    for ( const sw::ConquestUnit& unit : _world.getUnits() )
    {
        SW_EXPECT_EQUAL( sw::ConquestOrder::Hold, unit._order );
        SW_EXPECT_EQUAL( unit._team == sw::ConquestTeam::Player ? 0 : -1, unit._squadSlot );
    }
}

/**
 * @brief [ConquestRulesTest] 건설: 모르는 정의, 내 거점이 아닌 곳, 슬롯, 비용(모자라면 아무것도 빼지 않는다), 주거는 인구 한도를 늘린다
 */
SW_TEST_CASE_F( ConquestRulesTest, PlaceBuilding )
{
    SW_EXPECT_EQUAL( sw::ConquestResult::UnknownDef, _world.placeBuilding( "nope", "home" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::UnknownDef, _world.placeBuilding( "mill", "nope" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::SiteNotOwned, _world.placeBuilding( "mill", "keep" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::SiteNotOwned, _world.placeBuilding( "mill", "outpost" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::NotEnoughResources, _world.placeBuilding( "palace", "home" ) );
    SW_EXPECT_EQUAL( 100, _world.getResource( "gold" ) );

    SW_EXPECT_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "mill", "home" ) );
    SW_EXPECT_EQUAL( 90, _world.getResource( "gold" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "barracks", "home" ) );
    SW_EXPECT_EQUAL( 70, _world.getResource( "gold" ) );
    SW_EXPECT_EQUAL( 8, _world.computePopulationCap() );
    SW_EXPECT_EQUAL( sw::ConquestResult::NoBuildSlot, _world.placeBuilding( "mill", "home" ) );
    SW_EXPECT_EQUAL( 2u, _world.getBuildings().size() );
    SW_EXPECT_EQUAL( 70, _world.getResource( "gold" ) );
}

/**
 * @brief [ConquestRulesTest] 일꾼과 생산: 없는 건물, 슬롯까지 맞추기, 남는 일꾼, 주기마다 일꾼 × 양. 일꾼이 없으면 만들지 않고, 수입은 주기마다 들어온다
 */
SW_TEST_CASE_F( ConquestRulesTest, WorkersProductionAndIncome )
{
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "mill", "home" ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "mill", "home" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::InvalidBuilding, _world.assignWorkers( 2, 1 ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::InvalidBuilding, _world.assignWorkers( -1, 1 ) );

    SW_EXPECT_EQUAL( sw::ConquestResult::Ok, _world.assignWorkers( 0, 99 ) ); // 슬롯 3 까지
    SW_EXPECT_EQUAL( 3, _world.getBuildings()[0]._workers );
    SW_EXPECT_EQUAL( 1, _world.computeFreeWorkers() );
    SW_EXPECT_EQUAL( sw::ConquestResult::NoFreeWorkers, _world.assignWorkers( 1, 2 ) );
    SW_EXPECT_EQUAL( 0, _world.getBuildings()[1]._workers );
    SW_EXPECT_EQUAL( sw::ConquestResult::Ok, _world.assignWorkers( 0, 2 ) );
    SW_EXPECT_EQUAL( 2, _world.computeFreeWorkers() );

    run( 4 ); // 2 초: 주기 1 초가 둘(일꾼 2 × 양 2), 수입 주기 하나
    SW_EXPECT_EQUAL( 18, _world.getResource( "wood" ) );
    SW_EXPECT_EQUAL( 2, countEvents( sw::ConquestEvent::Kind::ResourceProduced ) );
    SW_EXPECT_TRUE( findFirstValue( sw::ConquestEvent::Kind::ResourceProduced ) == 4 );
    SW_EXPECT_EQUAL( 81, _world.getResource( "gold" ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::IncomePaid ) );
}

/**
 * @brief [ConquestRulesTest] 훈련: 거절의 순서와 비용, 대기열까지 센 인구 한도, 훈련 시간이 지나면 부대에 들어온다
 */
SW_TEST_CASE_F( ConquestRulesTest, TrainUnit )
{
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "barracks", "home" ) ); // 금 80, 인구 한도 8
    SW_EXPECT_EQUAL( sw::ConquestResult::InvalidBuilding, _world.trainUnit( 9, "spear" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::UnknownDef, _world.trainUnit( 0, "nope" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::CannotTrainHere, _world.trainUnit( 0, "ram" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::PopulationCap, _world.trainUnit( 0, "giant" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::NotEnoughResources, _world.trainUnit( 0, "rich" ) );
    SW_EXPECT_EQUAL( 80, _world.getResource( "gold" ) );

    SW_EXPECT_EQUAL( sw::ConquestResult::Ok, _world.trainUnit( 0, "spear" ) );
    SW_EXPECT_EQUAL( 70, _world.getResource( "gold" ) );
    SW_EXPECT_EQUAL( 1, _world.computePopulation() );
    SW_EXPECT_EQUAL( 1u, _world.getBuildings()[0]._listQueue.size() );

    run( 1 );
    SW_EXPECT_TRUE( _world.getUnits().empty() );
    run( 1 );
    SW_REQUIRE_EQUAL( 1u, _world.getUnits().size() );
    SW_EXPECT_TRUE( _world.getBuildings()[0]._listQueue.empty() );
    SW_EXPECT_EQUAL( 1, _world.getSquadSize() );
    SW_EXPECT_EQUAL( 1, _world.computePopulation() );
    const int32 trainedID = findFirstValue( sw::ConquestEvent::Kind::UnitTrained ).value();
    SW_REQUIRE_TRUE( _world.findUnit( trainedID ) != nullptr );
    SW_EXPECT_EQUAL( sw::ConquestOrder::Follow, _world.findUnit( trainedID )->_order );

    for ( int32 index = 0; index < 7; ++index )
    {
        SW_EXPECT_EQUAL( sw::ConquestResult::Ok, _world.trainUnit( 0, "spear" ) );
    }
    SW_EXPECT_EQUAL( 8, _world.computePopulation() );
    SW_EXPECT_EQUAL( sw::ConquestResult::PopulationCap, _world.trainUnit( 0, "spear" ) );
}

/**
 * @brief [ConquestRulesTest] 점령: 중립 거점은 머물면 넘어오고, 양쪽이 함께 있으면 진행하지 않는다
 */
SW_TEST_CASE_F( ConquestRulesTest, CaptureNeutralSite )
{
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 20.0f ) >= 1 );
    run( 1 );
    SW_EXPECT_NEAR( 0.5, _world.findSite( "outpost" )->_captureProgress, 1.0e-5 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Player, _world.findSite( "outpost" )->_captureTeam );
    run( 1 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Player, _world.findSite( "outpost" )->_owner );
    SW_EXPECT_NEAR( 0.0, _world.findSite( "outpost" )->_captureProgress, 1.0e-5 );
    SW_EXPECT_EQUAL( 2, _world.computeTerritory() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::SiteCaptured ) );
    SW_EXPECT_FALSE( _world.isVictory() );
}

/**
 * @brief [ConquestRulesTest] 점령: 양쪽이 함께 있는 구역은 진행하지 않는다
 */
SW_TEST_CASE_F( ConquestRulesTest, ContestedSiteDoesNotProgress )
{
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 18.0f ) >= 1 );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Enemy, 22.0f ) >= 1 ); // 4 떨어져 서로 때리지 못한다
    run( 6 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Neutral, _world.findSite( "outpost" )->_owner );
    SW_EXPECT_NEAR( 0.0, _world.findSite( "outpost" )->_captureProgress, 1.0e-5 );
}

/**
 * @brief [ConquestRulesTest] 공성: 성문이 서 있으면 안에 있어도 점령하지 못하고, 사다리가 걸치면 넘어가 점령한다. 마지막 적 거점이면 승리하고 판이 멈춘다
 */
SW_TEST_CASE_F( ConquestRulesTest, GateBarsCaptureUntilLadderPlants )
{
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 100.0f ) >= 1 ); // 성문 x = 96 은 사거리 밖이다
    run( 8 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Enemy, _world.findSite( "keep" )->_owner );
    SW_EXPECT_NEAR( 0.0, _world.findSite( "keep" )->_captureProgress, 1.0e-5 );

    const int32 ladderID = spawn( "ladder", sw::ConquestTeam::Player, 90.0f, sw::ConquestOrder::Charge );
    run( 3 ); // 걸음당 2: 92, 94, 95.5(성문 앞)
    SW_REQUIRE_TRUE( _world.findUnit( ladderID ) != nullptr );
    SW_EXPECT_NEAR( 95.5, _world.findUnit( ladderID )->_x, 1.0e-4 );
    SW_EXPECT_EQUAL( SW_FALSE, _world.findUnit( ladderID )->_bPlanted );
    run( 1 );
    SW_EXPECT_EQUAL( SW_TRUE, _world.findUnit( ladderID )->_bPlanted );
    SW_EXPECT_NEAR( 50.0, _world.findSite( "keep" )->_gateHealth, 1.0e-5 ); // 부수지 않고 넘는다
    run( 1 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Player, _world.findSite( "keep" )->_owner );
    SW_EXPECT_TRUE( _world.isVictory() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::Victory ) );

    const float32 elapsed = _world.getElapsed();
    run( 4 );
    SW_EXPECT_NEAR( elapsed, _world.getElapsed(), 1.0e-5 );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::Victory ) );
}

/**
 * @brief [ConquestRulesTest] 공성: 충차는 성문을 부수고 성벽을 부순 뒤 들어가 점령한다
 */
SW_TEST_CASE_F( ConquestRulesTest, RamBreaksGateThenWall )
{
    SW_EXPECT_TRUE( spawn( "ram", sw::ConquestTeam::Player, 90.0f, sw::ConquestOrder::Charge ) >= 1 );
    run( 5 );
    SW_EXPECT_NEAR( 50.0, _world.findSite( "keep" )->_gateHealth, 1.0e-5 );
    run( 1 ); // x = 95 에서 사거리 1: 20 × 5 = 100
    SW_EXPECT_NEAR( 0.0, _world.findSite( "keep" )->_gateHealth, 1.0e-5 );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::GateBroken ) );
    SW_EXPECT_NEAR( 30.0, _world.findSite( "keep" )->_wallHealth, 1.0e-5 );
    run( 2 ); // 공격 간격 1 초
    SW_EXPECT_NEAR( 0.0, _world.findSite( "keep" )->_wallHealth, 1.0e-5 );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::WallBroken ) );
    run( 10 );
    SW_EXPECT_TRUE( _world.isVictory() );
}

/**
 * @brief [ConquestRulesTest] 성문은 지휘관도 막는다
 */
SW_TEST_CASE_F( ConquestRulesTest, GateBlocksTheCommander )
{
    _world.setCommanderMove( 1.0f );
    run( 60 );
    SW_EXPECT_NEAR( 95.5, _world.getCommander()._x, 1.0e-4 );
    SW_EXPECT_TRUE( _world.findSite( "keep" )->_gateHealth > 0.0f );
    SW_EXPECT_TRUE( _world.findSite( "keep" )->_gateHealth < 50.0f ); // 닿으니 때렸다
}

/**
 * @brief [ConquestRulesTest] 돌격: 적이 없으면 앞쪽의 가장 가까운 남의 거점을 차례로 점령한다
 */
SW_TEST_CASE_F( ConquestRulesTest, ChargeCapturesSitesAhead )
{
    openOther( kOpenXML );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 10.0f, sw::ConquestOrder::Charge ) >= 1 );
    run( 120 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Player, _world.findSite( "outpost" )->_owner );
    SW_EXPECT_EQUAL( 2, countEvents( sw::ConquestEvent::Kind::SiteCaptured ) );
    SW_EXPECT_TRUE( _world.isVictory() );
}

/**
 * @brief [ConquestRulesTest] 피해: 지휘관 근처의 플레이어 병사는 사기로 늘고, 성벽이 서 있는 거점의 수비대는 줄고, 때린 양을 적는다
 */
SW_TEST_CASE_F( ConquestRulesTest, MoraleAndWallProtection )
{
    const int32 farPlayer  = spawn( "spear", sw::ConquestTeam::Player, 50.0f );
    const int32 farEnemy   = spawn( "spear", sw::ConquestTeam::Enemy, 50.5f );
    const int32 nearPlayer = spawn( "spear", sw::ConquestTeam::Player, 2.0f );
    const int32 nearEnemy  = spawn( "spear", sw::ConquestTeam::Enemy, 2.5f );
    const int32 wallEnemy  = spawn( "spear", sw::ConquestTeam::Enemy, 100.0f );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 99.5f ) >= 1 );
    run( 1 );

    SW_EXPECT_NEAR( 30.0, _world.findUnit( farEnemy )->_health, 1.0e-4 );
    SW_EXPECT_NEAR( 30.0, _world.findUnit( farPlayer )->_health, 1.0e-4 );
    SW_EXPECT_NEAR( 10.0, _world.findUnit( farPlayer )->_damageDealt, 1.0e-4 );
    SW_EXPECT_NEAR( 27.5, _world.findUnit( nearEnemy )->_health, 1.0e-4 ); // 10 × 1.25
    SW_EXPECT_NEAR( 12.5, _world.findUnit( nearPlayer )->_damageDealt, 1.0e-4 );
    SW_EXPECT_NEAR( 35.0, _world.findUnit( wallEnemy )->_health, 1.0e-4 ); // 성벽 보호 50%

    run( 1 ); // 공격 간격 1 초가 아직 남았다
    SW_EXPECT_NEAR( 30.0, _world.findUnit( farEnemy )->_health, 1.0e-4 );
    run( 1 );
    SW_EXPECT_NEAR( 20.0, _world.findUnit( farEnemy )->_health, 1.0e-4 );
}

/**
 * @brief [ConquestRulesTest] 쓰러진 병사는 걸음 끝에 사라지고 부대 슬롯을 다시 매기며, 인구 한도에서 빠진다
 */
SW_TEST_CASE_F( ConquestRulesTest, DeadUnitsLeaveTheSquad )
{
    const int32 victim   = spawn( "spear", sw::ConquestTeam::Player, 70.0f );
    const int32 survivor = spawn( "spear", sw::ConquestTeam::Player, 80.0f );
    SW_EXPECT_EQUAL( 1, _world.findUnit( survivor )->_squadSlot );
    SW_EXPECT_EQUAL( 2, _world.computePopulation() );
    SW_EXPECT_TRUE( spawn( "giant", sw::ConquestTeam::Enemy, 70.5f ) >= 1 );
    run( 1 );

    SW_EXPECT_TRUE( _world.findUnit( victim ) == nullptr );
    SW_EXPECT_EQUAL( 0, _world.findUnit( survivor )->_squadSlot );
    SW_EXPECT_EQUAL( 1, _world.getSquadSize() );
    SW_EXPECT_EQUAL( 1, _world.computePopulation() );
    SW_EXPECT_TRUE( findFirstValue( sw::ConquestEvent::Kind::UnitDied ) == victim );
}

/**
 * @brief [ConquestRulesTest] 지휘관 이동: 입력은 -1..1 로 잘리고 유지되며, 바라보는 쪽을 정한다
 */
SW_TEST_CASE_F( ConquestRulesTest, CommanderMove )
{
    _world.setCommanderMove( 2.0f );
    run( 1 );
    SW_EXPECT_NEAR( 2.0, _world.getCommander()._x, 1.0e-4 ); // 속도 4 × 0.5
    SW_EXPECT_NEAR( 1.0, _world.getCommander()._facing, 1.0e-5 );
    run( 1 );
    SW_EXPECT_NEAR( 4.0, _world.getCommander()._x, 1.0e-4 );

    _world.setCommanderMove( -0.5f );
    run( 1 );
    SW_EXPECT_NEAR( 3.0, _world.getCommander()._x, 1.0e-4 );
    SW_EXPECT_NEAR( -1.0, _world.getCommander()._facing, 1.0e-5 );

    _world.setCommanderMove( 0.0f );
    run( 1 );
    SW_EXPECT_NEAR( 3.0, _world.getCommander()._x, 1.0e-4 );
    SW_EXPECT_NEAR( -1.0, _world.getCommander()._facing, 1.0e-5 );
}

/**
 * @brief [ConquestRulesTest] 지휘관 죽음과 부활: 체력 0, 부활 시간 뒤 첫 거점에서 가득 찬 체력으로 일어난다
 */
SW_TEST_CASE_F( ConquestRulesTest, CommanderDiesAndRespawns )
{
    _world.setCommanderMove( 1.0f );
    run( 3 );
    _world.setCommanderMove( 0.0f );
    SW_EXPECT_NEAR( 6.0, _world.getCommander()._x, 1.0e-4 );

    SW_EXPECT_TRUE( spawn( "giant", sw::ConquestTeam::Enemy, 6.5f ) >= 1 ); // 첫 타 뒤 간격 100 초, 구역 밖이라 거점은 그대로다
    run( 1 );
    SW_EXPECT_EQUAL( SW_FALSE, _world.getCommander()._bAlive );
    SW_EXPECT_NEAR( 0.0, _world.getCommander()._health, 1.0e-5 );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::CommanderDied ) );

    run( 1 );
    SW_EXPECT_EQUAL( SW_FALSE, _world.getCommander()._bAlive );
    run( 1 );
    SW_EXPECT_EQUAL( SW_TRUE, _world.getCommander()._bAlive );
    SW_EXPECT_NEAR( 100.0, _world.getCommander()._health, 1.0e-5 );
    SW_EXPECT_NEAR( 0.0, _world.getCommander()._x, 1.0e-5 );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::CommanderRespawned ) );
}

/**
 * @brief [ConquestRulesTest] 부대 명령: 따라오기는 지휘관 뒤 슬롯으로, 대기는 그 자리 슬롯을 지키고, 적은 명령을 받지 않는다
 */
SW_TEST_CASE_F( ConquestRulesTest, SquadOrders )
{
    const int32 soldier = spawn( "spear", sw::ConquestTeam::Player, 10.0f, sw::ConquestOrder::Follow );
    const int32 enemy   = spawn( "spear", sw::ConquestTeam::Enemy, 60.0f );
    run( 12 ); // 걸음당 1: 지휘관 뒤 간격 1
    SW_EXPECT_NEAR( -1.0, _world.findUnit( soldier )->_x, 1.0e-4 );

    _world.issueOrder( sw::ConquestOrder::Hold );
    SW_EXPECT_EQUAL( sw::ConquestOrder::Hold, _world.findUnit( soldier )->_order );
    SW_EXPECT_NEAR( -1.0, _world.findUnit( soldier )->_holdX, 1.0e-4 );
    _world.setCommanderMove( 1.0f );
    run( 5 );
    _world.setCommanderMove( 0.0f );
    SW_EXPECT_NEAR( 10.0, _world.getCommander()._x, 1.0e-4 );
    SW_EXPECT_NEAR( -1.0, _world.findUnit( soldier )->_x, 1.0e-4 );

    _world.issueOrder( sw::ConquestOrder::Follow );
    run( 12 );
    SW_EXPECT_NEAR( 9.0, _world.findUnit( soldier )->_x, 1.0e-4 );
    SW_EXPECT_EQUAL( sw::ConquestOrder::Hold, _world.findUnit( enemy )->_order );
}

/**
 * @brief [ConquestRulesTest] 반격 웨이브: 기본 + 지난 분 × 분당 + (영토 - 1) × 거점당, 가장 오른쪽 적 거점에서 돌격으로 나온다
 */
SW_TEST_CASE_F( ConquestRulesTest, CounterWaves )
{
    openOther( kWaveXML );
    SW_EXPECT_EQUAL( 2, _world.computeWaveSize() );
    run( 119 );
    SW_EXPECT_TRUE( _world.getUnits().empty() );
    run( 1 ); // 60 초: 2 + 2
    SW_EXPECT_TRUE( findFirstValue( sw::ConquestEvent::Kind::WaveSpawned ) == 4 );
    SW_REQUIRE_EQUAL( 4u, _world.getUnits().size() );
    SW_EXPECT_NEAR( 0.0, _world.getWaveTimer(), 1.0e-4 );
    for ( const sw::ConquestUnit& unit : _world.getUnits() )
    {
        SW_EXPECT_EQUAL( sw::ConquestTeam::Enemy, unit._team );
        SW_EXPECT_EQUAL( sw::ConquestOrder::Charge, unit._order );
    }
    SW_EXPECT_NEAR( 100.0, _world.getUnits()[0]._x, 1.0e-4 );
    SW_EXPECT_NEAR( 103.0, _world.getUnits()[3]._x, 1.0e-4 );
}

/**
 * @brief [ConquestRulesTest] 웨이브 크기는 영토에 비례한다
 */
SW_TEST_CASE_F( ConquestRulesTest, WaveSizeGrowsWithTerritory )
{
    openOther( kWaveXML );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 20.0f ) >= 1 );
    run( 2 );
    SW_EXPECT_EQUAL( 2, _world.computeTerritory() );
    SW_EXPECT_EQUAL( 3, _world.computeWaveSize() ); // 1 초는 분당 몫이 아직 0
}

/**
 * @brief [ConquestRulesTest] 거점을 잃으면: 건물이 멈추고 대기열이 사라지며 내 명령을 받지 않는다.
 *        결함: 다른 거점의 건물이 잃은 거점의 일꾼을 쓰고 있으면 남는 일꾼이 음수가 되어, 없는 일꾼이 계속 생산했다 - 모자란 만큼 뺀다.
 */
SW_TEST_CASE_F( ConquestRulesTest, LosingASiteReleasesWorkers )
{
    openOther( kTwoFrontXML );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "mill", "home" ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "barracks", "farm" ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.assignWorkers( 0, 3 ) ); // 일꾼 넷 중 셋, farm 의 몫을 빌렸다
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.trainUnit( 1, "spear" ) );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Enemy, 30.0f ) >= 1 );
    run( 2 );

    SW_EXPECT_EQUAL( sw::ConquestTeam::Enemy, _world.findSite( "farm" )->_owner );
    SW_EXPECT_EQUAL( 1, _world.computeTerritory() );
    SW_EXPECT_TRUE( _world.getBuildings()[1]._listQueue.empty() );
    SW_EXPECT_EQUAL( 2, _world.getBuildings()[0]._workers );
    SW_EXPECT_EQUAL( 0, _world.computeFreeWorkers() );
    SW_EXPECT_EQUAL( 1u, _world.getUnits().size() ); // 대기열의 병사는 나오지 않았다
    SW_EXPECT_EQUAL( sw::ConquestResult::SiteNotOwned, _world.assignWorkers( 1, 0 ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::SiteNotOwned, _world.trainUnit( 1, "spear" ) );

    // 이 상태도 읽힌다(남는 일꾼이 음수면 읽기가 거절한다).
    sw::Archive written;
    _world.writeState( written );
    sw::Archive       reading( written.getData(), written.getSize() );
    sw::ConquestWorld restored;
    restored.initialize( &_otherCatalog );
    SW_EXPECT_TRUE( restored.readState( reading ) );
}

/**
 * @brief [ConquestRulesTest] 패배: 플레이어 거점이 없으면 끝난다
 */
SW_TEST_CASE_F( ConquestRulesTest, DefeatWhenEverySiteFalls )
{
    _world.setCommanderMove( 1.0f );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Enemy, 0.0f ) >= 1 );
    run( 10 );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Enemy, _world.findSite( "home" )->_owner );
    SW_EXPECT_TRUE( _world.isDefeat() );
    SW_EXPECT_FALSE( _world.isVictory() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ConquestEvent::Kind::Defeat ) );
}

/**
 * @brief [ConquestRulesTest] 깨진 입력: 모르는 병종 · 중립 병사는 -1. 카탈로그 없는 판은 아무 일도 하지 않는다.
 *        결함: 음수 자원을 받아 세이브 범위를 깼다 - 거절한다.
 */
SW_TEST_CASE_F( ConquestRulesTest, BrokenInput )
{
    SW_EXPECT_EQUAL( -1, _world.spawnUnit( "nope", sw::ConquestTeam::Player, 0.0f, sw::ConquestOrder::Hold ) );
    SW_EXPECT_EQUAL( -1, _world.spawnUnit( "spear", sw::ConquestTeam::Neutral, 0.0f, sw::ConquestOrder::Hold ) );

    SW_EXPECT_LOG( "ConquestWorld.NegativeResource" );
    _world.addResource( "gold", -5.0f );
    SW_EXPECT_EQUAL( 100, _world.getResource( "gold" ) );
    _world.addResource( "gold", 5.0f );
    SW_EXPECT_EQUAL( 105, _world.getResource( "gold" ) );

    sw::ConquestWorld empty;
    empty.initialize( nullptr );
    empty.update( 1.0f );
    SW_EXPECT_EQUAL( 0, empty.computePopulationCap() );
    SW_EXPECT_EQUAL( 0, empty.computeWaveSize() );
    SW_EXPECT_EQUAL( -1, empty.spawnUnit( "spear", sw::ConquestTeam::Player, 0.0f, sw::ConquestOrder::Hold ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::UnknownDef, empty.placeBuilding( "mill", "home" ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::InvalidBuilding, empty.assignWorkers( 0, 1 ) );
    SW_EXPECT_EQUAL( sw::ConquestResult::InvalidBuilding, empty.trainUnit( 0, "spear" ) );

    sw::Archive written;
    _world.writeState( written );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( empty.readState( reading ) );
}

/**
 * @brief [ConquestRulesTest] 상태 왕복: 건물 · 대기열 · 병사 · 자원 · 지휘관 · 고정 걸음 · 타이머를 싣고, 읽은 판이 같은 입력에 같은 판을 잇는다. 이벤트는 비운다
 */
SW_TEST_CASE_F( ConquestRulesTest, StateRoundTrip )
{
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "mill", "home" ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.placeBuilding( "barracks", "home" ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.assignWorkers( 0, 2 ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.trainUnit( 1, "spear" ) );
    SW_REQUIRE_EQUAL( sw::ConquestResult::Ok, _world.trainUnit( 1, "spear" ) );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 40.0f, sw::ConquestOrder::Hold ) >= 1 );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Enemy, 40.5f ) >= 1 );
    SW_EXPECT_TRUE( spawn( "ladder", sw::ConquestTeam::Player, 94.0f, sw::ConquestOrder::Charge ) >= 1 );
    _world.setCommanderMove( 0.5f );
    run( 3 );
    _world.update( 0.25f ); // 고정 걸음에 못 미친 시간도 싣는다

    sw::Archive written;
    _world.writeState( written );
    sw::Archive       reading( written.getData(), written.getSize() );
    sw::ConquestWorld restored;
    restored.initialize( &_catalog );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( ConquestTestUtil::isSameState( _world, restored ) );
    SW_EXPECT_EQUAL( _world.getUnits().size(), restored.getUnits().size() );

    sw::ConsumerLease lease;
    SW_REQUIRE_TRUE( restored.claimEvents( lease ) );
    sw::vector<sw::ConquestEvent> listRestoredEvent;
    restored.pollEvents( lease, listRestoredEvent );
    SW_EXPECT_TRUE( listRestoredEvent.empty() );

    for ( int32 step = 0; step < 20; ++step )
    {
        _world.update( 0.5f );
        restored.update( 0.5f );
    }
    SW_EXPECT_TRUE( ConquestTestUtil::isSameState( _world, restored ) );
}

/**
 * @brief [ConquestRulesTest] 상태 왕복: 쓰러진 지휘관, 점령 중인 거점도 읽힌다
 */
SW_TEST_CASE_F( ConquestRulesTest, StateRoundTripWithFallenCommander )
{
    _world.setCommanderMove( 1.0f );
    run( 3 );
    _world.setCommanderMove( 0.0f );
    SW_EXPECT_TRUE( spawn( "giant", sw::ConquestTeam::Enemy, 6.5f ) >= 1 );
    SW_EXPECT_TRUE( spawn( "spear", sw::ConquestTeam::Player, 20.0f ) >= 1 );
    run( 1 );
    SW_EXPECT_EQUAL( SW_FALSE, _world.getCommander()._bAlive );
    SW_EXPECT_TRUE( _world.findSite( "outpost" )->_captureProgress > 0.0f );

    sw::Archive written;
    _world.writeState( written );
    sw::Archive       reading( written.getData(), written.getSize() );
    sw::ConquestWorld restored;
    restored.initialize( &_catalog );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( ConquestTestUtil::isSameState( _world, restored ) );
}

/**
 * @brief [ConquestRulesTest] 결함: 상태 바이트가 값을 거의 보지 않았다. 음수 · 최대를 넘는 성문 · 성벽 · 체력, 다른 자리의 거점, 슬롯을 넘는 건물 · 일꾼, 이 건물이 훈련하지 않는 대기열, 겹친 병사 번호(`findUnit` 이 다른 병사를 돌려준다),
 *        다음 번호 이상의 병사 번호, 중립 병사, 적의 부대 슬롯, 사다리가 아닌 걸침, 음수 자원, 범위 밖 지휘관 입력을 받았다. 거절하고 상태를 두는지 본다
 */
SW_TEST_CASE_F( ConquestRulesTest, StateRefusals )
{
    const sw::ConquestWorld snapshot = _world;
    const auto              refuses  = [this, &snapshot]( auto mutate )
    {
        ConquestForgedWorld forged;
        mutate( forged );
        return readForged( forged ) == false && ConquestTestUtil::isSameState( snapshot, _world );
    };

    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._firstSiteID = "keep"; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._firstSiteID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepGate = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepGate = 51.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepGate = std::numeric_limits<float32>::quiet_NaN(); } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepWall = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepWall = 31.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepCapture = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepCapture = 2.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepOwner = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._keepCaptureTeam = 9; } ) );

    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._buildingID = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._queueUnit = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._queueUnit = "spear"; } ) ); // 제재소는 훈련하지 않는다
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._cycle = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._train = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._siteIndex = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._siteIndex = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._siteIndex = 2; } ) ); // 적 거점의 건물에 일꾼이 있다
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._workers = -1; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._workers = 4; } ) ); // 슬롯 3
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._buildingCount = 3; } ) ); // 거점 슬롯 2
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    {
        forged._buildingCount = 2;
        forged._workers       = 3; // 일꾼 넷을 넘는다
    } ) );

    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._unitName = "nope"; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._unitHealth = 0.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._unitHealth = 41.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._unitID = 0; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._unitID = 2; } ) ); // 다음 번호와 겹친다
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._unitCount = 2; } ) ); // 겹친 번호
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._squadSlot = -2; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._team = 0; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._team = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._team = static_cast<uint8>( sw::ConquestTeam::Enemy ); } ) ); // 적의 부대 슬롯
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._order = 9; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._alive = 0; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._alive = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._planted = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._planted = 1; } ) ); // 창병은 사다리가 아니다

    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._gold = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._commanderHealth = 101.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._commanderHealth = 0.0f; } ) ); // 살아 있는데 체력 0
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._commanderAlive = 0; } ) ); // 쓰러졌는데 체력이 있다
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._commanderAlive = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._moveAxis = 2.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._facing = 0.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._accumulator = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._elapsed = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._incomeTimer = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._waveTimer = -1.0f; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._nextUnitID = 0; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._victory = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    { forged._defeat = 2; } ) );
    SW_EXPECT_TRUE( refuses( []( ConquestForgedWorld& forged )
    {
        forged._victory = 1;
        forged._defeat  = 1;
    } ) );

    // 맞는 상태는 읽힌다: 걸친 사다리, 훈련 건물의 대기열.
    ConquestForgedWorld valid;
    valid._buildingID = "barracks";
    valid._queueUnit  = "spear";
    valid._workers    = 0;
    valid._unitName   = "ladder";
    valid._unitHealth = 20.0f;
    valid._planted    = 1;
    SW_REQUIRE_TRUE( readForged( valid ) );
    SW_EXPECT_EQUAL( 1, _world.getResource( "gold" ) );
    SW_EXPECT_EQUAL( 1u, _world.getBuildings()[0]._listQueue.size() );
    SW_EXPECT_TRUE( _world.findUnit( 1 ) != nullptr );
}

/**
 * @brief [ConquestTest] 카탈로그: 규칙 · 비용 · 수입 · 주둔군 · 공성 역할을 읽고, `<Start>` 가 `<Rules>` 앞에 와도 된다
 */
SW_TEST_CASE( ConquestTest, CatalogReadsDefinitions )
{
    sw::ConquestCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ConquestCatalog>
  <Start gold="20" wood="50"/>
  <Rules waveUnit="spear" waveInterval="30" captureTime="2"/>
  <Site id="keep" x="60" kind="Fortress" owner="Enemy" gate="200" wall="300" garrison="spear:3,ram"><Income gold="2"/></Site>
  <Building id="mill" produces="wood" trains="spear,ram" workerSlots="3"><Cost gold="10"/></Building>
  <Unit id="spear" hp="40" damage="6"><Cost gold="10" iron="2"/></Unit>
  <Unit id="ram" hp="90" siege="Ram"/>
</ConquestCatalog>)",
                                              "ConquestTest" ) );
    const sw::ConquestRules& rules = catalog.getRules();
    SW_EXPECT_NEAR( 2.0, rules._captureTime, 1.0e-5 );
    SW_EXPECT_NEAR( 20.0, rules._startResources.getValue( "gold" ), 1.0e-5 );
    SW_EXPECT_TRUE( rules._waveUnit == sw::hashed_string( "spear" ) );

    const sw::ConquestSiteDef* pKeep = catalog.findSite( "keep" );
    SW_REQUIRE_TRUE( pKeep != nullptr );
    SW_EXPECT_EQUAL( sw::ConquestSiteKind::Fortress, pKeep->_kind );
    SW_EXPECT_EQUAL( sw::ConquestTeam::Enemy, pKeep->_owner );
    SW_REQUIRE_EQUAL( 2u, pKeep->_listGarrison.size() );
    SW_EXPECT_EQUAL( 3, pKeep->_listGarrison[0]._count );
    SW_EXPECT_EQUAL( 1, pKeep->_listGarrison[1]._count );
    SW_EXPECT_NEAR( 2.0, pKeep->_income.getValue( "gold" ), 1.0e-5 );

    SW_EXPECT_NEAR( 2.0, catalog.findUnit( "spear" )->_cost.getValue( "iron" ), 1.0e-5 );
    SW_EXPECT_EQUAL( sw::ConquestSiegeRole::Ram, catalog.findUnit( "ram" )->_siegeRole );
    SW_EXPECT_NEAR( 10.0, catalog.findBuilding( "mill" )->_cost.getValue( "gold" ), 1.0e-5 );
    SW_EXPECT_EQUAL( 2u, catalog.findBuilding( "mill" )->_listTrainable.size() );
    SW_EXPECT_EQUAL( 2u, catalog.getUnits().size() );
    SW_EXPECT_EQUAL( 1u, catalog.getBuildings().size() );
    SW_EXPECT_EQUAL( 1u, catalog.getSites().size() );
}

/**
 * @brief [ConquestTest] 결함: 겹친 id 가 앞 정의를 말없이 덮어썼다(주석은 "알리고 버린다"). 원소 하나를 넣지 않고 오류다
 */
SW_TEST_CASE( ConquestTest, DuplicateIDKeepsTheFirstDefinition )
{
    sw::ConquestCatalog catalog;
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ConquestCatalog>
  <Unit id="u" hp="10"/><Unit id="u" hp="20"/>
  <Building id="b" housing="1"/><Building id="b" housing="2"/>
  <Site id="s" x="1"/><Site id="s" x="2"/>
</ConquestCatalog>)",
                                              "ConquestTest" ) );
    SW_EXPECT_NEAR( 10.0, catalog.findUnit( "u" )->_health, 1.0e-5 );
    SW_EXPECT_EQUAL( 1, catalog.findBuilding( "b" )->_housing );
    SW_EXPECT_NEAR( 1.0, catalog.findSite( "s" )->_x, 1.0e-5 );
}

/**
 * @brief [ConquestTest] 결함: 비용 · 수입 · 시작 자원의 깨진 값을 건너뛰었다 - `gold="1O"` 는 공짜 건물, 음수 비용은 쓸수록 돈을 줬다. 중립 거점의 주둔군은 판이 만들지 않아 말없이 사라졌다.
 *        사거리 0.5 미만 병종은 성문 앞에서 영원히 때리지 못했다 - 범위 오류다. 그 원소를 넣지 않는다
 */
SW_TEST_CASE( ConquestTest, CatalogRefusesBrokenElements )
{
    sw::ConquestCatalog catalog;
    SW_EXPECT_LOG( "ConquestCatalog.ResourceNeedsAmount" );
    SW_EXPECT_LOG( "ConquestCatalog.SiteBadGarrisonCount" );
    SW_EXPECT_LOG( "ConquestCatalog.NeutralSiteGarrison" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ConquestCatalog>
  <Unit id="good"/>
  <Start gold="abc"/>
  <Unit id="typo"><Cost gold="1O"/></Unit>
  <Unit id="refund"><Cost gold="-5"/></Unit>
  <Building id="freeBuilding"><Cost wood="x"/></Building>
  <Site id="badIncome" owner="Player"><Income gold="y"/></Site>
  <Site id="badCount" owner="Enemy" garrison="good:x"/>
  <Site id="negativeCount" owner="Enemy" garrison="good:-1"/>
  <Site id="neutralCamp" garrison="good:1"/>
  <Dragon id="x"/>
  <Unit id="unknownAttribute" hpp="1"/>
  <Unit id="short" range="0.2"/>
  <Rules commanderRange="0.2"/>
</ConquestCatalog>)",
                                              "ConquestTest" ) );
    SW_EXPECT_TRUE( catalog.findUnit( "good" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findUnit( "typo" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findUnit( "refund" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findUnit( "unknownAttribute" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findUnit( "short" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findBuilding( "freeBuilding" ) == nullptr );
    SW_EXPECT_TRUE( catalog.getSites().empty() );
}

/**
 * @brief [ConquestTest] 서로 가리키는 id: 모르는 병종을 읽은 뒤 알린다. 새 판의 모르는 주둔군은 만들지 않는다
 */
SW_TEST_CASE( ConquestTest, CatalogReportsUnknownReferences )
{
    sw::ConquestCatalog catalog;
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" ); // 반격 병종 · 훈련 병종 · 주둔 병종 셋 — 반격 병종도 같은 참조 표 한 줄이다
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<ConquestCatalog>
  <Rules waveUnit="nope"/>
  <Building id="b" trains="nope"/>
  <Site id="s" owner="Enemy" garrison="nope:2"/>
</ConquestCatalog>)",
                                              "ConquestTest" ) );
    sw::ConquestWorld world;
    world.initialize( &catalog );
    SW_EXPECT_TRUE( world.getUnits().empty() );
}
