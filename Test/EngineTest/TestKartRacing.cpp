#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Casual/KartRacing/KartAi.h"
#include "GameFramework/Kits/Casual/KartRacing/KartGhost.h"
#include "GameFramework/Kits/Casual/KartRacing/KartItems.h"
#include "GameFramework/Kits/Casual/KartRacing/KartRace.h"
#include "GameFramework/Kits/Casual/KartRacing/KartTrack.h"
#include "GameFramework/Utility/GameRandom.h"

#include "TestFramework/TestFramework.h"

// 카트 레이싱 키트 — Catmull-Rom 트랙 · 오프로드, 체크포인트를 모두 지나야 한 바퀴(지름길 · 결승선 뒤로 넘기), 역주행, 진행 순위, 순위 가중 아이템 표,
// 아이템 효과(껍질 · 유도 · 방어막 · 1 등 공격 · 바나나), AI 완주 · 러버밴딩 · 결정성, 고스트 재생이 같은 경로인지 본다.

using namespace sw;

namespace
{
    /** @brief 둥근 직사각형 회로 — (0,0) 에서 +Z 로 출발해 오른쪽으로 돈다. 첫 직선에 부스트 패드 · 아이템 상자가 있다. */
    const utf8* const kTrackXml = R"(<KartTrackCatalog>
  <Track id="oval" name="Oval" width="12" laps="3" offroadScale="0.5" samples="16">
    <Point x="0" z="0"/><Point x="0" z="100"/><Point x="30" z="130"/><Point x="90" z="130"/>
    <Point x="120" z="100"/><Point x="120" z="0"/><Point x="90" z="-30"/><Point x="30" z="-30"/>
    <Checkpoint at="0.5"/><Checkpoint at="0.25"/><Checkpoint at="0.75"/>
    <ItemBox at="0.12" offset="0"/>
    <BoostPad at="0.05" offset="0" length="6" width="10" duration="1"/>
    <Offroad from="0.3" to="0.35" minOffset="-6" maxOffset="0" scale="0.3"/>
  </Track>
  <Track id="broken" width="12"><Point x="0" z="0"/><Point x="0" z="10"/></Track>
</KartTrackCatalog>)";

    const utf8* const kKartRacingItemXml = R"(<KartItemCatalog places="8">
  <Item id="banana" kind="Banana" lifetime="0" spin="1.0"/>
  <Item id="green" kind="GreenShell" speed="45" spin="1.0"/>
  <Item id="red" kind="RedShell" speed="25" turnRate="10" spin="1.0"/>
  <Item id="mushroom" kind="Booster" boost="1.5"/>
  <Item id="shield" kind="Shield" shield="8"/>
  <Item id="blue" kind="LeaderShell" speed="60" turnRate="20" blastRadius="9" ignoresShield="1" spin="1.5"/>
  <RankTable from="1" to="1"><Entry item="banana" weight="70"/><Entry item="green" weight="30"/></RankTable>
  <RankTable from="2" to="4"><Entry item="green" weight="40"/><Entry item="red" weight="40"/><Entry item="shield" weight="20"/></RankTable>
  <RankTable from="5" to="8"><Entry item="red" weight="40"/><Entry item="mushroom" weight="40"/><Entry item="blue" weight="20"/></RankTable>
