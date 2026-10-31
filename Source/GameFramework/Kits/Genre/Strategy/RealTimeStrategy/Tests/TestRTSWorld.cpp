#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
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
    // 고정값은 Release(MSVC · Clang 같은 빌드 설정)에서 잡았다. 동작을 일부러 바꾼 커밋만 값을 다시 적는다.
    SW_EXPECT_EQUAL( 10950072093537724704ull, digestMatch( match ) );
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
