#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSSelection.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include <cstring>

// RTSWorldTest — RTS 월드: 짜인 한 판(채취 · 생산 · 건설 · 무리 공격 이동 · 안개)의 동작 요약이 고정값과 같고,
// 중간에 상태를 쓰고 읽은 월드가 끊김 없이 돈 월드와 같은 요약을 낸다. 월드 안의 상태 묶음(시야 · 버킷 · 흐름장)을 옮길 때 동작이 그대로인지 묶는다. 키트 GF_RealTimeStrategy(nogpu).

namespace
{
    constexpr const utf8* kUnitXML = R"(<RTSCatalog supplyMax="200">
  <Unit id="minerals" name="minerals" kind="Resource" resource="Minerals" amount="1500" footprint="1"/>
  <Unit id="command_center" name="command_center" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" producedBy="worker" minerals="400" buildTime="75" sight="10"/>
  <Unit id="supply_depot" name="supply_depot" kind="Building" hp="500" armor="1" footprint="2" provides="8" producedBy="worker" minerals="100" buildTime="25" sight="6"/>
  <Unit id="worker" name="worker" hp="60" speed="2.8" radius="0.35" damage="5" range="0.2" cooldown="1.1" worker="true" producedBy="command_center" minerals="50" supply="1" buildTime="13" gatherTime="2.8" cargo="8" sight="7"/>
  <Unit id="marine" name="marine" hp="40" speed="2.25" radius="0.35" damage="6" range="4" cooldown="0.86" targets="Ground,Air" producedBy="command_center" minerals="50" supply="1" buildTime="18" sight="7"/>
</RTSCatalog>)";

    constexpr int32         kMapSize = 48;
    const sw::hashed_string kMinerals{ "minerals" };
    const sw::hashed_string kDepot{ "command_center" };
    const sw::hashed_string kSupply{ "supply_depot" };
    const sw::hashed_string kWorker{ "worker" };
    const sw::hashed_string kMarine{ "marine" };

    /** @brief 짜인 한 판입니다. 두 플레이어가 대칭으로 본진 · 일꾼 · 광물 · 해병을 갖는다. */
    struct ScriptedMatch
    {
        sw::RTSWorld              _world;
        sw::Wallet                _arrWallet[2];
        sw::vector<sw::RTSUnitID> _arrArmy[2];
        sw::vector<sw::RTSUnitID> _arrWorker[2];
        sw::RTSUnitID             _arrDepot[2];

        bool setUp( const sw::RTSCatalog& catalog )
        {
            if ( _world.initialize( &catalog, kMapSize, kMapSize, sw::RTSSettings{} ) == false )
                return false;
            for ( int32 y = 20; y < 28; ++y )
                _world.setTerrainBlocked( 24, y, true ); // 가운데 벽 — 무리 이동이 돌아간다
            for ( int32 player = 0; player < 2; ++player )
            {
                _arrWallet[player].add( _world.getSettings()._mineralCurrency, 600 );
                (void)_world.addPlayer( player, &_arrWallet[player], sw::float3{} );
            }
            for ( int32 player = 0; player < 2; ++player )
            {
                const auto at = [player]( float32 x, float32 z )
                { return player == 0 ? sw::float3{ x, 0.0f, z } : sw::float3{ kMapSize - x, 0.0f, kMapSize - z }; };
                _arrDepot[player] = _world.spawnUnit( kDepot, player, at( 4.5f, 4.5f ) );
                for ( int32 index = 0; index < 4; ++index )
                    (void)_world.spawnUnit( kMinerals, sw::RTSWorld::kNoOwner, at( 12.5f, 3.5f + static_cast<float32>( index ) ) );
                for ( int32 index = 0; index < 4; ++index )
                    _arrWorker[player].push_back( _world.spawnUnit( kWorker, player, at( 9.5f, 3.5f + static_cast<float32>( index ) ) ) );
                for ( int32 index = 0; index < 8; ++index )
                    _arrArmy[player].push_back( _world.spawnUnit( kMarine, player, at( 6.5f + static_cast<float32>( index % 4 ), 10.5f + static_cast<float32>( index / 4 ) ) ) );
            }
            return _arrDepot[0].isValid() && _arrDepot[1].isValid();
        }

        /** @brief 정해진 시각에 명령을 내리며 @p seconds 를 진행합니다. @p startSecond 는 이 구간이 시작하는 판 시각(초)입니다. */
        void run( int32 startSecond, int32 seconds )
        {
            for ( int32 second = startSecond; second < startSecond + seconds; ++second )
            {
                for ( int32 player = 0; player < 2; ++player )
                {
                    if ( second == 0 )
                    {
                        const sw::RTSUnitID mineral = _world.findNearestResource( _world.findUnit( _arrWorker[player][0] )->_position, sw::RTSResourceType::Minerals, 20.0f );
                        for ( size_t index = 1; index < _arrWorker[player].size(); ++index )
                            (void)_world.issueGather( _arrWorker[player][index], mineral );
                        (void)_world.train( _arrDepot[player], kWorker );
                        (void)_world.train( _arrDepot[player], kMarine );
                        const sw::int2 site = player == 0 ? sw::int2{ 3, 12 } : sw::int2{ kMapSize - 5, kMapSize - 14 };
                        (void)_world.issueBuild( _arrWorker[player][0], kSupply, site );
                    }
                    if ( second == 4 )
                        (void)_world.issueGroupMove( _arrArmy[player], sw::float3{ kMapSize * 0.5f, 0.0f, kMapSize * 0.5f }, true );
                    if ( second == 20 )
                        (void)_world.issueGroupMove( _arrArmy[player], _world.findUnit( _arrDepot[1 - player] )->_position, true, true );
                }
                for ( int32 tick = 0; tick < 30; ++tick )
                    _world.update( 1.0f / 30.0f );
            }
        }
    };

    struct Digest
    {
        uint64 _value{ 14695981039346656037ull };

        void addBytes( const void* pData, size_t size )
        {
            const uint8* pByte = static_cast<const uint8*>( pData );
            for ( size_t index = 0; index < size; ++index )
            {
                _value ^= pByte[index];
                _value *= 1099511628211ull;
            }
        }
        template <typename T>
        void add( const T& value )
        {
            addBytes( &value, sizeof( value ) );
        }
    };

    /** @brief 공개 조회로만 본 판의 요약입니다. 저장 형식과 상관없어 내부 배치를 옮겨도 그대로여야 한다. */
    uint64 digestMatch( const ScriptedMatch& match )
    {
        Digest              digest;
        const sw::RTSWorld& world = match._world;
        digest.add( world.getTime() );
        digest.add( world.getWinningTeam() );
        for ( int32 player = 0; player < world.getPlayerCount(); ++player )
        {
            digest.add( world.getMinerals( player ) );
            digest.add( world.findPlayer( player )->_supplyUsed );
            digest.add( world.findPlayer( player )->_supplyCap );
            for ( int32 y = 0; y < kMapSize; ++y )
            {
                for ( int32 x = 0; x < kMapSize; ++x )
                    digest.add( world.getVisibility( player, sw::int2{ x, y } ) );
            }
        }
        world.forEachUnit(
            [&]( const sw::RTSUnit& unit )
        {
            digest.add( unit._id.packed() );
            digest.add( unit._position._x );
            digest.add( unit._position._z );
            digest.add( unit._hp );
            digest.add( unit._buildProgress );
            digest.add( unit._resourceLeft );
            digest.add( unit._cargoAmount );
            digest.add( static_cast<uint32>( unit._listOrder.size() ) );
            digest.add( static_cast<uint32>( unit._listProduction.size() ) );
            digest.add( world.isVisibleTo( 0, unit._id ) );
            digest.add( world.isVisibleTo( 1, unit._id ) );
        } );
        sw::vector<sw::RTSUnitID> listNear;
        world.queryUnits( sw::float3{ kMapSize * 0.5f, 0.0f, kMapSize * 0.5f }, 10.0f, listNear );
        for ( const sw::RTSUnitID& unitID : listNear )
            digest.add( unitID.packed() );
        return digest._value;
    }

    /** @brief `RTSWorld::writeState` 의 형식으로 작은 판(8x8, 플레이어 하나, 슬롯 하나 또는 둘)을 꾸밉니다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨립니다. */
    struct RTSFixture
    {
        static constexpr int32 kSize = 8;

        sw::hashed_string             _defID{ "marine" };
        sw::vector<sw::hashed_string> _listProduction{};
        sw::hashed_string             _orderBuildID{};
        sw::vector<uint32>            _listFreeSlot{};
        sw::int2                      _cell{ -1, -1 };
        uint64                        _idPacked{ sw::RTSUnitID::make( 0, 1 ).packed() };
        uint32                        _generation{ 1 };
        float32                       _hp{ 40.0f };
        float32                       _buildProgress{ 1.0f };
        float32                       _gatherTimer{ 0.0f };
        float32                       _arriveRadius{ 0.0f };
        float32                       _nextUnderAttackTime{ 0.0f };
        float32                       _time{ 0.0f };
        int32                         _owner{ 0 };
        int32                         _resourceLeft{ 0 };
        int32                         _cargoAmount{ 0 };
        int32                         _supplyReserved{ 0 };
        int32                         _team{ 0 };
        int32                         _supplyUsed{ 1 };
        int32                         _winningTeam{ -1 };
        uint8                         _terrain{ SW_FALSE };
        uint8                         _visibility{ 0 };
        uint8                         _bAlive{ SW_TRUE };
        uint8                         _bFlowField{ SW_FALSE };
        uint8                         _bWithOrder{ SW_FALSE };
        uint8                         _bDeadSecondSlot{ SW_FALSE };

        static void writeDeadSlot( sw::Archive& outArchive )
        {
            outArchive << static_cast<uint32>( 1 );
            sw::StateIO::writeName( outArchive, sw::hashed_string{} );
            outArchive << static_cast<uint32>( 0 );
            outArchive << static_cast<uint32>( 0 );
            writeUnitProperties( outArchive, sw::int2{ -1, -1 }, 0, 0.0f, 1.0f, 0.0f, -1, 0, 0, 0, SW_FALSE );
        }

        static void writeUnitProperties( sw::Archive& outArchive, const sw::int2& cell, uint64 idPacked, float32 hp, float32 buildProgress, float32 gatherTimer, int32 owner, int32 resourceLeft, int32 cargoAmount, int32 supplyReserved, uint8 bAlive )
        {
            outArchive << sw::float3{ 1.5f, 0.0f, 1.5f };
            outArchive << sw::float3{};
            outArchive << sw::float3{};
            sw::StateIO::writeInt2( outArchive, cell );
            outArchive << idPacked;
            for ( int32 linked = 0; linked < 5; ++linked )
            {
                outArchive << static_cast<uint64>( 0 );
            }
            outArchive << hp;
            outArchive << 0.0f;
            outArchive << buildProgress;
            outArchive << 0.0f;
            outArchive << gatherTimer;
            outArchive << owner;
            outArchive << resourceLeft;
            outArchive << cargoAmount;
            outArchive << supplyReserved;
            outArchive << static_cast<uint8>( sw::RTSResourceType::None );
            outArchive << static_cast<uint8>( sw::RTSGatherPhase::ToResource );
            outArchive << bAlive;
            outArchive << static_cast<uint8>( SW_FALSE );
            outArchive << static_cast<uint8>( SW_FALSE );
        }

        bool readInto( sw::RTSWorld& inoutWorld ) const
        {
            sw::Archive written;
            written << kSize;
            written << kSize;
            written << sw::vector<uint8>( kSize * kSize, _terrain );
            written << static_cast<uint32>( _bDeadSecondSlot != SW_FALSE ? 2 : 1 );
            written << _generation;
            sw::StateIO::writeName( written, _defID );
            written << static_cast<uint32>( _bWithOrder != SW_FALSE ? 1 : 0 );
            if ( _bWithOrder != SW_FALSE )
            {
                sw::StateIO::writeName( written, _orderBuildID );
                written << sw::float3{ 2.5f, 0.0f, 2.5f };
                sw::StateIO::writeInt2( written, sw::int2{ -1, -1 } );
                written << static_cast<uint64>( 0 );
                written << _arriveRadius;
                written << static_cast<uint8>( sw::RTSOrderType::Move );
                written << _bFlowField;
            }
            written << static_cast<uint32>( _listProduction.size() );
            for ( const sw::hashed_string& productionID : _listProduction )
            {
                sw::StateIO::writeName( written, productionID );
            }
            writeUnitProperties( written, _cell, _idPacked, _hp, _buildProgress, _gatherTimer, _owner, _resourceLeft, _cargoAmount, _supplyReserved, _bAlive );
            if ( _bDeadSecondSlot != SW_FALSE )
                writeDeadSlot( written );
            written << static_cast<uint32>( _listFreeSlot.size() );
            for ( const uint32 slot : _listFreeSlot )
            {
                written << slot;
            }
            written << static_cast<uint32>( 1 );
            written << sw::float3{};
            written << _supplyUsed;
            written << static_cast<int32>( 0 );
            written << _team;
            written << _nextUnderAttackTime;
            written << static_cast<uint8>( SW_FALSE );
            written << static_cast<uint8>( SW_FALSE );
            written << static_cast<uint32>( 1 );
            written << sw::vector<uint8>( kSize * kSize, _visibility );
            written << 0.0f;
            sw::StateIO::writeStepTimer( written, sw::FixedStepTimer{ 0.05f, 0.25f } );
            written << _time;
            written << _winningTeam;
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutWorld.readState( reading );
        }
    };
} // namespace