</KartItemCatalog>)";

    constexpr float32 kKartRacingStep = 1.0f / 60.0f;

    bool loadTestTrack( KartTrack& outTrack, int32 lapCount )
    {
        KartTrackCatalog catalog;
        if ( catalog.loadFromXmlText( kTrackXml, "test" ) == false || catalog.findTrack( "oval" ) == nullptr )
            return false;
        KartTrackDef def = *catalog.findTrack( "oval" );
        def._lapCount    = lapCount;
        return outTrack.initialize( def );
    }

    /** @brief 거리 · 옆 비킴의 자리와 그 자리의 진행 방향(요)입니다. */
    float3 makeTrackPoint( const KartTrack& track, float32 distance, float32 offset, float32& outYaw )
    {
        const KartTrackFrame frame = track.sample( distance );
        outYaw                     = MathUtil::atan2( frame._tangent._x, frame._tangent._z );
        return float3{ frame._position._x + frame._right._x * offset, frame._position._y, frame._position._z + frame._right._z * offset };
    }

    void placeOnTrack( KartRace& race, int32 racer, float32 distance, float32 offset )
    {
        float32      yaw      = 0.0f;
        const float3 position = makeTrackPoint( *race.getTrack(), distance, offset, yaw );
        race.placeRacer( racer, position, yaw );
    }

    KartRacerInput makeThrottle( float32 throttle )
    {
        KartRacerInput input;
        input._vehicle._throttle = throttle;
        return input;
    }

    void runSteps( KartRace& race, int32 stepCount )
    {
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            race.step();
    }

    const KartRaceEvent* findEvent( const vector<KartRaceEvent>& listEvent, KartRaceEvent::Kind kind, int32 racer = -1 )
    {
        for ( const KartRaceEvent& event : listEvent )
        {
            if ( event._kind == kind && ( racer < 0 || event._racer == racer ) )
                return &event;
        }
        return nullptr;
    }

    int32 countEvents( const vector<KartRaceEvent>& listEvent, KartRaceEvent::Kind kind )
    {
        int32 count = 0;
        for ( const KartRaceEvent& event : listEvent )
        {
            if ( event._kind == kind )
                ++count;
        }
        return count;
    }

    /** @brief 사람 차를 AI 운전수의 입력으로 몰고(고스트 시험), 이벤트를 모읍니다. 차가 들어오면 멈춥니다. */
    void driveHumanWithAi( KartRace& race, int32 racer, KartAiDriver& driver, int32 maxSteps, vector<KartRaceEvent>& outListEvent )
    {
        for ( int32 stepIndex = 0; stepIndex < maxSteps; ++stepIndex )
        {
            const KartRacer* pKart = race.findRacer( racer );
            if ( pKart == nullptr || pKart->_bFinished == SW_TRUE )
                break;
            KartRacerInput input;
            input._vehicle = driver.computeInput( *race.getTrack(), pKart->_motor, pKart->_distance );
            race.setInput( racer, input );
            race.step();
            vector<KartRaceEvent> listEvent;
            race.drainEvents( listEvent );
            outListEvent.insert( outListEvent.end(), listEvent.begin(), listEvent.end() );
        }
    }

    KartRaceSettings makeQuietSettings()
    {
        KartRaceSettings settings;
        settings._countdownTime = 0.0f;
        settings._bItems        = SW_FALSE;
        return settings;
    }
} // namespace

SW_TEST_CASE( KartRacingTest, TrackSplineGatesAndOffroad )
{
    KartTrackCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kTrackXml, "test" ) );
    SW_EXPECT_TRUE( catalog.findTrack( "broken" ) == nullptr ); // 점 둘은 건너뛴다

    KartTrack track;
    SW_ASSERT_TRUE( loadTestTrack( track, 3 ) );
    KartTrackDef tooFew;
    tooFew._listControlPoint.push_back( float3{ 0.0f, 0.0f, 0.0f } );
    KartTrack broken;
    SW_EXPECT_FALSE( broken.initialize( tooFew ) );

    // 닫힌 곡선 — 조절점을 지나고, 길이는 조절점 다각형 둘레(약 490 m) 근처.
    SW_EXPECT_TRUE( track.getLength() > 480.0f && track.getLength() < 530.0f );
    const KartTrackFrame start = track.sample( 0.0f );
    SW_EXPECT_NEAR_EQUAL( start._position._x, 0.0f, 0.001f );
    SW_EXPECT_TRUE( start._tangent._z > 0.95f ); // +Z 쪽으로 출발
    const KartTrackFrame wrapped = track.sample( track.getLength() + 10.0f );
    SW_EXPECT_NEAR_EQUAL( wrapped._position._z, track.sample( 10.0f )._position._z, 0.001f );
    SW_EXPECT_NEAR_EQUAL( track.project( float3{ 0.0f, 0.0f, 100.0f } )._position._z, 100.0f, 0.5f );

    // 체크포인트는 거리 순으로 정리된다. 문은 중심선에 직각.
    SW_EXPECT_EQUAL( track.getCheckpointCount(), 3 );
    SW_EXPECT_TRUE( track.getGate( 1 )._distance < track.getGate( 2 )._distance && track.getGate( 2 )._distance < track.getGate( 3 )._distance );
    SW_EXPECT_EQUAL( track.computeGateCrossing( 0, float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 0.0f, 1.0f } ), 1 );
    SW_EXPECT_EQUAL( track.computeGateCrossing( 0, float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 0.0f, -1.0f } ), -1 );
    SW_EXPECT_EQUAL( track.computeGateCrossing( 0, float3{ 40.0f, 0.0f, -1.0f }, float3{ 40.0f, 0.0f, 1.0f } ), 0 ); // 문 밖(안쪽 풀밭)

    // 투영 · 오프로드 — 오른쪽 3 m 는 길, 9 m 는 길 밖, 오프로드 구간 왼쪽은 더 느리다.
    float32                   yaw     = 0.0f;
    const float3              onRoad  = makeTrackPoint( track, 50.0f, 3.0f, yaw );
    const float3              offRoad = makeTrackPoint( track, 50.0f, 9.0f, yaw );
    const KartTrackProjection side    = track.project( onRoad );
    SW_EXPECT_NEAR_EQUAL( side._offset, 3.0f, 0.05f );
    SW_EXPECT_NEAR_EQUAL( side._distance, 50.0f, 0.1f );
    SW_EXPECT_NEAR_EQUAL( track.sampleSpeedScale( onRoad._x, onRoad._z ), 1.0f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( track.sampleSpeedScale( offRoad._x, offRoad._z ), 0.5f, 0.001f );
    SW_EXPECT_TRUE( track.isOnRoad( onRoad ) && track.isOnRoad( offRoad ) == false );
    const float3 sandL = makeTrackPoint( track, track.getLength() * 0.32f, -3.0f, yaw );
    const float3 sandR = makeTrackPoint( track, track.getLength() * 0.32f, 3.0f, yaw );
    SW_EXPECT_NEAR_EQUAL( track.sampleSpeedScale( sandL._x, sandL._z ), 0.3f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( track.sampleSpeedScale( sandR._x, sandR._z ), 1.0f, 0.001f );
    SW_EXPECT_EQUAL( track.findBoostPadAt( makeTrackPoint( track, track.getLength() * 0.05f, 2.0f, yaw ) ), 0 );
    SW_EXPECT_EQUAL( track.findBoostPadAt( makeTrackPoint( track, track.getLength() * 0.05f, -6.0f, yaw ) ), -1 );
}

SW_TEST_CASE( KartRacingTest, LapNeedsEveryCheckpointInOrder )
{
    KartTrack track;
    SW_ASSERT_TRUE( loadTestTrack( track, 3 ) );
    KartRace race;
    race.initialize( makeQuietSettings(), &track, nullptr );
    SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, false ) == 0 );
    race.start();
    SW_EXPECT_TRUE( race.getPhase() == KartRacePhase::Racing );
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_lap, 0 ); // 격자는 결승선 뒤

    // 1) 출발선을 지나면 1 바퀴째.
    race.setInput( 0, makeThrottle( 1.0f ) );
    runSteps( race, 60 );
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_lap, 1 );
    vector<KartRaceEvent> listEvent;
    race.drainEvents( listEvent );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::LapStarted, 0 ) != nullptr );

    // 2) 지름길 — 결승선 뒤로 옮겨 다시 지나도 체크포인트가 없으니 바퀴가 오르지 않는다(이 규칙을 끄면 2 바퀴가 된다).
    placeOnTrack( race, 0, -5.0f, 0.0f );
    runSteps( race, 60 );
    listEvent.clear();
    race.drainEvents( listEvent );
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_lap, 1 );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::CheckpointMissed, 0 ) != nullptr );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_listLapTime.empty() );

    // 3) 거꾸로 달려 결승선을 뒤로 넘으면 역주행 경고와 함께 바퀴가 내려간다.
    float32      yaw      = 0.0f;
    const float3 position = makeTrackPoint( track, 20.0f, 0.0f, yaw );
    race.placeRacer( 0, position, yaw + MathUtil::Pi );
    runSteps( race, 150 );
    listEvent.clear();
    race.drainEvents( listEvent );
    const KartRaceEvent* pWrongWay = findEvent( listEvent, KartRaceEvent::Kind::WrongWay, 0 );
    SW_ASSERT_NOT_NULL( pWrongWay );
    SW_EXPECT_EQUAL( pWrongWay->_value, 1 );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_bWrongWay == SW_TRUE );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::LapRevoked, 0 ) != nullptr );
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_lap, 0 );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_progress < 0.0f );

    // 4) 제대로 한 바퀴 — 체크포인트 셋을 차례로 지나 랩 타임이 남는다. 첫 직선의 아이템 상자도 깨진다.
    placeOnTrack( race, 0, -5.0f, 0.0f );
    KartAiDriver          driver;
    vector<KartRaceEvent> listDrive;
    for ( int32 stepIndex = 0; stepIndex < 60 * 60 && race.findRacer( 0 )->_lap < 2; ++stepIndex )
    {
        KartRacerInput input;
        input._vehicle = driver.computeInput( track, race.findRacer( 0 )->_motor, race.findRacer( 0 )->_distance );
        race.setInput( 0, input );
        race.step();
        listEvent.clear();
        race.drainEvents( listEvent );
        listDrive.insert( listDrive.end(), listEvent.begin(), listEvent.end() );
    }
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_lap, 2 );
    SW_EXPECT_EQUAL( countEvents( listDrive, KartRaceEvent::Kind::CheckpointPassed ), 3 );
    SW_EXPECT_EQUAL( static_cast<int32>( race.findRacer( 0 )->_listLapTime.size() ), 1 );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_bestLapTime > 5.0f );
    SW_EXPECT_NEAR_EQUAL( race.getBestLapTime(), race.findRacer( 0 )->_bestLapTime, 0.0001f );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_bWrongWay == SW_FALSE );
    SW_EXPECT_TRUE( findEvent( listDrive, KartRaceEvent::Kind::BoostPad, 0 ) != nullptr );
}