/**
 * @brief [RTSWorldTest] 짜인 한 판의 동작 요약(시간 · 자원 · 보급 · 안개 · 유닛 위치 · 체력 · 명령 · 이웃 조회)이 고정값과 같다 — 월드 내부를 옮겨도 동작은 그대로다
 */
SW_TEST_CASE( RTSWorldTest, ScriptedMatchDigestIsPinned )
{
    sw::RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kUnitXML, "RTSWorldTest" ) );
    ScriptedMatch match;
    SW_REQUIRE_TRUE( match.setUp( catalog ) );
    match.run( 0, 40 );
    // 판이 실제로 돌았는지 먼저 본다 — 싸움(체력이 준 유닛) · 죽음 · 채취(지갑) · 안개(본 적 있는 칸)
    int32 damagedCount = 0;
    match._world.forEachUnit( [&]( const sw::RTSUnit& unit )
    { damagedCount += unit._hp < unit._pDef->_hp ? 1 : 0; } );
    int32 exploredCount = 0;
    for ( int32 y = 0; y < kMapSize; ++y )
    {
        for ( int32 x = 0; x < kMapSize; ++x )
            exploredCount += match._world.getVisibility( 0, sw::int2{ x, y } ) != sw::RTSVisibility::Unexplored ? 1 : 0;
    }
    SW_EXPECT_TRUE( damagedCount > 0 );
    SW_EXPECT_TRUE( match._world.getMinerals( 0 ) != 600 );
    SW_EXPECT_TRUE( exploredCount > 0 && exploredCount < kMapSize * kMapSize );
    // 고정값은 Release(MSVC · Clang 같은 빌드 설정)에서 잡았다. 동작을 일부러 바꾼 커밋만 값을 다시 적는다(마지막: 962e297a37 — 땅 유닛의 새 경로가 요청한 걸음 끝에 붙는다).
    SW_EXPECT_EQUAL( 8708104897650782380ull, digestMatch( match ) );
}