SW_TEST_CASE( KartRacingTest, PlacesUseLapAndClampedDistance )
{
    KartTrack track;
    SW_ASSERT_TRUE( loadTestTrack( track, 3 ) );
    KartRace race;
    race.initialize( makeQuietSettings(), &track, nullptr );
    SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, false ) == 0 );
    SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, false ) == 1 );
    race.start();
    SW_EXPECT_EQUAL( race.addRacer( ArcadeVehicleSettings{}, false ), -1 ); // 출발 뒤에는 더할 수 없다

    // 둘 다 출발선을 넘는다.
    race.setInput( 0, makeThrottle( 1.0f ) );
    race.setInput( 1, makeThrottle( 1.0f ) );
    runSteps( race, 60 );
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_lap, 1 );
    SW_EXPECT_EQUAL( race.findRacer( 1 )->_lap, 1 );

    // 0 번을 체크포인트 둘을 건너뛴 자리(0.6 바퀴)로 옮긴다 — 진행값은 다음 문(첫 체크포인트)에서 잘린다.
    const float32 length    = track.getLength();
    const float32 firstGate = track.getGate( 1 )._distance;
    placeOnTrack( race, 0, length * 0.6f, 0.0f );
    placeOnTrack( race, 1, firstGate - 5.0f, 0.0f );
    race.setInput( 0, makeThrottle( 0.0f ) );
    race.step();
    SW_EXPECT_NEAR_EQUAL( race.findRacer( 0 )->_progress, firstGate, 0.01f );
    SW_EXPECT_EQUAL( race.findRacer( 0 )->_place, 1 );

    // 1 번이 문을 제대로 지나면 몸은 뒤에 있어도 앞선다.
    vector<KartRaceEvent> listEvent;
    race.drainEvents( listEvent );
    runSteps( race, 60 );
    listEvent.clear();
    race.drainEvents( listEvent );
    SW_EXPECT_EQUAL( race.findRacer( 1 )->_nextCheckpoint, 1 );
    SW_EXPECT_EQUAL( race.findRacer( 1 )->_place, 1 );
    SW_EXPECT_EQUAL( race.findRacerAtPlace( 2 ), 0 );
    const KartRaceEvent* pChanged = findEvent( listEvent, KartRaceEvent::Kind::PlaceChanged, 1 );
    SW_ASSERT_NOT_NULL( pChanged );
    SW_EXPECT_EQUAL( pChanged->_value, 1 );
}