/**
 * @brief [RTSWorldTest] 20 초에 쓴 상태를 읽은 월드는 쓴 월드와 같은 요약을 내고, 같은 바이트를 읽은 두 월드는 이어 돌아도 같다(흐름장을 쓰던 무리 이동 포함)
 * @details 읽은 쪽은 경로를 다시 구하므로(`readState`) 끊김 없이 돈 판과 이어서 같을 필요는 없다 — 읽기 자체가 결정적인지만 묶는다.
 */
SW_TEST_CASE( RTSWorldTest, StateRoundTripContinuesTheSameMatch )
{
    sw::RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kUnitXML, "RTSWorldTest" ) );
    ScriptedMatch straight;
    SW_REQUIRE_TRUE( straight.setUp( catalog ) );
    straight.run( 0, 20 );

    sw::Archive written;
    straight._world.writeState( written );
    ScriptedMatch restored;
    SW_REQUIRE_TRUE( restored.setUp( catalog ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored._world.readState( reading ) );
    for ( int32 player = 0; player < 2; ++player )
    {
        sw::Archive walletWritten;
        straight._arrWallet[player].writeState( walletWritten );
        sw::Archive walletReading( walletWritten.getData(), walletWritten.getSize() );
        SW_REQUIRE_TRUE( restored._arrWallet[player].readState( walletReading ) );
    }
    SW_EXPECT_EQUAL( digestMatch( straight ), digestMatch( restored ) );

    ScriptedMatch again;
    SW_REQUIRE_TRUE( again.setUp( catalog ) );
    sw::Archive readingAgain( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( again._world.readState( readingAgain ) );
    for ( int32 player = 0; player < 2; ++player )
        again._arrWallet[player] = restored._arrWallet[player];
    restored.run( 20, 20 );
    again.run( 20, 20 );
    SW_EXPECT_EQUAL( digestMatch( restored ), digestMatch( again ) );
}

/**
 * @brief [RTSWorldTest] 결함: 유닛의 주인과 플레이어의 팀이 배열을 바로 찌르는 번호인데 범위를 보지 않아 꾸민 바이트 하나로 범위 밖을 읽고 썼다. 빈 슬롯 목록은 살아 있는 슬롯을 내주어 유닛을 덮어쓰게 했다. 거절하고 월드를 둔다
 */
SW_TEST_CASE( RTSWorldTest, ReadStateRefusesOutOfRangeIndices )
{
    sw::RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kUnitXML, "RTSWorldTest" ) );
    sw::RTSWorld world;
    SW_REQUIRE_TRUE( world.initialize( &catalog, RTSFixture::kSize, RTSFixture::kSize, sw::RTSSettings{} ) );
    sw::Wallet wallet;
    SW_REQUIRE_EQUAL( 0, world.addPlayer( 0, &wallet, sw::float3{} ) );

    const RTSFixture    valid;
    const sw::RTSUnitID unitID = sw::RTSUnitID::make( 0, 1 );
    SW_REQUIRE_TRUE( valid.readInto( world ) );
    SW_REQUIRE_TRUE( world.findUnit( unitID ) != nullptr );

    RTSFixture ownerOutside = valid;
    ownerOutside._owner     = 1;
    SW_EXPECT_FALSE( ownerOutside.readInto( world ) );

    RTSFixture ownerNegative = valid;
    ownerNegative._owner     = -2;
    SW_EXPECT_FALSE( ownerNegative.readInto( world ) );

    RTSFixture teamOutside = valid;
    teamOutside._team      = 1;
    SW_EXPECT_FALSE( teamOutside.readInto( world ) );

    RTSFixture teamNegative = valid;
    teamNegative._team      = -1;
    SW_EXPECT_FALSE( teamNegative.readInto( world ) );

    RTSFixture winnerOutside   = valid;
    winnerOutside._winningTeam = 1;
    SW_EXPECT_FALSE( winnerOutside.readInto( world ) );

    RTSFixture winnerNegative   = valid;
    winnerNegative._winningTeam = -2;
    SW_EXPECT_FALSE( winnerNegative.readInto( world ) );

    RTSFixture freeAlive    = valid;
    freeAlive._listFreeSlot = { 0 };
    SW_EXPECT_FALSE( freeAlive.readInto( world ) );

    RTSFixture freeOutside    = valid;
    freeOutside._listFreeSlot = { 1 };
    SW_EXPECT_FALSE( freeOutside.readInto( world ) );

    RTSFixture freeRepeated       = valid;
    freeRepeated._bDeadSecondSlot = SW_TRUE;
    freeRepeated._listFreeSlot    = { 1, 1 };
    SW_EXPECT_FALSE( freeRepeated.readInto( world ) );

    RTSFixture freeDead       = valid;
    freeDead._bDeadSecondSlot = SW_TRUE;
    freeDead._listFreeSlot    = { 1 };
    SW_EXPECT_TRUE( freeDead.readInto( world ) ); // 죽어 비운 슬롯은 내줄 수 있다

    RTSFixture idElsewhere = valid;
    idElsewhere._idPacked  = sw::RTSUnitID::make( 3, 1 ).packed();
    SW_EXPECT_FALSE( idElsewhere.readInto( world ) );

    RTSFixture idOldGeneration = valid;
    idOldGeneration._idPacked  = sw::RTSUnitID::make( 0, 2 ).packed();
    SW_EXPECT_FALSE( idOldGeneration.readInto( world ) );

    SW_EXPECT_TRUE( world.findUnit( unitID ) != nullptr );
    SW_EXPECT_NEAR( 40.0f, world.findUnit( unitID )->_hp, 1.0e-5f );
    SW_EXPECT_EQUAL( -1, world.getWinningTeam() );
}