SW_TEST_CASE( KartRacingTest, ItemTablesFavorRacersBehind )
{
    KartItemCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kKartRacingItemXml, "test" ) );
    SW_EXPECT_EQUAL( static_cast<int32>( catalog.getItems().getCount() ), 6 );

    // 8 명 기준 표 — 1 등은 바나나 · 녹색, 꼴찌는 1 등 공격까지.
    SW_EXPECT_NEAR_EQUAL( catalog.computeItemChance( 1, 8, "blue" ), 0.0f, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( catalog.computeItemChance( 1, 8, "banana" ), 0.7f, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( catalog.computeItemChance( 8, 8, "blue" ), 0.2f, 0.0001f );

    // 4 명 경기 — 4 등은 8 명 표의 8 등으로 늘려 고른다(늘리지 않으면 2..4 등 표라 1 등 공격이 나오지 않는다).
    SW_EXPECT_NEAR_EQUAL( catalog.computeItemChance( 4, 4, "blue" ), 0.2f, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( catalog.computeItemChance( 1, 4, "blue" ), 0.0f, 0.0001f );
    const KartRankTable* pRank = catalog.findRankTable( 2, 4 );
    SW_ASSERT_NOT_NULL( pRank );
    SW_EXPECT_EQUAL( pRank->_fromPlace, 2 ); // 2 등 → 8 명 표의 3 등(2..4 표)

    // 씨앗이 같으면 같은 아이템 줄.
    GameRandom firstRandom( 1234u );
    GameRandom secondRandom( 1234u );
    int32      blueCount = 0;
    for ( int32 rollIndex = 0; rollIndex < 200; ++rollIndex )
    {
        const KartItemDef* pFirst  = catalog.rollItem( 4, 4, firstRandom );
        const KartItemDef* pSecond = catalog.rollItem( 4, 4, secondRandom );
        SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr );
        SW_EXPECT_TRUE( pFirst->_id == pSecond->_id );
        SW_EXPECT_TRUE( pFirst->_kind != KartItemKind::Banana ); // 꼴찌 표에 바나나는 없다
        if ( pFirst->_kind == KartItemKind::LeaderShell )
            ++blueCount;
    }
    SW_EXPECT_TRUE( 20 <= blueCount && blueCount <= 70 );
}

SW_TEST_CASE( KartRacingTest, ItemEffectsHitBlockAndHome )
{
    KartTrack track;
    SW_ASSERT_TRUE( loadTestTrack( track, 3 ) );
    KartItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXmlText( kKartRacingItemXml, "test" ) );
    KartRaceSettings settings = makeQuietSettings();
    settings._bItems          = SW_TRUE;
    KartRace race;
    race.initialize( settings, &track, &items );
    for ( int32 racer = 0; racer < 4; ++racer )
        SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, false ) == racer );
    race.start();
    race.step();
    vector<KartRaceEvent> listEvent;
    race.drainEvents( listEvent );

    // 1) 녹색 껍질 — 2 번(뒷줄 왼쪽)이 곧게 쏘면 앞의 0 번이 돈다. 도는 동안 페달이 듣지 않는다.
    race.giveItem( 2, "green" );
    SW_EXPECT_TRUE( race.useItem( 2 ) );
    SW_EXPECT_FALSE( race.useItem( 2 ) ); // 이제 빈손
    race.setInput( 0, makeThrottle( 1.0f ) );
    runSteps( race, 30 );
    listEvent.clear();
    race.drainEvents( listEvent );
    const KartRaceEvent* pHit = findEvent( listEvent, KartRaceEvent::Kind::Hit, 0 );
    SW_ASSERT_NOT_NULL( pHit );
    SW_EXPECT_EQUAL( pHit->_other, 2 );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_spinTime > 0.0f );
    SW_EXPECT_TRUE( race.findRacer( 0 )->_motor.getForwardSpeed() < 0.5f );
    race.setInput( 0, makeThrottle( 0.0f ) );
    runSteps( race, 60 );
    SW_EXPECT_NEAR_EQUAL( race.findRacer( 0 )->_spinTime, 0.0f, 0.0001f );

    // 2) 방어막은 한 번을 막는다.
    placeOnTrack( race, 0, -6.0f, -3.0f );
    placeOnTrack( race, 2, -12.0f, -3.0f );
    race.giveItem( 0, "shield" );
    SW_EXPECT_TRUE( race.useItem( 0 ) );
    race.giveItem( 2, "green" );
    SW_EXPECT_TRUE( race.useItem( 2 ) );
    runSteps( race, 30 );
    listEvent.clear();
    race.drainEvents( listEvent );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::ShieldBlocked, 0 ) != nullptr );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::Hit, 0 ) == nullptr );
    SW_EXPECT_NEAR_EQUAL( race.findRacer( 0 )->_shieldTime, 0.0f, 0.0001f );

    // 3) 빨간 껍질 — 바로 앞 순위(옆 앞의 2 번)를 잡아 돌아 들어간다.
    placeOnTrack( race, 0, -1.0f, -4.0f );
    placeOnTrack( race, 1, -1.0f, 4.0f );
    placeOnTrack( race, 2, -6.0f, -3.0f );
    placeOnTrack( race, 3, -12.0f, 3.0f );
    race.step();
    listEvent.clear();
    race.drainEvents( listEvent );
    SW_EXPECT_EQUAL( race.findRacer( 3 )->_place, 4 );
    SW_EXPECT_EQUAL( race.findRacerAtPlace( 3 ), 2 );
    race.giveItem( 3, "red" );
    SW_EXPECT_TRUE( race.useItem( 3 ) );
    SW_ASSERT_TRUE( race.getProjectiles().size() == 1 );
    SW_EXPECT_EQUAL( race.getProjectiles()[0]._target, 2 );
    runSteps( race, 60 );
    listEvent.clear();
    race.drainEvents( listEvent );
    pHit = findEvent( listEvent, KartRaceEvent::Kind::Hit );
    SW_ASSERT_NOT_NULL( pHit );
    SW_EXPECT_EQUAL( pHit->_racer, 2 );
    SW_EXPECT_EQUAL( pHit->_other, 3 );

    // 4) 1 등 공격 — 방어막을 뚫고 1 등과 그 둘레를 맞힌다. 멀리 있는 차는 멀쩡하다.
    runSteps( race, 60 );
    placeOnTrack( race, 0, -1.0f, -4.0f );
    placeOnTrack( race, 1, -1.0f, 4.0f );
    placeOnTrack( race, 2, -6.0f, -3.0f );
    placeOnTrack( race, 3, -40.0f, 0.0f );
    race.step();
    race.giveItem( 0, "shield" );
    SW_EXPECT_TRUE( race.useItem( 0 ) );
    listEvent.clear();
    race.drainEvents( listEvent );
    race.giveItem( 2, "blue" );
    SW_EXPECT_TRUE( race.useItem( 2 ) );
    runSteps( race, 60 );
    listEvent.clear();
    race.drainEvents( listEvent );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::Hit, 0 ) != nullptr );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::Hit, 1 ) != nullptr );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::Hit, 3 ) == nullptr );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::ShieldBlocked, 0 ) == nullptr );

    // 5) 바나나 — 뒤에 놓이고, 달려온 차가 밟는다.
    runSteps( race, 120 );
    placeOnTrack( race, 0, -5.0f, 0.0f );
    placeOnTrack( race, 1, -1.0f, 5.0f );
    placeOnTrack( race, 3, -14.0f, 0.0f );
    race.giveItem( 0, "banana" );
    SW_EXPECT_TRUE( race.useItem( 0 ) );
    race.setInput( 3, makeThrottle( 1.0f ) );
    runSteps( race, 120 );
    listEvent.clear();
    race.drainEvents( listEvent );
    pHit = findEvent( listEvent, KartRaceEvent::Kind::Hit, 3 );
    SW_ASSERT_NOT_NULL( pHit );
    SW_EXPECT_EQUAL( pHit->_other, 0 );
    SW_EXPECT_TRUE( race.getProjectiles().empty() );
}