/**
 * @brief [RTSWorldTest] 결함: 유닛과 명령의 값 범위를 보지 않았다. 최대를 넘거나 음수인 체력 · 진행 · 양, 격자 밖 건물, 없는 정의, 상한을 넘는 생산 줄, 깨진 안개 · 지형 · 표시, 음수 시각을 거절하고 월드를 둔다
 */
SW_TEST_CASE( RTSWorldTest, ReadStateRefusesOutOfRangeValues )
{
    sw::RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kUnitXML, "RTSWorldTest" ) );
    sw::RTSWorld world;
    SW_REQUIRE_TRUE( world.initialize( &catalog, RTSFixture::kSize, RTSFixture::kSize, sw::RTSSettings{} ) );
    sw::Wallet wallet;
    SW_REQUIRE_EQUAL( 0, world.addPlayer( 0, &wallet, sw::float3{} ) );

    RTSFixture valid;
    valid._bWithOrder     = SW_TRUE;
    valid._orderBuildID   = kSupply;
    valid._arriveRadius   = 1.0f;
    valid._listProduction = { kMarine };
    SW_REQUIRE_TRUE( valid.readInto( world ) );

    RTSFixture hpOver = valid;
    hpOver._hp        = 41.0f;
    SW_EXPECT_FALSE( hpOver.readInto( world ) );

    RTSFixture hpNegative = valid;
    hpNegative._hp        = -1.0f;
    SW_EXPECT_FALSE( hpNegative.readInto( world ) );

    RTSFixture progressOver     = valid;
    progressOver._buildProgress = 1.5f;
    SW_EXPECT_FALSE( progressOver.readInto( world ) );

    RTSFixture progressNegative     = valid;
    progressNegative._buildProgress = -0.5f;
    SW_EXPECT_FALSE( progressNegative.readInto( world ) );

    RTSFixture resourceNegative    = valid;
    resourceNegative._resourceLeft = -1;
    SW_EXPECT_FALSE( resourceNegative.readInto( world ) );

    RTSFixture cargoNegative   = valid;
    cargoNegative._cargoAmount = -1;
    SW_EXPECT_FALSE( cargoNegative.readInto( world ) );

    RTSFixture supplyNegative      = valid;
    supplyNegative._supplyReserved = -1;
    SW_EXPECT_FALSE( supplyNegative.readInto( world ) );

    RTSFixture gatherNegative   = valid;
    gatherNegative._gatherTimer = -1.0f;
    SW_EXPECT_FALSE( gatherNegative.readInto( world ) );

    RTSFixture buildingOutside = valid;
    buildingOutside._defID     = kSupply;
    buildingOutside._hp        = 100.0f;
    buildingOutside._cell      = sw::int2{ 99, 0 };
    SW_EXPECT_FALSE( buildingOutside.readInto( world ) );

    RTSFixture orderUnknown    = valid;
    orderUnknown._orderBuildID = "nothing";
    SW_EXPECT_FALSE( orderUnknown.readInto( world ) );

    RTSFixture orderRadius    = valid;
    orderRadius._arriveRadius = -1.0f;
    SW_EXPECT_FALSE( orderRadius.readInto( world ) );

    RTSFixture orderFlowField  = valid;
    orderFlowField._bFlowField = 2;
    SW_EXPECT_FALSE( orderFlowField.readInto( world ) );

    RTSFixture productionUnknown      = valid;
    productionUnknown._listProduction = { "nothing" };
    SW_EXPECT_FALSE( productionUnknown.readInto( world ) );

    RTSFixture productionOver      = valid;
    productionOver._listProduction = sw::vector<sw::hashed_string>( 6, kMarine ); // 설정의 상한은 5 이다
    SW_EXPECT_FALSE( productionOver.readInto( world ) );

    RTSFixture visibilityBroken  = valid;
    visibilityBroken._visibility = 3;
    SW_EXPECT_FALSE( visibilityBroken.readInto( world ) );

    RTSFixture terrainBroken = valid;
    terrainBroken._terrain   = 2;
    SW_EXPECT_FALSE( terrainBroken.readInto( world ) );

    RTSFixture supplyUsedNegative  = valid;
    supplyUsedNegative._supplyUsed = -1;
    SW_EXPECT_FALSE( supplyUsedNegative.readInto( world ) );

    RTSFixture timeNegative = valid;
    timeNegative._time      = -1.0f;
    SW_EXPECT_FALSE( timeNegative.readInto( world ) );

    RTSFixture attackTimeNegative           = valid;
    attackTimeNegative._nextUnderAttackTime = -1.0f;
    SW_EXPECT_FALSE( attackTimeNegative.readInto( world ) );

    const sw::RTSUnit* pUnit = world.findUnit( sw::RTSUnitID::make( 0, 1 ) );
    SW_REQUIRE_TRUE( pUnit != nullptr );
    SW_EXPECT_EQUAL( kMarine, pUnit->_pDef->_id );
    SW_EXPECT_EQUAL( size_t( 1 ), pUnit->_listProduction.size() );
}

/**
 * @brief [RTSWorldTest] 결함: 선택과 부대가 겹친 유닛과 상한을 넘는 선택을 그대로 받았다. 거절하고 선택을 둔다. 없는 유닛의 id 는 죽은 유닛의 id 와 같아 받는다
 */
SW_TEST_CASE( RTSWorldTest, SelectionStateRefusesRepeatedAndOverLimit )
{
    const auto writeLists = []( const sw::vector<uint64>& listSelected, const sw::vector<uint64>& listFirstGroup, sw::Archive& outArchive )
    {
        sw::StateIO::writeIDList( outArchive, listSelected );
        sw::StateIO::writeIDList( outArchive, listFirstGroup );
        for ( int32 group = 1; group < sw::RTSSelection::kGroupCount; ++group )
        {
            sw::StateIO::writeIDList( outArchive, sw::vector<uint64>{} );
        }
    };
    const uint64 first  = sw::RTSUnitID::make( 1, 1 ).packed();
    const uint64 second = sw::RTSUnitID::make( 2, 1 ).packed();
    const uint64 third  = sw::RTSUnitID::make( 3, 1 ).packed();

    sw::RTSSelection selection;
    selection.setMaxCount( 2 );

    sw::Archive writtenValid;
    writeLists( { first, second }, { third }, writtenValid );
    sw::Archive readingValid( writtenValid.getData(), writtenValid.getSize() );
    SW_REQUIRE_TRUE( selection.readState( readingValid ) );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelected().size() );

    sw::Archive writtenRepeated;
    writeLists( { third, third }, {}, writtenRepeated );
    sw::Archive readingRepeated( writtenRepeated.getData(), writtenRepeated.getSize() );
    SW_EXPECT_FALSE( selection.readState( readingRepeated ) );

    sw::Archive writtenGroupRepeated;
    writeLists( { third }, { first, first }, writtenGroupRepeated );
    sw::Archive readingGroupRepeated( writtenGroupRepeated.getData(), writtenGroupRepeated.getSize() );
    SW_EXPECT_FALSE( selection.readState( readingGroupRepeated ) );

    sw::Archive writtenOver;
    writeLists( { first, second, third }, {}, writtenOver );
    sw::Archive readingOver( writtenOver.getData(), writtenOver.getSize() );
    SW_EXPECT_FALSE( selection.readState( readingOver ) );

    SW_REQUIRE_EQUAL( size_t( 2 ), selection.getSelected().size() );
    SW_EXPECT_EQUAL( first, selection.getSelected()[0].packed() );
}