SW_TEST_CASE( KartRacingTest, AiFinishesDeterministicallyWithRubberBand )
{
    KartTrack track;
    SW_ASSERT_TRUE( loadTestTrack( track, 1 ) );
    KartItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXmlText( kKartRacingItemXml, "test" ) );

    // 1) AI 셋 — 한 바퀴를 모두 완주하고 순위가 1..3 으로 확정된다. 같은 씨앗이면 같은 기록.
    float32 arrFinishTime[2][3]{};
    for ( int32 runIndex = 0; runIndex < 2; ++runIndex )
    {
        KartRaceSettings settings;
        settings._countdownTime = 1.0f;
        settings._seed          = 77u;
        KartRace race;
        race.initialize( settings, &track, &items );
        for ( int32 racer = 0; racer < 3; ++racer )
            SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, true ) == racer );
        race.start();
        SW_EXPECT_TRUE( race.getPhase() == KartRacePhase::Countdown );
        for ( int32 stepIndex = 0; stepIndex < 60 * 120 && race.getPhase() != KartRacePhase::Ended; ++stepIndex )
            race.step();
        SW_ASSERT_TRUE( race.getPhase() == KartRacePhase::Ended );
        int32 placeMask = 0;
        for ( int32 racer = 0; racer < 3; ++racer )
        {
            const KartRacer* pKart = race.findRacer( racer );
            SW_EXPECT_TRUE( pKart->_finishTime > 10.0f );
            placeMask |= 1 << pKart->_finishPlace;
            arrFinishTime[runIndex][racer] = pKart->_finishTime;
        }
        SW_EXPECT_EQUAL( placeMask, 0b1110 );
        SW_EXPECT_TRUE( race.getBestLapTime() > 10.0f );
    }
    for ( int32 racer = 0; racer < 3; ++racer )
        SW_EXPECT_TRUE( arrFinishTime[0][racer] == arrFinishTime[1][racer] );

    // 2) 러버밴딩 — 1 등과 60 m 벌어진 AI 는 최고 속도가 오르고, 사람보다 앞선 AI 는 조금 내린다. 끄면 1.
    for ( int32 bandIndex = 0; bandIndex < 2; ++bandIndex )
    {
        KartRaceSettings settings = makeQuietSettings();
        settings._bRubberBand     = bandIndex == 0 ? SW_TRUE : SW_FALSE;
        KartRace race;
        race.initialize( settings, &track, nullptr );
        SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, false ) == 0 );
        SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, true ) == 1 );
        SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, true ) == 2 );
        race.start();
        placeOnTrack( race, 0, -1.0f, 0.0f );
        placeOnTrack( race, 1, -61.0f, 0.0f );
        placeOnTrack( race, 2, 0.0f - 0.5f, 3.0f );
        const float32 behindScale = race.computeRubberBandScale( 1 );
        SW_EXPECT_NEAR_EQUAL( race.computeRubberBandScale( 0 ), 1.0f, 0.0001f ); // 사람은 그대로
        if ( bandIndex == 0 )
        {
            SW_EXPECT_NEAR_EQUAL( behindScale, 1.0f + 0.15f * 60.0f / 120.0f, 0.01f );
            SW_EXPECT_TRUE( race.computeRubberBandScale( 2 ) < 1.0f );
            race.step();
            SW_EXPECT_NEAR_EQUAL( race.findRacer( 1 )->_speedScale, behindScale, 0.01f );
        }
        else
        {
            SW_EXPECT_NEAR_EQUAL( behindScale, 1.0f, 0.0001f );
            SW_EXPECT_NEAR_EQUAL( race.computeRubberBandScale( 2 ), 1.0f, 0.0001f );
        }
    }
}

SW_TEST_CASE( KartRacingTest, GhostReplaysTheSamePath )
{
    KartTrack track;
    SW_ASSERT_TRUE( loadTestTrack( track, 1 ) );
    KartRace race;
    race.initialize( makeQuietSettings(), &track, nullptr );
    SW_ASSERT_TRUE( race.addRacer( ArcadeVehicleSettings{}, false ) == 0 );
    race.start();
    KartGhost ghost;
    race.startGhostRecording( 0, &ghost );

    // 사람 차를 AI 운전수의 입력으로 몬다(드리프트 · 부스트 패드 포함) — 경기는 양자화한 입력으로 달리고 그것을 기록한다.
    KartAiDriver          driver;
    vector<KartRaceEvent> listEvent;
    driveHumanWithAi( race, 0, driver, 60 * 90, listEvent );
    const KartRacer* pKart = race.findRacer( 0 );
    SW_ASSERT_TRUE( pKart->_bFinished == SW_TRUE );
    SW_EXPECT_TRUE( findEvent( listEvent, KartRaceEvent::Kind::BoostPad, 0 ) != nullptr );
    SW_EXPECT_NEAR_EQUAL( ghost.getFinishTime(), pKart->_finishTime, 0.0001f );
    SW_EXPECT_EQUAL( ghost.getFrameCount(), static_cast<int32>( MathUtil::round( pKart->_finishTime / kKartRacingStep ) ) );
    const float3 finishPosition = pKart->_motor.getPosition();

    // 바이트로 오가도 같다. 잘린 기록은 읽지 않는다.
    vector<uint8> buffer;
    ghost.serialize( buffer );
    SW_EXPECT_TRUE( buffer.size() < static_cast<size_t>( ghost.getFrameCount() ) * 3u + 64u );
    KartGhost loaded;
    SW_ASSERT_TRUE( loaded.deserialize( buffer.data(), static_cast<int32>( buffer.size() ) ) );
    SW_EXPECT_EQUAL( loaded.getFrameCount(), ghost.getFrameCount() );
    KartGhost truncated;
    SW_EXPECT_FALSE( truncated.deserialize( buffer.data(), static_cast<int32>( buffer.size() / 2 ) ) );

    // 다시 돌리면 같은 자리에서 끝난다 — 부스트 패드 규칙까지 같이 돌아야 한다(트랙 없이 돌리면 어긋난다).
    KartGhostPlayer player;
    player.initialize( &loaded, ArcadeVehicleSettings{}, &track );
    while ( player.step() )
    {
    }
    SW_EXPECT_TRUE( player.isFinished() );
    SW_EXPECT_TRUE( player.getMotor().getPosition()._x == finishPosition._x );
    SW_EXPECT_TRUE( player.getMotor().getPosition()._z == finishPosition._z );

    KartGhostPlayer flatPlayer;
    flatPlayer.initialize( &loaded, ArcadeVehicleSettings{}, nullptr );
    while ( flatPlayer.step() )
    {
    }
    const float32 deltaX = flatPlayer.getMotor().getPosition()._x - finishPosition._x;
    const float32 deltaZ = flatPlayer.getMotor().getPosition()._z - finishPosition._z;
    SW_EXPECT_TRUE( deltaX * deltaX + deltaZ * deltaZ > 1.0f );
}
