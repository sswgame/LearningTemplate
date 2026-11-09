#include "pch.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartAI.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartGhost.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartItems.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartRace.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartTrack.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"

#include <cstring>
#include <limits>

// 카트 키트의 규칙: 트랙(거리 · 투영 · 문 · 패드 · 오프로드), 순위별 아이템 표, AI 판단, 고스트(양자화 · 직렬화 · 재생 경로), 경기(바퀴 · 체크포인트 · 역주행 · 결승 · 아이템 · 러버밴딩), 상태 바이트의 범위 검사.
// 차는 실제 `KinematicVehicleMotor` 로 달린다 — 문 앞 1.5 m 에 세우고 페달을 밟아 지나게 한다. 키트 GF_KartRacing(nogpu).

namespace
{
    constexpr const utf8* kKartItemsXML = R"(<KartItemCatalog>
  <Item id="banana" kind="Banana" projectile="banana_peel" spin="1" hitSpeedScale="0.5"/>
  <Item id="green" kind="GreenShell" projectile="green_shell" spin="1"/>
  <Item id="red" kind="RedShell" projectile="red_shell"/>
  <Item id="blue" kind="LeaderShell" projectile="blue_shell" ignoresShield="true"/>
  <Item id="boost" kind="Booster" boost="2"/>
  <Item id="shield" kind="Shield" shield="5"/>
  <RankTable from="1" to="1"><Entry item="banana"/></RankTable>
  <RankTable from="2" to="4"><Entry item="green" weight="1"/><Entry item="red" weight="3"/></RankTable>
</KartItemCatalog>)";

    // 아이템이 가리키는 탄(운동은 탄, 반응은 아이템). 카트는 평면에서 돌아 중력이 없고 사거리 대신 수명으로 끝난다. 유도는 도/초다(10 rad/s ≈ 573°/s, 3 rad/s ≈ 172°/s).
    constexpr const utf8* kKartProjectileXML = R"(<ProjectileCatalog>
  <Projectile id="banana_peel" speed="0" lifetime="0" range="0" radius="1.2" gravityScale="0"/>
  <Projectile id="green_shell" speed="45" lifetime="1" range="0" radius="1.2" gravityScale="0"/>
  <Projectile id="red_shell" speed="45" lifetime="3" range="0" radius="1.2" homingRate="572.9578" gravityScale="0"/>
  <Projectile id="blue_shell" speed="45" lifetime="3" range="0" radius="1.2" homingRate="171.8873" blastRadius="4" gravityScale="0"/>
</ProjectileCatalog>)";

    constexpr float32 kKartTolerance = 1.0e-3f;

    /** @brief 트랙과 고스트 도우미입니다. */
    struct KartTestUtil
    {
        /** @brief 반지름 100 쯤의 고리입니다. 거리 0 은 (100, 0, 0) 이고 +Z 로 나가며 왼쪽으로 돈다. */
        static sw::KartTrackDef makeRingDef( int32 lapCount, const sw::vector<float32>& listCheckpoint )
        {
            sw::KartTrackDef def;
            def._id                = "ring";
            def._name              = "Ring";
            def._width             = 12.0f;
            def._lapCount          = lapCount;
            def._samplesPerSegment = 16;
            def._listCheckpoint    = listCheckpoint;
            def._listControlPoint.push_back( sw::float3{ 100.0f, 0.0f, 0.0f } );
            def._listControlPoint.push_back( sw::float3{ 0.0f, 0.0f, 100.0f } );
            def._listControlPoint.push_back( sw::float3{ -100.0f, 0.0f, 0.0f } );
            def._listControlPoint.push_back( sw::float3{ 0.0f, 0.0f, -100.0f } );
            return def;
        }

        /** @brief 진행 방향 오른쪽으로 @p offset 만큼 비킨 자리입니다. */
        static sw::float3 makeOffsetPoint( const sw::KartTrack& track, float32 distance, float32 offset )
        {
            const sw::KartTrackFrame frame = track.sample( distance );
            return sw::float3{ frame._position._x + frame._right._x * offset, frame._position._y, frame._position._z + frame._right._z * offset };
        }

        static float32 computeTrackYaw( const sw::KartTrack& track, float32 distance )
        {
            const sw::KartTrackFrame frame = track.sample( distance );
            return sw::MathUtil::atan2( frame._tangent._x, frame._tangent._z );
        }

        /** @brief 고스트 머리를 손으로 쓴 바이트입니다. */
        static sw::vector<uint8> writeGhostHead( uint32 magic, float32 step, float32 finishTime, uint64 frameCount )
        {
            sw::BitWriter writer;
            writer.writeUint32( magic );
            writer.writeBits( sw::KartGhost::kVersion, 8 );
            writer.writeFloat( 0.0f );
            writer.writeFloat( 0.0f );
            writer.writeFloat( 0.0f );
            writer.writeFloat( 0.0f );
            writer.writeFloat( step );
            writer.writeFloat( finishTime );
            writer.writeVarUint( frameCount );
            return writer.releaseBytes();
        }
    };

    /** @brief 고리 트랙 하나와 경기 하나입니다. 카운트다운 없이 바로 달립니다. */
    struct KartRaceTest
    {
        sw::KartTrack                 _track{};
        sw::KartItemCatalog           _items{};
        sw::ProjectileCatalog         _projectiles{};
        sw::KartRaceSettings          _settings{};
        sw::KartRace                  _race{};
        sw::vector<sw::KartRaceEvent> _listEvent{};

        KartRaceTest()
        {
            SW_REQUIRE_TRUE( _items.loadFromXMLText( kKartItemsXML, "KartTest" ) );
            SW_REQUIRE_TRUE( _projectiles.loadFromXMLText( kKartProjectileXML, "KartTest" ) );
            _settings._countdownTime = 0.0f;
            _settings._bRubberBand   = SW_FALSE;
        }

        /** @brief @p race 를 이 판의 트랙 · 카탈로그로 엽니다(@p pTrack 이 비면 이 판의 트랙). */
        void initializeRace( sw::KartRace& race, const sw::KartRaceSettings& settings, const sw::KartTrack* pTrack = nullptr )
        {
            SW_REQUIRE_TRUE( race.initialize( settings, pTrack != nullptr ? pTrack : &_track, &_items, &_projectiles ) );
        }

        void open( const sw::KartTrackDef& def, int32 racerCount, bool bAILast = false )
        {
            SW_REQUIRE_TRUE( _track.initialize( def ) );
            initializeRace( _race, _settings );
            for ( int32 index = 0; index < racerCount; ++index )
            {
                SW_EXPECT_EQUAL( index, _race.addRacer( sw::KinematicVehicleSettings{}, bAILast && index == racerCount - 1 ) );
            }
            _race.start();
        }

        int32 countEvents( sw::KartRaceEvent::Kind kind )
        {
            _race.pollEvents( _listEvent );
            int32 count = 0;
            for ( const sw::KartRaceEvent& event : _listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        const sw::KartRacer& racer( int32 index ) const { return *_race.findRacer( index ); }

        /** @brief 거리 @p distance 의 문을 지나게 합니다. 문 앞(뒤로 지나면 문 뒤)에 세우고 페달을 밟아 0.5 m 넘어갈 때까지 달립니다. */
        bool drive( int32 index, float32 distance, bool bForward )
        {
            const float32 start = distance + ( bForward ? -1.5f : 1.5f );
            const float32 yaw   = KartTestUtil::computeTrackYaw( _track, start ) + ( bForward ? 0.0f : sw::MathUtil::kPi );
            _race.placeRacer( index, _track.sample( start )._position, yaw );
            sw::KartRacerInput input;
            input._vehicle._throttle = 1.0f;
            for ( int32 stepIndex = 0; stepIndex < 240; ++stepIndex )
            {
                _race.setInput( index, input );
                _race.step();
                const float32 gap = _track.computeSignedGap( distance, racer( index )._distance );
                if ( bForward ? gap > 0.5f : gap < -0.5f )
                    return true;
            }
            return false;
        }

        bool driveGate( int32 index, int32 gate, bool bForward ) { return drive( index, _track.getGate( gate )._distance, bForward ); }

        /** @brief 차 @p owner 의 앞 @p ahead m 자리입니다. */
        sw::float3 pointAhead( int32 owner, float32 ahead ) const
        {
            const sw::float3  forward = racer( owner )._motor.computeForward();
            const sw::float3& here    = racer( owner )._motor.getPosition();
            return sw::float3{ here._x + forward._x * ahead, here._y, here._z + forward._z * ahead };
        }

        void stepTimes( int32 stepCount )
        {
            for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                _race.step();
            }
        }
    };
} // namespace

/**
 * @brief [KartTrackTest] 거리: 고리 길이, 음수 · 넘친 거리 감기, 부호 있는 간격(반 바퀴 안), 어느 쪽으로 도는지
 */
SW_TEST_CASE( KartTrackTest, DistanceMath )
{
    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );
    const float32 length = track.getLength();
    SW_EXPECT_TRUE( length > 500.0f && length < 800.0f );

    SW_EXPECT_NEAR( 10.0f, track.wrapDistance( 10.0f ), kKartTolerance );
    SW_EXPECT_NEAR( length - 10.0f, track.wrapDistance( -10.0f ), 0.01f );
    SW_EXPECT_NEAR( 10.0f, track.wrapDistance( length + 10.0f ), 0.01f );
    SW_EXPECT_NEAR( 0.0f, track.wrapDistance( length ), kKartTolerance );

    SW_EXPECT_NEAR( 20.0f, track.computeSignedGap( 10.0f, 30.0f ), 0.01f );
    SW_EXPECT_NEAR( -20.0f, track.computeSignedGap( 30.0f, 10.0f ), 0.01f );
    SW_EXPECT_NEAR( 20.0f, track.computeSignedGap( length - 10.0f, 10.0f ), 0.01f ); // 결승선을 넘는다

    const float32 turn = track.computeHeadingChange( 0.0f, 25.0f );
    SW_EXPECT_TRUE( turn < -0.1f && turn > -0.5f ); // 왼쪽으로 돈다
    SW_EXPECT_NEAR( 0.0f, track.computeHeadingChange( 40.0f, 0.0f ), kKartTolerance );
}

/**
 * @brief [KartTrackTest] 투영: 옆 비킴의 부호, 힌트 범위 안에서만 찾기, 범위가 크면 전체 찾기, 빈 트랙의 안전한 값
 */
SW_TEST_CASE( KartTrackTest, Projection )
{
    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );

    const sw::KartTrackProjection right = track.project( KartTestUtil::makeOffsetPoint( track, 150.0f, 3.0f ) );
    SW_EXPECT_NEAR( 150.0f, right._distance, 1.0f );
    SW_EXPECT_NEAR( 3.0f, right._offset, 0.1f );
    const sw::KartTrackProjection left = track.project( KartTestUtil::makeOffsetPoint( track, 150.0f, -3.0f ) );
    SW_EXPECT_NEAR( -3.0f, left._offset, 0.1f );
    SW_EXPECT_TRUE( track.isOnRoad( KartTestUtil::makeOffsetPoint( track, 150.0f, 5.5f ) ) );
    SW_EXPECT_FALSE( track.isOnRoad( KartTestUtil::makeOffsetPoint( track, 150.0f, 6.5f ) ) );

    const sw::float3 opposite = track.sample( 300.0f )._position;
    SW_EXPECT_NEAR( 300.0f, track.project( opposite, 290.0f, 30.0f )._distance, 1.0f );
    const float32 hinted = track.project( opposite, 0.0f, 20.0f )._distance; // 맞는 자리가 범위 밖이면 범위 안의 답이다
    SW_EXPECT_TRUE( sw::MathUtil::abs( track.computeSignedGap( 0.0f, hinted ) ) < 40.0f );
    SW_EXPECT_NEAR( 300.0f, track.project( opposite, 0.0f, track.getLength() )._distance, 1.0f ); // 범위가 크면 전체를 본다
    SW_EXPECT_NEAR( 300.0f, track.project( opposite, 0.0f, -1.0f )._distance, 1.0f );

    sw::KartTrack empty;
    SW_EXPECT_FALSE( empty.isValid() );
    SW_EXPECT_NEAR( 0.0f, empty.project( opposite )._distance, kKartTolerance );
    SW_EXPECT_NEAR( 0.0f, empty.wrapDistance( 50.0f ), kKartTolerance );
    SW_EXPECT_NEAR( 1.0f, empty.sampleSurface( 0.0f, 0.0f )._speedScale, kKartTolerance );
}

/**
 * @brief [KartTrackTest] 문: 앞으로 1, 뒤로 -1, 문 가장자리 밖이나 지나지 않은 이동과 없는 문은 0. 체크포인트는 거리 순으로 정리되고 (0, 1) 밖은 빠진다
 */
SW_TEST_CASE( KartTrackTest, GatesAndCheckpoints )
{
    SW_EXPECT_LOG( "KartTrack.TrackCheckpointOutsideSkipped" );
    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( KartTestUtil::makeRingDef( 3, { 0.75f, 0.0f, 0.25f, 1.0f, 0.5f } ) ) );
    SW_REQUIRE_EQUAL( 3, track.getCheckpointCount() );
    SW_EXPECT_NEAR( track.getLength() * 0.25f, track.getGate( 1 )._distance, 0.01f );
    SW_EXPECT_NEAR( track.getLength() * 0.75f, track.getGate( 3 )._distance, 0.01f );

    const sw::float3 before = track.sample( -1.0f )._position;
    const sw::float3 after  = track.sample( 1.0f )._position;
    SW_EXPECT_EQUAL( 1, track.computeGateCrossing( 0, before, after ) );
    SW_EXPECT_EQUAL( -1, track.computeGateCrossing( 0, after, before ) );
    SW_EXPECT_EQUAL( 0, track.computeGateCrossing( 0, after, after ) );
    SW_EXPECT_EQUAL( 0, track.computeGateCrossing( 0, track.sample( 1.0f )._position, track.sample( 3.0f )._position ) );
    SW_EXPECT_EQUAL( 0, track.computeGateCrossing( 1, before, after ) ); // 다른 문
    SW_EXPECT_EQUAL( 0, track.computeGateCrossing( -1, before, after ) );
    SW_EXPECT_EQUAL( 0, track.computeGateCrossing( 99, before, after ) );

    // 폭 12 + 여유 6 씩 — 가장자리 안은 지나고 밖은 안 지난다
    SW_EXPECT_EQUAL( 1, track.computeGateCrossing( 0, KartTestUtil::makeOffsetPoint( track, -1.0f, 11.0f ), KartTestUtil::makeOffsetPoint( track, 1.0f, 11.0f ) ) );
    SW_EXPECT_EQUAL( 0, track.computeGateCrossing( 0, KartTestUtil::makeOffsetPoint( track, -1.0f, 13.0f ), KartTestUtil::makeOffsetPoint( track, 1.0f, 13.0f ) ) );
}

/**
 * @brief [KartTrackTest] 부스트 패드는 새로 올라설 때 한 번 걸리고, 내렸다 다시 오르면 또 걸린다. 넓이 밖은 패드가 아니다
 */
SW_TEST_CASE( KartTrackTest, BoostPadOncePerEntry )
{
    sw::KartBoostPadDef pad;
    pad._at              = 0.25f;
    pad._length          = 4.0f;
    pad._width           = 4.0f;
    pad._duration        = 1.0f;
    sw::KartTrackDef def = KartTestUtil::makeRingDef( 3, {} );
    def._listBoostPad.push_back( pad );
    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( def ) );
    const float32 padDistance = track.getLength() * 0.25f;

    SW_EXPECT_TRUE( track.findBoostPadAt( track.sample( padDistance )._position ) == 0 );
    SW_EXPECT_FALSE( track.findBoostPadAt( track.sample( padDistance + 10.0f )._position ).has_value() );
    SW_EXPECT_FALSE( track.findBoostPadAt( KartTestUtil::makeOffsetPoint( track, padDistance, 3.0f ) ).has_value() );

    sw::KinematicVehicleMotor motor;
    motor.setGround( &track );
    int32 lastPad = -1;
    motor.reset( track.sample( padDistance )._position, 0.0f );
    SW_EXPECT_TRUE( track.applyBoostPad( motor, lastPad ) );
    SW_EXPECT_TRUE( motor.isBoosting() );
    SW_EXPECT_FALSE( track.applyBoostPad( motor, lastPad ) ); // 같은 패드 위
    motor.reset( track.sample( padDistance + 50.0f )._position, 0.0f );
    SW_EXPECT_FALSE( track.applyBoostPad( motor, lastPad ) );
    SW_EXPECT_EQUAL( -1, lastPad );
    motor.reset( track.sample( padDistance )._position, 0.0f );
    SW_EXPECT_TRUE( track.applyBoostPad( motor, lastPad ) );
}

/**
 * @brief [KartTrackTest] 속도 배율: 길 밖, 결승선을 넘어 감긴 구간, 옆 비킴 범위. 결함: 한 바퀴를 모두 덮는 구간(0..1)이 감으면 길이 0 이 되어 시작점에서만 걸렸다
 */
SW_TEST_CASE( KartTrackTest, SpeedScaleZones )
{
    sw::KartSurfaceZoneDef wrapped;
    wrapped._from        = 0.9f;
    wrapped._to          = 0.1f;
    wrapped._speedScale  = 0.25f;
    sw::KartTrackDef def = KartTestUtil::makeRingDef( 3, {} );
    def._listSurfaceZone.push_back( wrapped );
    def._offroadScale = 0.5f;
    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( def ) );
    const float32 length  = track.getLength();
    const auto    scaleAt = [&track]( float32 distance, float32 offset )
    {
        const sw::float3 point = KartTestUtil::makeOffsetPoint( track, distance, offset );
        return track.sampleSurface( point._x, point._z )._speedScale;
    };
    SW_EXPECT_NEAR( 0.25f, scaleAt( length * 0.95f, 0.0f ), kKartTolerance );
    SW_EXPECT_NEAR( 0.25f, scaleAt( length * 0.05f, 0.0f ), kKartTolerance );
    SW_EXPECT_NEAR( 1.0f, scaleAt( length * 0.5f, 0.0f ), kKartTolerance );
    SW_EXPECT_NEAR( 0.5f, scaleAt( length * 0.5f, 8.0f ), kKartTolerance );   // 길 밖
    SW_EXPECT_NEAR( 0.25f, scaleAt( length * 0.95f, 8.0f ), kKartTolerance ); // 둘 중 작은 쪽

    sw::KartSurfaceZoneDef side;
    side._from               = 0.0f;
    side._to                 = 1.0f;
    side._minOffset          = 1.0f;
    side._maxOffset          = 5.0f;
    side._speedScale         = 0.75f;
    sw::KartTrackDef fullDef = KartTestUtil::makeRingDef( 3, {} );
    fullDef._listSurfaceZone.push_back( side );
    sw::KartTrack full;
    SW_REQUIRE_TRUE( full.initialize( fullDef ) );
    const sw::float3 inZone  = KartTestUtil::makeOffsetPoint( full, full.getLength() * 0.5f, 3.0f );
    const sw::float3 outZone = KartTestUtil::makeOffsetPoint( full, full.getLength() * 0.5f, -3.0f );
    SW_EXPECT_NEAR( 0.75f, full.sampleSurface( inZone._x, inZone._z )._speedScale, kKartTolerance );
    SW_EXPECT_NEAR( 1.0f, full.sampleSurface( outZone._x, outZone._z )._speedScale, kKartTolerance );
}

/**
 * @brief [KartTrackTest] 관성 이동: 같은 페달로 1 초 — 내리막 트랙이 평지보다 빠르고, 눈 구간(높은 구름 저항)이 빙판 구간보다 느리며, 빙판은 옆으로 더 미끄러진다
 */
SW_TEST_CASE( KartTrackTest, SlopeAndSurfaceZonesChangeTheKart )
{
    const auto runKart = []( const sw::KartTrackDef& def, float32 seconds, float32 steer, float32& outLateral )
    {
        sw::KartTrack track;
        SW_EXPECT_TRUE( track.initialize( def ) );
        sw::KinematicVehicleMotor motor;
        motor.setGround( &track );
        motor.reset( track.sample( 0.0f )._position, KartTestUtil::computeTrackYaw( track, 0.0f ) );
        sw::KinematicVehicleInput input;
        input._throttle = 1.0f;
        input._steer    = steer;
        for ( int32 stepIndex = 0; stepIndex < static_cast<int32>( seconds * 60.0f + 0.5f ); ++stepIndex )
            motor.update( input, 1.0f / 60.0f );
        outLateral = sw::MathUtil::abs( motor.getLateralSpeed() );
        return motor.getForwardSpeed();
    };
    float32 lateral = 0.0f;

    const sw::KartTrackDef flatDef  = KartTestUtil::makeRingDef( 3, {} );
    sw::KartTrackDef       hillDef  = flatDef;
    hillDef._listControlPoint[1]._y = -40.0f; // 거리 0 에서 +Z 로 나가면 내리막
    const float32 flat              = runKart( flatDef, 1.0f, 0.0f, lateral );
    const float32 downhill          = runKart( hillDef, 1.0f, 0.0f, lateral );
    SW_EXPECT_TRUE( downhill > flat + 0.5f );

    sw::KartSurfaceZoneDef zone;
    zone._from                = 0.0f;
    zone._to                  = 1.0f;
    sw::KartTrackDef iceDef   = flatDef;
    zone._rollingResistance   = 0.005f;
    zone._gripScale           = 0.15f;
    iceDef._listSurfaceZone   = { zone };
    sw::KartTrackDef snowDef  = flatDef;
    zone._rollingResistance   = 0.3f;
    zone._gripScale           = 0.6f;
    snowDef._listSurfaceZone  = { zone };
    float32       iceLateral  = 0.0f;
    float32       snowLateral = 0.0f;
    const float32 ice         = runKart( iceDef, 1.0f, 0.0f, lateral );
    const float32 snow        = runKart( snowDef, 1.0f, 0.0f, lateral );
    SW_EXPECT_TRUE( ice > snow + 1.0f ); // 0.3·9.81 ≈ 2.9 m/s² 를 1 초 덜 민다
    (void)runKart( iceDef, 1.5f, 1.0f, iceLateral );
    (void)runKart( flatDef, 1.5f, 1.0f, snowLateral );
    SW_EXPECT_TRUE( iceLateral > snowLateral ); // 같은 조향에 빙판은 옆 미끄러짐이 남는다
}

/**
 * @brief [KartTrackTest] 쓸 수 없는 정의는 거절하고 빈 상태가 된다. 결함: 조절점이 모두 한 자리면 길이 0 인 트랙이 유효했다(문이 뜻을 잃어 경기가 끝나지 않는다)
 */
SW_TEST_CASE( KartTrackTest, RefusesUnusableDefinitions )
{
    SW_EXPECT_LOG( "KartTrack.TrackNeedsLeastControl" );
    SW_EXPECT_LOG( "KartTrack.TrackHasNoLength" );
    sw::KartTrack track;

    sw::KartTrackDef fewPoints = KartTestUtil::makeRingDef( 3, {} );
    fewPoints._listControlPoint.resize( 2 );
    SW_EXPECT_FALSE( track.initialize( fewPoints ) );
    SW_EXPECT_FALSE( track.isValid() );

    sw::KartTrackDef noWidth = KartTestUtil::makeRingDef( 3, {} );
    noWidth._width           = 0.0f;
    SW_EXPECT_FALSE( track.initialize( noWidth ) );

    sw::KartTrackDef onePoint = KartTestUtil::makeRingDef( 3, {} );
    for ( sw::float3& point : onePoint._listControlPoint )
    {
        point = sw::float3{ 5.0f, 0.0f, 5.0f };
    }
    SW_EXPECT_FALSE( track.initialize( onePoint ) );
    SW_EXPECT_FALSE( track.isValid() );
    SW_EXPECT_NEAR( 0.0f, track.getLength(), kKartTolerance );

    sw::KartRace race;
    SW_EXPECT_TRUE( race.initialize( sw::KartRaceSettings{}, &track, nullptr, nullptr ) ); // 아이템이 없는 경기다
    SW_EXPECT_EQUAL( -1, race.addRacer( sw::KinematicVehicleSettings{}, false ) );
}

/**
 * @brief [KartTrackTest] 카탈로그: 트랙 하나의 칸을 읽고, 점이 모자란 트랙 · 모르는 자식 · 모르는 원소는 그것만 뺀다. 같은 id 는 뒤의 것이 이긴다
 */
SW_TEST_CASE( KartTrackTest, CatalogReportsBrokenTracks )
{
    SW_EXPECT_LOG( "GameCatalog.DuplicateID" ); // 뒤의 ring 은 버리고 앞 정의를 지킨다
    SW_EXPECT_LOG( "KartTrack.TrackNeedsMorePoint" );
    SW_EXPECT_LOG( "KartTrack.TrackUnknownChild" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    constexpr const utf8* kTrackXML = R"(<KartTrackCatalog>
  <Track id="ring" name="Ring" width="12" laps="2" samples="4">
    <Point x="100" z="0"/><Point x="0" z="100"/><Point x="-100" z="0"/><Point x="0" z="-100"/>
    <Checkpoint at="0.5"/><Checkpoint at="0.25"/>
    <ItemBox at="0.5" offset="-3"/>
    <BoostPad at="0.2" duration="1"/>
    <Surface from="0.6" to="0.65" scale="0.4"/>
  </Track>
  <Track id="ring" name="First" width="20" laps="9"><Point x="100" z="0"/><Point x="0" z="100"/><Point x="-100" z="0"/></Track>
  <Track id="short" name="Short"><Point x="0" z="0"/></Track>
  <Track id="odd" name="Odd"><Point x="1" z="0"/><Point x="0" z="1"/><Point x="-1" z="0"/><Bridge/></Track>
  <Mystery/>
</KartTrackCatalog>)";
    sw::KartTrackCatalog  catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kTrackXML, "KartTest" ) );
    SW_EXPECT_TRUE( catalog.find( "short" ) == nullptr );
    SW_EXPECT_TRUE( catalog.find( "odd" ) == nullptr );
    const sw::KartTrackDef* pDef = catalog.find( "ring" );
    SW_REQUIRE_TRUE( pDef != nullptr );
    SW_EXPECT_EQUAL( 2, pDef->_lapCount );

    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( *pDef ) );
    SW_EXPECT_EQUAL( 2, track.getCheckpointCount() );
    SW_EXPECT_EQUAL( size_t( 1 ), track.getItemBoxPositions().size() );
    SW_EXPECT_EQUAL( size_t( 1 ), track.getBoostPads().size() );
    SW_EXPECT_NEAR( track.getLength() * 0.25f, track.getGate( 1 )._distance, 0.01f );
}

/**
 * @brief [KartItemsTest] 순위 표 고르기: 순위 구간, 어느 구간에도 들지 않으면 가장 가까운 표, 기준 인원에 맞춰 늘리기(꼴찌는 늘 꼴찌 표), 표가 없으면 없음
 */
SW_TEST_CASE( KartItemsTest, RankTableSelection )
{
    sw::KartItemCatalog catalog;
    SW_EXPECT_TRUE( catalog.findRankTable( 1, 4 ) == nullptr );
    sw::GameRandom random;
    random.setSeed( 3 );
    SW_EXPECT_TRUE( catalog.rollItem( 1, 4, random ) == nullptr );

    sw::LootEntry front;
    front._itemID = "banana";
    sw::LootEntry back;
    back._itemID = "red";
    catalog.addRankTable( 1, 4, { front } );
    catalog.addRankTable( 8, 5, { back } ); // 뒤집어 적어도 된다
    SW_EXPECT_EQUAL( 1, catalog.findRankTable( 1, 8 )->_fromPlace );
    SW_EXPECT_EQUAL( 5, catalog.findRankTable( 5, 8 )->_fromPlace );
    SW_EXPECT_EQUAL( 8, catalog.findRankTable( 5, 8 )->_toPlace );
    SW_EXPECT_EQUAL( 5, catalog.findRankTable( 99, 0 )->_fromPlace ); // 가장 가까운 표
    SW_EXPECT_EQUAL( 1, catalog.findRankTable( -3, 0 )->_fromPlace );

    catalog.setReferencePlaceCount( 8 );
    SW_EXPECT_EQUAL( 1, catalog.findRankTable( 1, 4 )->_fromPlace );
    SW_EXPECT_EQUAL( 5, catalog.findRankTable( 4, 4 )->_fromPlace ); // 넷 중 꼴찌 = 여덟 중 꼴찌
    SW_EXPECT_EQUAL( 1, catalog.findRankTable( 2, 4 )->_fromPlace ); // 1 + round( 1/3 × 7 ) = 3
    SW_EXPECT_EQUAL( 1, catalog.findRankTable( 1, 1 )->_fromPlace ); // 혼자면 늘리지 않는다
}

/**
 * @brief [KartItemsTest] XML 표로 굴리기: 순위에 맞는 아이템이 나오고 확률은 가중치 몫이다. 시드가 같으면 같다
 */
SW_TEST_CASE( KartItemsTest, RollsByPlace )
{
    sw::KartItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kKartItemsXML, "KartTest" ) );
    sw::GameRandom random;
    random.setSeed( 3 );
    for ( int32 roll = 0; roll < 8; ++roll )
    {
        const sw::KartItemDef* pItem = catalog.rollItem( 1, 4, random );
        SW_REQUIRE_TRUE( pItem != nullptr );
        SW_EXPECT_EQUAL( sw::hashed_string( "banana" ), pItem->_id );
    }
    SW_EXPECT_NEAR( 1.0f, catalog.computeItemChance( 1, 4, "banana" ), kKartTolerance );
    SW_EXPECT_NEAR( 0.25f, catalog.computeItemChance( 3, 4, "green" ), kKartTolerance );
    SW_EXPECT_NEAR( 0.75f, catalog.computeItemChance( 3, 4, "red" ), kKartTolerance );
    SW_EXPECT_NEAR( 0.0f, catalog.computeItemChance( 3, 4, "banana" ), kKartTolerance );

    sw::GameRandom left;
    sw::GameRandom right;
    left.setSeed( 9 );
    right.setSeed( 9 );
    for ( int32 roll = 0; roll < 16; ++roll )
    {
        SW_EXPECT_TRUE( catalog.rollItem( 3, 4, left ) == catalog.rollItem( 3, 4, right ) );
    }
}

/**
 * @brief [KartItemsTest] 깨진 표는 넣지 않고 알린다: 모르는 아이템, 항목 없음, 뒤집힌 범위, 모르는 원소, 아이템 없음
 */
SW_TEST_CASE( KartItemsTest, ReportsBrokenTables )
{
    SW_EXPECT_LOG( "KartItems.ItemNeedsProjectile" ); // 탄을 적지 않은 날아가는 아이템은 받지 않는다
    SW_EXPECT_LOG( "KartItems.RankTableEntryNames" );
    SW_EXPECT_LOG( "KartItems.RankTableNeedsEntryChildren" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    constexpr const utf8* kBrokenXML = R"(<KartItemCatalog>
  <RankTable from="1" to="1"><Entry item="banana"/></RankTable>
  <Item id="banana" kind="Banana" projectile="banana"/>
  <Item id="noshot" kind="Banana"/>
  <RankTable from="2" to="2"><Entry item="ghost"/><Entry item="banana"/></RankTable>
  <RankTable from="3" to="3"/>
  <RankTable from="5" to="4"><Entry item="banana"/></RankTable>
  <Mystery/>
</KartItemCatalog>)";
    sw::KartItemCatalog   catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrokenXML, "KartTest" ) );
    const sw::KartRankTable* pOnly = catalog.findRankTable( 1, 0 ); // 아이템이 뒤에 적혀도 표를 읽는다
    SW_REQUIRE_TRUE( pOnly != nullptr );
    SW_EXPECT_EQUAL( 1, pOnly->_toPlace );
    SW_EXPECT_EQUAL( 1, catalog.findRankTable( 5, 0 )->_toPlace ); // 다른 표는 들어가지 않았다

    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );
    sw::KartItemCatalog   noItems;
    constexpr const utf8* kNoItemsXML = R"(<KartItemCatalog/>)";
    SW_EXPECT_FALSE( noItems.loadFromXMLText( kNoItemsXML, "KartTest" ) );
}

/**
 * @brief [KartAITest] 아이템 판단: 받자마자는 안 쓰고 오래 들고 있지 않는다. 바나나는 뒤 차, 껍질은 앞 차 거리(없으면 안 쓴다), 방어막은 없을 때만
 */
SW_TEST_CASE( KartAITest, ItemDecisions )
{
    sw::KartAIDriver  driver;
    sw::KartAIContext context;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) ); // 아이템 없음

    context._bHasItem     = SW_TRUE;
    context._itemHeldTime = 0.5f;
    context._itemKind     = sw::KartItemKind::Booster;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) ); // 받은 지 얼마 안 됐다
    context._itemHeldTime = 1.0f;
    SW_EXPECT_TRUE( driver.shouldUseItem( context ) );
    context._itemKind = sw::KartItemKind::LeaderShell;
    SW_EXPECT_TRUE( driver.shouldUseItem( context ) );

    context._itemKind = sw::KartItemKind::Banana;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) ); // 뒤 차 없음(-1)
    context._gapBehind = 16.0f;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) );
    context._gapBehind = 15.0f;
    SW_EXPECT_TRUE( driver.shouldUseItem( context ) );

    context._itemKind = sw::KartItemKind::GreenShell;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) );
    context._gapAhead = 45.0f;
    SW_EXPECT_TRUE( driver.shouldUseItem( context ) );
    context._gapAhead = 45.5f;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) );
    context._itemKind = sw::KartItemKind::RedShell;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) );

    context._itemHeldTime = 8.0f; // 더 들고 있지 않는다
    SW_EXPECT_TRUE( driver.shouldUseItem( context ) );

    context._itemHeldTime = 1.0f;
    context._itemKind     = sw::KartItemKind::Shield;
    SW_EXPECT_TRUE( driver.shouldUseItem( context ) );
    context._bShielded = SW_TRUE;
    SW_EXPECT_FALSE( driver.shouldUseItem( context ) );
}

/**
 * @brief [KartAITest] 운전: 겨눈 쪽으로 꺾고, 곡선이 크면 드리프트를 걸었다가 펴지면 놓으며, 아주 급하면 페달을 늦춘다. 빈 트랙은 입력이 없다
 */
SW_TEST_CASE( KartAITest, SteeringAndDrift )
{
    sw::KartTrack track;
    SW_REQUIRE_TRUE( track.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );
    sw::KinematicVehicleSettings vehicle;
    vehicle._drift._minSpeed = 0.0f;
    sw::KinematicVehicleMotor motor;
    motor.setSettings( vehicle );
    motor.setGround( &track );
    const sw::float3 start = track.sample( 0.0f )._position;
    const float32    yaw   = KartTestUtil::computeTrackYaw( track, 0.0f );

    sw::KartAIDriver   driver;
    sw::KartAISettings settings;
    settings._driftStartAngle = 3.0f; // 드리프트 없이 조향만 본다
    driver.setSettings( settings );
    motor.reset( start, yaw + 1.2f ); // 길보다 오른쪽을 본다
    SW_EXPECT_NEAR( -1.0f, driver.computeInput( track, motor, 0.0f )._steer, kKartTolerance );
    motor.reset( start, yaw - 1.2f );
    SW_EXPECT_NEAR( 1.0f, driver.computeInput( track, motor, 0.0f )._steer, kKartTolerance );
    SW_EXPECT_NEAR( 1.0f, driver.computeInput( track, motor, 0.0f )._throttle, kKartTolerance );

    settings._brakeAngle = 0.1f;
    driver.setSettings( settings );
    SW_EXPECT_NEAR( 0.4f, driver.computeInput( track, motor, 0.0f )._throttle, kKartTolerance );

    settings._brakeAngle      = 2.2f;
    settings._driftStartAngle = 0.1f;
    settings._driftEndAngle   = 0.01f;
    driver.setSettings( settings );
    const sw::KinematicVehicleInput drifting = driver.computeInput( track, motor, 0.0f );
    SW_EXPECT_EQUAL( -1, driver.getDriftSide() ); // 왼쪽 곡선
    SW_EXPECT_TRUE( drifting._bDriftHeld != SW_FALSE );
    SW_EXPECT_NEAR( -1.0f, drifting._steer, kKartTolerance ); // 걸 때는 그쪽으로 꺾는다

    settings._driftEndAngle = 1.0f;
    driver.setSettings( settings );
    SW_EXPECT_TRUE( driver.computeInput( track, motor, 0.0f )._bDriftHeld == SW_FALSE );
    SW_EXPECT_EQUAL( 0, driver.getDriftSide() );

    sw::KartTrack empty;
    SW_EXPECT_NEAR( 0.0f, driver.computeInput( empty, motor, 0.0f )._throttle, kKartTolerance );
}

/**
 * @brief [KartGhostTest] 입력 양자화: 축을 범위로 자르고 단추를 옮긴다. 한 번 거친 입력은 다시 거쳐도 그대로다(기록하는 차와 재생이 같은 값으로 달린다)
 */
SW_TEST_CASE( KartGhostTest, QuantizesInput )
{
    sw::KinematicVehicleInput input;
    input._throttle                = 5.0f;
    input._steer                   = -5.0f;
    input._bDriftHeld              = SW_TRUE;
    input._bJumpPressed            = SW_TRUE;
    const sw::KartGhostFrame frame = sw::KartGhost::encodeInput( input, true );
    SW_EXPECT_EQUAL( 127, static_cast<int32>( frame._throttle ) );
    SW_EXPECT_EQUAL( -127, static_cast<int32>( frame._steer ) );
    SW_EXPECT_EQUAL( sw::KartGhost::kButtonDrift | sw::KartGhost::kButtonJump | sw::KartGhost::kButtonUseItem, static_cast<int32>( frame._buttons ) );

    const sw::KinematicVehicleInput decoded = sw::KartGhost::decodeInput( frame );
    SW_EXPECT_NEAR( 1.0f, decoded._throttle, kKartTolerance );
    SW_EXPECT_NEAR( -1.0f, decoded._steer, kKartTolerance );
    SW_EXPECT_TRUE( decoded._bDriftHeld != SW_FALSE );
    SW_EXPECT_TRUE( decoded._bBoostPressed == SW_FALSE );

    input._throttle                       = 0.3333f;
    input._steer                          = -0.71f;
    const sw::KinematicVehicleInput once  = sw::KartGhost::quantizeInput( input );
    const sw::KinematicVehicleInput twice = sw::KartGhost::quantizeInput( once );
    SW_EXPECT_TRUE( once._throttle == twice._throttle );
    SW_EXPECT_TRUE( once._steer == twice._steer );
    SW_EXPECT_NEAR( 0.3333f, once._throttle, 0.01f );
}

/**
 * @brief [KartGhostTest] 프레임 쓰기와 읽기가 맞고, 범위 밖 축(-128)은 쓰지 않으며, 비트가 모자라면 읽지 못한다
 */
SW_TEST_CASE( KartGhostTest, FrameBits )
{
    sw::KartGhostFrame frame;
    frame._throttle = -127;
    frame._steer    = 42;
    frame._buttons  = sw::KartGhost::kButtonBoost;
    sw::BitWriter writer;
    SW_REQUIRE_TRUE( sw::KartGhost::writeFrame( writer, frame ) );

    sw::KartGhostFrame broken;
    broken._throttle = -128;
    sw::BitWriter brokenWriter;
    SW_EXPECT_FALSE( sw::KartGhost::writeFrame( brokenWriter, broken ) );

    const sw::vector<uint8> bytes = writer.releaseBytes();
    sw::BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    sw::KartGhostFrame      read;
    SW_REQUIRE_TRUE( sw::KartGhost::readFrame( reader, read ) );
    SW_EXPECT_EQUAL( -127, static_cast<int32>( read._throttle ) );
    SW_EXPECT_EQUAL( 42, static_cast<int32>( read._steer ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::KartGhost::kButtonBoost ), static_cast<int32>( read._buttons ) );

    sw::BitReader emptyReader( bytes.data(), 0 );
    SW_EXPECT_FALSE( sw::KartGhost::readFrame( emptyReader, read ) );
}

/**
 * @brief [KartGhostTest] 왕복은 머리와 프레임을 지키고 쓰던 버퍼를 다시 써도 된다. 깨진 바이트는 거절하고 상태를 둔다. 결함: 음수 기록을 받았다(0 은 "끝까지 달리지 않음" 인데 음수는 뜻이 없다)
 */
SW_TEST_CASE( KartGhostTest, SerializeRoundTripAndRejection )
{
    sw::KartGhost ghost;
    ghost.begin( sw::float3{ 1.0f, 2.0f, 3.0f }, 0.5f, 0.125f );
    sw::KinematicVehicleInput input;
    input._throttle = 1.0f;
    ghost.record( input );
    input._steer = -1.0f;
    ghost.record( input, true );
    ghost.setFinishTime( 9.5f );

    sw::vector<uint8> buffer{ 1, 2, 3 }; // 앞의 내용은 버린다
    SW_REQUIRE_TRUE( ghost.serialize( buffer ) );

    sw::KartGhost restored;
    SW_REQUIRE_TRUE( restored.deserialize( buffer.data(), static_cast<int32>( buffer.size() ) ) );
    SW_REQUIRE_EQUAL( 2, restored.getFrameCount() );
    SW_EXPECT_NEAR( 2.0f, restored.getStartPosition()._y, kKartTolerance );
    SW_EXPECT_NEAR( 0.5f, restored.getStartYaw(), kKartTolerance );
    SW_EXPECT_NEAR( 0.125f, restored.getStep(), kKartTolerance );
    SW_EXPECT_NEAR( 9.5f, restored.getFinishTime(), kKartTolerance );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::KartGhost::kButtonUseItem ), static_cast<int32>( restored.getFrames()[1]._buttons ) );

    SW_EXPECT_FALSE( restored.deserialize( nullptr, 10 ) );
    SW_EXPECT_FALSE( restored.deserialize( buffer.data(), 0 ) );
    SW_EXPECT_FALSE( restored.deserialize( buffer.data(), 12 ) ); // 머리 중간에서 잘렸다

    const uint32 kMagic = sw::FourCCUtil::make( "KGHT" );
    const auto   read   = [&restored]( const sw::vector<uint8>& bytes )
    { return restored.deserialize( bytes.data(), static_cast<int32>( bytes.size() ) ); };
    SW_EXPECT_TRUE( read( KartTestUtil::writeGhostHead( kMagic, 0.1f, 0.0f, 0 ) ) ); // 형식은 맞다
    SW_REQUIRE_TRUE( restored.deserialize( buffer.data(), static_cast<int32>( buffer.size() ) ) );

    SW_EXPECT_FALSE( read( KartTestUtil::writeGhostHead( sw::FourCCUtil::make( "NOPE" ), 0.1f, 0.0f, 0 ) ) );
    SW_EXPECT_FALSE( read( KartTestUtil::writeGhostHead( kMagic, 0.0f, 0.0f, 0 ) ) );
    SW_EXPECT_FALSE( read( KartTestUtil::writeGhostHead( kMagic, -0.1f, 0.0f, 0 ) ) );
    SW_EXPECT_FALSE( read( KartTestUtil::writeGhostHead( kMagic, std::numeric_limits<float32>::quiet_NaN(), 0.0f, 0 ) ) );
    SW_EXPECT_FALSE( read( KartTestUtil::writeGhostHead( kMagic, 0.1f, -1.0f, 0 ) ) );
    SW_EXPECT_FALSE( read( KartTestUtil::writeGhostHead( kMagic, 0.1f, 0.0f, 1000000 ) ) ); // 남은 바이트로 담을 수 없는 수
    SW_EXPECT_EQUAL( 2, restored.getFrameCount() );
}

/**
 * @brief [KartGhostTest] 재생: 프레임 시간을 고정 걸음으로 나누고 기록이 끝나면 멈춘다. 고스트 없이는 끝난 상태다
 */
SW_TEST_CASE( KartGhostTest, PlayerStopsAtEnd )
{
    sw::KartGhost ghost;
    ghost.begin( sw::float3{}, 0.0f, 0.125f );
    for ( int32 frame = 0; frame < 3; ++frame )
    {
        ghost.record( sw::KinematicVehicleInput{} );
    }
    sw::KartGhostPlayer player;
    player.initialize( &ghost, sw::KinematicVehicleSettings{}, nullptr );
    SW_EXPECT_FALSE( player.isFinished() );
    SW_EXPECT_EQUAL( 2, player.advance( 0.25f ) );
    SW_EXPECT_EQUAL( 1, player.advance( 0.25f ) ); // 두 걸음 몫이지만 하나 남았다
    SW_EXPECT_TRUE( player.isFinished() );
    SW_EXPECT_EQUAL( 3, player.getFrameIndex() );
    SW_EXPECT_FALSE( player.step() );
    SW_EXPECT_EQUAL( 0, player.advance( 0.25f ) );

    sw::KartGhostPlayer empty;
    empty.initialize( nullptr, sw::KinematicVehicleSettings{}, nullptr );
    SW_EXPECT_TRUE( empty.isFinished() );
    SW_EXPECT_FALSE( empty.step() );
}

/**
 * @brief [KartRaceTest] 결함: 격자에 서기 전에 기록을 걸면 출발 위치가 틀려 재생이 다른 길로 갔다. 양자화한 입력과 부스트 패드까지 같은 순서로 돌아 경로가 같다
 */
SW_TEST_CASE_F( KartRaceTest, GhostReplaysSamePath )
{
    sw::KartBoostPadDef pad;
    pad._at              = 0.02f;
    pad._width           = 30.0f;
    pad._length          = 8.0f;
    sw::KartTrackDef def = KartTestUtil::makeRingDef( 3, {} );
    def._listBoostPad.push_back( pad );

    SW_REQUIRE_TRUE( _track.initialize( def ) );
    initializeRace( _race, _settings );
    SW_REQUIRE_EQUAL( 0, _race.addRacer( sw::KinematicVehicleSettings{}, false ) );
    sw::KartGhost ghost;
    _race.startGhostRecording( 0, &ghost ); // 출발 전에 건다
    _race.start();
    SW_EXPECT_NEAR( racer( 0 )._motor.getPosition()._x, ghost.getStartPosition()._x, kKartTolerance );
    SW_EXPECT_NEAR( racer( 0 )._motor.getPosition()._z, ghost.getStartPosition()._z, kKartTolerance );

    constexpr int32 kStepCount = 300;
    for ( int32 stepIndex = 0; stepIndex < kStepCount; ++stepIndex )
    {
        sw::KartRacerInput input;
        input._vehicle._throttle = 1.0f;
        input._vehicle._steer    = stepIndex % 60 < 30 ? 0.123f : -0.2f;
        _race.setInput( 0, input );
        _race.step();
    }
    SW_REQUIRE_EQUAL( kStepCount, ghost.getFrameCount() );
    SW_EXPECT_TRUE( countEvents( sw::KartRaceEvent::Kind::BoostPad ) >= 1 ); // 패드를 밟은 판이다

    sw::KartGhostPlayer player;
    player.initialize( &ghost, sw::KinematicVehicleSettings{}, &_track );
    while ( player.step() )
    {
    }
    SW_EXPECT_EQUAL( kStepCount, player.getFrameIndex() );
    SW_EXPECT_NEAR( racer( 0 )._motor.getPosition()._x, player.getMotor().getPosition()._x, kKartTolerance );
    SW_EXPECT_NEAR( racer( 0 )._motor.getPosition()._z, player.getMotor().getPosition()._z, kKartTolerance );
}

/**
 * @brief [KartRaceTest] 단계: 차가 없으면 시작하지 않고, 카운트다운 동안은 시간이 흐르지 않으며, 시작 뒤에는 차를 더하지 못한다. 트랙이 없으면 더하지 못한다
 */
SW_TEST_CASE_F( KartRaceTest, PhasesAndCountdown )
{
    SW_REQUIRE_TRUE( _track.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );
    _settings._countdownTime = 0.25f;
    _settings._step          = 0.1f;
    initializeRace( _race, _settings );
    _race.start();
    SW_EXPECT_EQUAL( sw::MatchPhase::Waiting, _race.getPhase() );
    SW_EXPECT_EQUAL( 0, _race.update( 0.0f ) );
    _race.step();
    SW_EXPECT_EQUAL( sw::MatchPhase::Waiting, _race.getPhase() );

    SW_EXPECT_EQUAL( 0, _race.addRacer( sw::KinematicVehicleSettings{}, false ) );
    _race.start();
    SW_EXPECT_EQUAL( sw::MatchPhase::Warmup, _race.getPhase() );
    SW_EXPECT_EQUAL( -1, _race.addRacer( sw::KinematicVehicleSettings{}, false ) );
    SW_EXPECT_EQUAL( 1, racer( 0 )._place );

    _race.step();
    _race.step();
    SW_EXPECT_EQUAL( sw::MatchPhase::Warmup, _race.getPhase() );
    SW_EXPECT_NEAR( 0.0f, _race.getRaceTime(), kKartTolerance );
    _race.step();
    SW_EXPECT_EQUAL( sw::MatchPhase::InProgress, _race.getPhase() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::RaceStarted ) );
    _race.step();
    SW_EXPECT_NEAR( 0.1f, _race.getRaceTime(), kKartTolerance );

    sw::KartRace noTrack;
    SW_EXPECT_TRUE( noTrack.initialize( _settings, nullptr, nullptr, nullptr ) );
    SW_EXPECT_EQUAL( -1, noTrack.addRacer( sw::KinematicVehicleSettings{}, false ) );
    SW_EXPECT_TRUE( noTrack.findRacer( 0 ) == nullptr );
    SW_EXPECT_FALSE( noTrack.findRacerAtPlace( 1 ).has_value() );
}

/**
 * @brief [KartRaceTest] 바퀴: 체크포인트를 차례로 다 지나야 오른다. 앞뒤로 넘나들면 통과가 취소되고, 결승선을 뒤로 넘으면 바퀴와 기록을 되돌린다. 마지막 바퀴 뒤에 결승이다
 */
SW_TEST_CASE_F( KartRaceTest, LapsRequireEveryCheckpointInOrder )
{
    open( KartTestUtil::makeRingDef( 2, { 0.25f, 0.5f, 0.75f } ), 1 );
    SW_EXPECT_EQUAL( 0, racer( 0 )._lap );

    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) ); // 출발 격자에서 결승선을 넘으면 1 바퀴째
    SW_EXPECT_EQUAL( 1, racer( 0 )._lap );
    SW_EXPECT_EQUAL( 0, racer( 0 )._nextCheckpoint );
    SW_EXPECT_EQUAL( 0, countEvents( sw::KartRaceEvent::Kind::LapCompleted ) );

    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) ); // 체크포인트 없이 — 지름길
    SW_EXPECT_EQUAL( 1, racer( 0 )._lap );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::CheckpointMissed ) );

    SW_REQUIRE_TRUE( driveGate( 0, 2, true ) ); // 순서가 틀렸다
    SW_EXPECT_EQUAL( 0, racer( 0 )._nextCheckpoint );
    SW_REQUIRE_TRUE( driveGate( 0, 1, true ) );
    SW_EXPECT_EQUAL( 1, racer( 0 )._nextCheckpoint );
    SW_REQUIRE_TRUE( driveGate( 0, 1, false ) ); // 뒤로 지나면 취소
    SW_EXPECT_EQUAL( 0, racer( 0 )._nextCheckpoint );
    SW_REQUIRE_TRUE( driveGate( 0, 1, true ) );
    SW_REQUIRE_TRUE( driveGate( 0, 2, true ) );
    SW_REQUIRE_TRUE( driveGate( 0, 3, true ) );
    SW_EXPECT_EQUAL( 3, racer( 0 )._nextCheckpoint );

    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) );
    SW_EXPECT_EQUAL( 2, racer( 0 )._lap );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::LapCompleted ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::FinalLap ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), racer( 0 )._listLapTime.size() );
    SW_EXPECT_TRUE( racer( 0 )._bestLapTime > 0.0f );
    SW_EXPECT_TRUE( _race.getBestLapTime() > 0.0f );

    SW_REQUIRE_TRUE( driveGate( 0, 1, true ) );
    SW_REQUIRE_TRUE( driveGate( 0, 2, true ) );
    SW_REQUIRE_TRUE( driveGate( 0, 3, true ) );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) );
    SW_EXPECT_EQUAL( sw::MatchPhase::Ended, _race.getPhase() );
    SW_EXPECT_TRUE( racer( 0 )._bFinished == SW_TRUE );
    SW_EXPECT_EQUAL( 1, racer( 0 )._finishPlace );
    SW_EXPECT_TRUE( racer( 0 )._finishTime > 0.0f );
    SW_EXPECT_EQUAL( size_t( 2 ), racer( 0 )._listLapTime.size() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::Finished ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::RaceEnded ) );
}

/**
 * @brief [KartRaceTest] 결함: 결승선을 뒤로 넘어 바퀴를 지워도 경기 최고 기록은 지운 바퀴로 남았다. 차 기록과 함께 되돌린다
 */
SW_TEST_CASE_F( KartRaceTest, RevokedLapLeavesNoBestTime )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 1 );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) );
    SW_REQUIRE_EQUAL( 2, racer( 0 )._lap );
    SW_EXPECT_TRUE( _race.getBestLapTime() > 0.0f );

    SW_REQUIRE_TRUE( drive( 0, 0.0f, false ) );
    SW_EXPECT_EQUAL( 1, racer( 0 )._lap );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::LapRevoked ) );
    SW_EXPECT_TRUE( racer( 0 )._listLapTime.empty() );
    SW_EXPECT_NEAR( 0.0f, racer( 0 )._bestLapTime, kKartTolerance );
    SW_EXPECT_NEAR( 0.0f, _race.getBestLapTime(), kKartTolerance );

    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) ); // 다시 넘으면 그 바퀴의 시작부터 잰다
    SW_EXPECT_EQUAL( 2, racer( 0 )._lap );
    SW_REQUIRE_EQUAL( size_t( 1 ), racer( 0 )._listLapTime.size() );
    SW_EXPECT_NEAR( racer( 0 )._listLapTime[0], _race.getBestLapTime(), kKartTolerance );

    SW_REQUIRE_TRUE( drive( 0, 0.0f, false ) );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, false ) ); // 1 바퀴를 지우고 출발선 전으로
    SW_EXPECT_EQUAL( 0, racer( 0 )._lap );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, false ) ); // 출발선 전에서는 더 내려가지 않는다
    SW_EXPECT_EQUAL( 0, racer( 0 )._lap );
}

/**
 * @brief [KartRaceTest] 결함: 한 걸음에 문 하나만 세어, 같은 자리의 두 문(또는 한 걸음 안의 가까운 문)은 뒤의 문을 영영 지나지 못해 바퀴를 끝낼 수 없었다
 */
SW_TEST_CASE_F( KartRaceTest, TwoGatesInOneStepBothCount )
{
    open( KartTestUtil::makeRingDef( 3, { 0.5f, 0.5f } ), 1 );
    SW_REQUIRE_EQUAL( 2, _track.getCheckpointCount() );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) );
    SW_REQUIRE_TRUE( driveGate( 0, 1, true ) );
    SW_EXPECT_EQUAL( 2, racer( 0 )._nextCheckpoint );
    SW_EXPECT_EQUAL( 2, countEvents( sw::KartRaceEvent::Kind::CheckpointPassed ) );
    SW_REQUIRE_TRUE( drive( 0, 0.0f, true ) );
    SW_EXPECT_EQUAL( 2, racer( 0 )._lap );

    SW_REQUIRE_TRUE( driveGate( 0, 1, true ) );
    SW_EXPECT_EQUAL( 2, racer( 0 )._nextCheckpoint );
    SW_REQUIRE_TRUE( driveGate( 0, 1, false ) ); // 함께 취소된다
    SW_EXPECT_EQUAL( 0, racer( 0 )._nextCheckpoint );
}

/**
 * @brief [KartRaceTest] 순위는 진행값 순이고, 진행값은 다음 문을 넘지 못한다(문을 건너뛴 차가 앞선 것처럼 보이지 않는다). 첫 결승 뒤 시한이 지나면 남은 차를 지금 순위로 끝낸다
 */
SW_TEST_CASE_F( KartRaceTest, PlacesAndFinishTimeout )
{
    _settings._finishTimeout = 0.05f;
    open( KartTestUtil::makeRingDef( 1, {} ), 3 );
    _race.placeRacer( 2, _track.sample( -2.0f )._position, KartTestUtil::computeTrackYaw( _track, -2.0f ) ); // 격자 앞으로
    _race.step();
    SW_EXPECT_TRUE( _race.findRacerAtPlace( 1 ) == 2 );
    SW_EXPECT_EQUAL( 1, racer( 2 )._place );
    SW_EXPECT_FALSE( _race.findRacerAtPlace( 0 ).has_value() );
    SW_EXPECT_FALSE( _race.findRacerAtPlace( 4 ).has_value() );
    SW_EXPECT_TRUE( countEvents( sw::KartRaceEvent::Kind::PlaceChanged ) >= 1 );

    _race.placeRacer( 1, _track.sample( -3.0f )._position, KartTestUtil::computeTrackYaw( _track, -3.0f ) );
    SW_REQUIRE_TRUE( drive( 2, 0.0f, true ) ); // 1 바퀴째
    SW_REQUIRE_TRUE( drive( 2, 0.0f, true ) ); // 결승
    SW_EXPECT_EQUAL( 1, racer( 2 )._finishPlace );
    SW_EXPECT_NEAR( _race.getRaceTime(), racer( 2 )._finishTime, 0.5f );

    stepTimes( 10 );
    SW_REQUIRE_EQUAL( sw::MatchPhase::Ended, _race.getPhase() );
    SW_EXPECT_EQUAL( 2, racer( 1 )._finishPlace ); // 더 앞에 있었다
    SW_EXPECT_EQUAL( 3, racer( 0 )._finishPlace );
    SW_EXPECT_NEAR( 0.0f, racer( 1 )._finishTime, kKartTolerance ); // 기록 없음
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::RaceEnded ) );
    const float32 endedAt = _race.getRaceTime();
    stepTimes( 5 );
    SW_EXPECT_NEAR( endedAt, _race.getRaceTime(), kKartTolerance );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::RaceEnded ) );
}

/**
 * @brief [KartRaceTest] 역주행 시간은 이어서 거꾸로 달린 시간이다 - 끊기면 처음부터 센다
 */
SW_TEST_CASE_F( KartRaceTest, WrongWayTimerCountsOnlyUnbrokenReversing )
{
    _settings._wrongWayDelay = 10.0f;
    open( KartTestUtil::makeRingDef( 3, {} ), 1 );
    sw::KartRacerInput input;
    input._vehicle._throttle = 1.0f;
    const float32 yaw        = KartTestUtil::computeTrackYaw( _track, 200.0f ) + sw::MathUtil::kPi;
    _race.placeRacer( 0, _track.sample( 200.0f )._position, yaw );
    for ( int32 stepIndex = 0; stepIndex < 120 && racer( 0 )._wrongWayTime <= 0.0f; ++stepIndex )
    {
        _race.setInput( 0, input );
        _race.step();
    }
    SW_REQUIRE_TRUE( racer( 0 )._wrongWayTime > 0.0f );

    // 멈추면(앞으로 달리지 않아도) 처음부터 다시 센다
    _race.placeRacer( 0, _track.sample( 200.0f )._position, yaw );
    _race.setInput( 0, sw::KartRacerInput{} );
    _race.step();
    SW_EXPECT_NEAR( 0.0f, racer( 0 )._wrongWayTime, 1.0e-5f );
}

/**
 * @brief [KartRaceTest] 역주행: 정한 시간 동안 거꾸로 달리면 알리고, 앞으로 달리면 풀린다
 */
SW_TEST_CASE_F( KartRaceTest, WrongWayDetection )
{
    _settings._wrongWayDelay = 0.5f;
    open( KartTestUtil::makeRingDef( 3, {} ), 1 );
    sw::KartRacerInput input;
    input._vehicle._throttle = 1.0f;

    _race.placeRacer( 0, _track.sample( 200.0f )._position, KartTestUtil::computeTrackYaw( _track, 200.0f ) + sw::MathUtil::kPi );
    for ( int32 stepIndex = 0; stepIndex < 120 && racer( 0 )._bWrongWay == SW_FALSE; ++stepIndex )
    {
        _race.setInput( 0, input );
        _race.step();
    }
    SW_EXPECT_TRUE( racer( 0 )._bWrongWay == SW_TRUE );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::WrongWay ) );
    SW_EXPECT_TRUE( _race.getRaceTime() >= 0.5f );

    _race.placeRacer( 0, _track.sample( 200.0f )._position, KartTestUtil::computeTrackYaw( _track, 200.0f ) );
    for ( int32 stepIndex = 0; stepIndex < 120 && racer( 0 )._bWrongWay == SW_TRUE; ++stepIndex )
    {
        _race.setInput( 0, input );
        _race.step();
    }
    SW_EXPECT_TRUE( racer( 0 )._bWrongWay == SW_FALSE );
    SW_EXPECT_EQUAL( 2, countEvents( sw::KartRaceEvent::Kind::WrongWay ) );
    SW_EXPECT_NEAR( 0.0f, racer( 0 )._wrongWayTime, kKartTolerance );
}

/**
 * @brief [KartRaceTest] 눌림은 다음 걸음까지 남고 그 걸음에 쓰인다. 맞아서 도는 동안의 입력은 버린다
 */
SW_TEST_CASE_F( KartRaceTest, PressesLatchUntilNextStep )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 1 );
    sw::KartRacerInput pressed;
    pressed._bUseItem               = SW_TRUE;
    pressed._vehicle._bBoostPressed = SW_TRUE;
    _race.setInput( 0, pressed );
    _race.setInput( 0, sw::KartRacerInput{} ); // 같은 걸음 안에 뗐다
    SW_EXPECT_TRUE( racer( 0 )._input._bUseItem == SW_TRUE );
    SW_EXPECT_TRUE( racer( 0 )._input._vehicle._bBoostPressed == SW_TRUE );

    _race.giveItem( 0, "boost" );
    _race.step();
    SW_EXPECT_TRUE( racer( 0 )._input._bUseItem == SW_FALSE );
    SW_EXPECT_TRUE( racer( 0 )._itemID.empty() ); // 그 걸음에 썼다
    SW_EXPECT_TRUE( racer( 0 )._motor.isBoosting() );

    sw::KartItemDef hit;
    hit._spinTime = 1.0f;
    SW_REQUIRE_TRUE( _race.applyHit( 0, -1, hit ) );
    _race.giveItem( 0, "boost" );
    _race.setInput( 0, pressed );
    _race.step();
    SW_EXPECT_TRUE( racer( 0 )._input._bUseItem == SW_FALSE );
    SW_EXPECT_EQUAL( sw::hashed_string( "boost" ), racer( 0 )._itemID ); // 도는 동안은 쓰지 못한다

    _race.setInput( 7, pressed ); // 없는 차는 아무 일도 없다
    _race.setInput( -1, pressed );
}

/**
 * @brief [KartRaceTest] 아이템 쓰기: 부스터 · 방어막, 빈손 · 없는 차 · 모르는 아이템. 방어막은 한 번 막고 사라지며 뚫는 아이템은 막지 못한다. 결함: 시작 전에도 쓸 수 있었다
 */
SW_TEST_CASE_F( KartRaceTest, ItemUseAndShield )
{
    SW_REQUIRE_TRUE( _track.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );
    initializeRace( _race, _settings );
    SW_REQUIRE_EQUAL( 0, _race.addRacer( sw::KinematicVehicleSettings{}, false ) );
    _race.giveItem( 0, "boost" );
    SW_EXPECT_FALSE( _race.useItem( 0 ) ); // 아직 시작 전
    SW_EXPECT_EQUAL( sw::hashed_string( "boost" ), racer( 0 )._itemID );
    _race.start();

    SW_EXPECT_FALSE( _race.useItem( -1 ) );
    SW_EXPECT_TRUE( _race.useItem( 0 ) );
    SW_EXPECT_TRUE( racer( 0 )._motor.isBoosting() );
    SW_EXPECT_FALSE( _race.useItem( 0 ) ); // 빈손
    _race.giveItem( 0, "nothing" );
    SW_EXPECT_FALSE( _race.useItem( 0 ) );

    _race.giveItem( 0, "shield" );
    SW_EXPECT_TRUE( _race.useItem( 0 ) );
    SW_EXPECT_TRUE( racer( 0 )._shieldTime.isActive() );

    sw::KartItemDef hit;
    hit._spinTime = 1.0f;
    SW_EXPECT_FALSE( _race.applyHit( 0, -1, hit ) );
    SW_EXPECT_FALSE( racer( 0 )._spinTime.isActive() );
    SW_EXPECT_FALSE( racer( 0 )._shieldTime.isActive() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::ShieldBlocked ) );

    SW_EXPECT_TRUE( _race.applyHit( 0, -1, hit ) );
    SW_EXPECT_TRUE( racer( 0 )._spinTime.isActive() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::Hit ) );

    _race.giveItem( 0, "shield" );
    SW_EXPECT_TRUE( _race.useItem( 0 ) );
    hit._bIgnoresShield = true;
    SW_EXPECT_TRUE( _race.applyHit( 0, -1, hit ) );
    SW_EXPECT_TRUE( racer( 0 )._shieldTime.isActive() ); // 뚫었으니 방어막은 그대로다

    SW_EXPECT_FALSE( _race.applyHit( 9, -1, hit ) );
}

/**
 * @brief [KartRaceTest] 날거나 놓이는 아이템의 운동은 탄 카탈로그의 탄이다. 탄을 적지 않은 아이템은 카탈로그가 받지 않고, 탄 카탈로그에 없는 탄은 경기를 열 때 알리며 써도 나가지 않고 아이템이 남는다
 */
SW_TEST_CASE_F( KartRaceTest, ItemsFlyByTheirProjectile )
{
    SW_EXPECT_LOG( "KartItems.ItemNeedsProjectile" );
    SW_EXPECT_LOG( "KartRace.ItemProjectileMissing" );
    sw::KartItemCatalog broken;
    SW_REQUIRE_TRUE( broken.loadFromXMLText( R"(<KartItemCatalog>
  <Item id="bare" kind="GreenShell"/>
  <Item id="lost" kind="GreenShell" projectile="ghost_shell"/>
  <Item id="boost" kind="Booster" boost="2"/>
</KartItemCatalog>)",
                                             "KartTest" ) );
    SW_EXPECT_TRUE( broken.find( "bare" ) == nullptr );
    SW_REQUIRE_TRUE( _track.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );
    SW_EXPECT_FALSE( _race.initialize( _settings, &_track, &broken, &_projectiles ) );
    SW_REQUIRE_EQUAL( 0, _race.addRacer( sw::KinematicVehicleSettings{}, false ) );
    _race.start();
    _race.giveItem( 0, "lost" );
    SW_EXPECT_FALSE( _race.useItem( 0 ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "lost" ), racer( 0 )._itemID );
    SW_EXPECT_TRUE( _race.getProjectiles().empty() );
    _race.giveItem( 0, "boost" ); // 탄을 쓰지 않는 아이템은 그대로 된다
    SW_EXPECT_TRUE( _race.useItem( 0 ) );

    // 탄의 값이 운동을 정한다: 녹색 껍질은 탄의 속력 45 로 날고 탄의 반지름만큼 앞에서 나간다.
    SW_REQUIRE_TRUE( _race.initialize( _settings, &_track, &_items, &_projectiles ) );
    SW_REQUIRE_EQUAL( 0, _race.addRacer( sw::KinematicVehicleSettings{}, false ) );
    _race.start();
    _race.giveItem( 0, "green" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), _race.getProjectiles().size() );
    SW_EXPECT_NEAR( 45.0f, _race.getProjectiles()[0].getSpeed(), kKartTolerance );
    SW_EXPECT_NEAR( 1.2f, _race.getProjectiles()[0]._radius, kKartTolerance );
    SW_EXPECT_NEAR( 1.0f, _race.getProjectiles()[0]._lifetime, kKartTolerance );
    SW_EXPECT_TRUE( _race.getProjectiles()[0]._defID == sw::hashed_string( "green" ) ); // 반응은 아이템이 정한다
}

/**
 * @brief [KartRaceTest] 바나나: 차 뒤에 멈춰 남고(수명 0) 밟은 차가 돈다. 쏜 차도 유예가 지나면 맞는다
 */
SW_TEST_CASE_F( KartRaceTest, BananaStaysAndHits )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 2 );
    _race.giveItem( 0, "banana" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), _race.getProjectiles().size() );
    const sw::float3 dropped = _race.getProjectiles()[0]._position;
    const sw::float3 behind  = pointAhead( 0, -2.5f );
    SW_EXPECT_NEAR( behind._x, dropped._x, kKartTolerance );
    SW_EXPECT_NEAR( behind._z, dropped._z, kKartTolerance );

    stepTimes( 120 );
    SW_REQUIRE_EQUAL( size_t( 1 ), _race.getProjectiles().size() );
    SW_EXPECT_NEAR( dropped._x, _race.getProjectiles()[0]._position._x, kKartTolerance );
    SW_EXPECT_EQUAL( 0, countEvents( sw::KartRaceEvent::Kind::Hit ) );

    _race.placeRacer( 1, dropped, 0.0f );
    _race.step();
    SW_EXPECT_TRUE( _race.getProjectiles().empty() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::Hit ) );
    SW_EXPECT_TRUE( racer( 1 )._spinTime.isActive() );

    _race.giveItem( 1, "banana" );
    SW_REQUIRE_TRUE( _race.useItem( 1 ) );
    const sw::float3 ownBanana = _race.getProjectiles()[0]._position;
    _race.placeRacer( 1, ownBanana, 0.0f );
    _race.step();
    SW_EXPECT_EQUAL( size_t( 1 ), _race.getProjectiles().size() ); // 유예 중이다
    stepTimes( 40 );
    SW_EXPECT_TRUE( _race.getProjectiles().empty() );
}

/**
 * @brief [KartRaceTest] 껍질: 녹색은 곧게 날아 앞 차를 맞히고, 맞히지 못하면 수명이 다해 사라진다
 */
SW_TEST_CASE_F( KartRaceTest, GreenShellFliesAndExpires )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 2 );
    _race.placeRacer( 1, pointAhead( 0, 15.0f ), 0.0f );
    _race.giveItem( 0, "green" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::ItemUsed ) );
    stepTimes( 40 );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::Hit ) );
    SW_EXPECT_TRUE( _race.getProjectiles().empty() );
    SW_EXPECT_TRUE( racer( 1 )._spinTime.isActive() );
    SW_EXPECT_FALSE( racer( 0 )._spinTime.isActive() );

    _race.placeRacer( 1, pointAhead( 0, -50.0f ), 0.0f ); // 비켰다
    _race.giveItem( 0, "green" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), _race.getProjectiles().size() );
    stepTimes( 80 ); // 수명 1 초
    SW_EXPECT_TRUE( _race.getProjectiles().empty() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::Hit ) );
}

/**
 * @brief [KartRaceTest] 빨간 껍질은 바로 앞 순위의 차를 잡는다
 */
SW_TEST_CASE_F( KartRaceTest, RedShellTargetsCarAhead )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 3 );
    _race.placeRacer( 1, pointAhead( 0, 5.0f ), 0.0f );
    _race.placeRacer( 2, pointAhead( 0, 2.0f ), 0.0f );
    _race.step();
    const int32 ahead = _race.findRacerAtPlace( racer( 0 )._place - 1 ).value();
    SW_REQUIRE_EQUAL( 2, ahead ); // 차 2 가 차 0 바로 앞이다
    _race.giveItem( 0, "red" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), _race.getProjectiles().size() );
    SW_EXPECT_EQUAL( ahead, _race.getProjectiles()[0]._target );
}

/**
 * @brief [KartRaceTest] 결함: 1 등 공격은 이미 들어온 1 등을 쫓아 아무도 맞히지 못하고 떠돌았다. 경기 중인 맨 앞 차를 쫓는다. 들어온 차는 아이템을 쓰지 못하고 맞지도 않는다
 */
SW_TEST_CASE_F( KartRaceTest, LeaderShellChasesLeaderStillRacing )
{
    open( KartTestUtil::makeRingDef( 1, {} ), 3 );
    SW_REQUIRE_TRUE( drive( 2, 0.0f, true ) );
    SW_REQUIRE_TRUE( drive( 2, 0.0f, true ) );
    SW_REQUIRE_TRUE( racer( 2 )._bFinished == SW_TRUE );

    _race.giveItem( 2, "boost" );
    SW_EXPECT_FALSE( _race.useItem( 2 ) ); // 들어온 차는 경기 밖이다
    sw::KartItemDef hit;
    hit._spinTime = 1.0f;
    SW_EXPECT_FALSE( _race.applyHit( 2, -1, hit ) );

    _race.placeRacer( 1, pointAhead( 0, 4.0f ), 0.0f ); // 경기 중인 차의 맨 앞으로 세운다
    _race.step();
    SW_REQUIRE_TRUE( _race.findRacerAtPlace( 2 ) == 1 );

    _race.giveItem( 0, "blue" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), _race.getProjectiles().size() );
    SW_EXPECT_EQUAL( 1, _race.getProjectiles()[0]._target );
    stepTimes( 2 );
    SW_EXPECT_TRUE( _race.getProjectiles().empty() );
    SW_EXPECT_TRUE( racer( 1 )._spinTime.isActive() );
}

/**
 * @brief [KartRaceTest] 상자: 빈손이면 순위 표로 뽑고, 들고 있어도 깨지며 아이템은 그대로다. 다시 놓일 때까지 먹지 못한다. 아이템을 끈 경기에는 상자가 깨지지 않는다
 */
SW_TEST_CASE_F( KartRaceTest, ItemBoxesRespawn )
{
    sw::KartItemBoxDef box;
    box._at              = 0.25f;
    sw::KartTrackDef def = KartTestUtil::makeRingDef( 3, {} );
    def._listItemBox.push_back( box );
    _settings._itemBoxRespawn = 0.1f;
    open( def, 1 );
    const sw::float3 boxPosition = _track.getItemBoxPositions()[0];
    const sw::float3 away        = _track.sample( _track.getLength() * 0.75f )._position;
    SW_EXPECT_TRUE( _race.isItemBoxActive( 0 ) );
    SW_EXPECT_FALSE( _race.isItemBoxActive( -1 ) );
    SW_EXPECT_FALSE( _race.isItemBoxActive( 1 ) );

    _race.placeRacer( 0, boxPosition, 0.0f );
    _race.step();
    SW_EXPECT_FALSE( _race.isItemBoxActive( 0 ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "banana" ), racer( 0 )._itemID ); // 1 등 표
    SW_EXPECT_EQUAL( 1, countEvents( sw::KartRaceEvent::Kind::ItemBoxTaken ) );

    _race.placeRacer( 0, away, 0.0f );
    stepTimes( 12 );
    SW_EXPECT_TRUE( _race.isItemBoxActive( 0 ) );

    _race.giveItem( 0, "green" );
    _race.placeRacer( 0, boxPosition, 0.0f );
    _race.step();
    sw::hashed_string lastTaken( "stale" );
    SW_EXPECT_FALSE( _race.isItemBoxActive( 0 ) ); // 들고 있어도 깨진다
    SW_EXPECT_EQUAL( sw::hashed_string( "green" ), racer( 0 )._itemID );
    SW_EXPECT_EQUAL( 2, countEvents( sw::KartRaceEvent::Kind::ItemBoxTaken ) );
    for ( const sw::KartRaceEvent& event : _listEvent )
    {
        if ( event._kind == sw::KartRaceEvent::Kind::ItemBoxTaken && event._time > 0.0f )
            lastTaken = event._itemID;
    }
    SW_EXPECT_TRUE( lastTaken.empty() ); // 받은 것이 없다

    sw::KartRaceSettings noItems = _settings;
    noItems._bItems              = SW_FALSE;
    sw::KartRace plain;
    initializeRace( plain, noItems );
    SW_REQUIRE_EQUAL( 0, plain.addRacer( sw::KinematicVehicleSettings{}, false ) );
    plain.start();
    plain.placeRacer( 0, boxPosition, 0.0f );
    plain.step();
    SW_EXPECT_TRUE( plain.isItemBoxActive( 0 ) );
    SW_EXPECT_TRUE( plain.findRacer( 0 )->_itemID.empty() );
}

/**
 * @brief [KartRaceTest] 러버밴딩: 사람과 들어온 차는 1 이다. 뒤처진 AI 는 간격에 비례해 상한까지 빨라지고, 사람보다 앞선 AI 1 등은 느려진다. 끄면 1 이다
 */
SW_TEST_CASE_F( KartRaceTest, RubberBandScale )
{
    _settings._bRubberBand = SW_TRUE;
    open( KartTestUtil::makeRingDef( 3, {} ), 2, true ); // 0 은 사람, 1 은 AI
    const float32 length = _track.getLength();

    _race.placeRacer( 0, _track.sample( length - 10.0f )._position, 0.0f );
    _race.placeRacer( 1, _track.sample( length - 70.0f )._position, 0.0f );
    SW_EXPECT_NEAR( 1.0f, _race.computeRubberBandScale( 0 ), kKartTolerance );
    const float32 gap = racer( 0 )._progress - racer( 1 )._progress;
    SW_EXPECT_NEAR( 60.0f, gap, 1.0f );
    SW_EXPECT_NEAR( 1.0f + 0.15f * gap / 120.0f, _race.computeRubberBandScale( 1 ), kKartTolerance );

    _race.placeRacer( 1, _track.sample( length - 300.0f )._position, 0.0f );
    SW_EXPECT_NEAR( 1.15f, _race.computeRubberBandScale( 1 ), kKartTolerance );

    _race.placeRacer( 1, _track.sample( length - 5.0f )._position, 0.0f );
    _race.placeRacer( 0, _track.sample( length - 65.0f )._position, 0.0f );
    const float32 lead = racer( 1 )._progress - racer( 0 )._progress;
    SW_EXPECT_NEAR( 1.0f - 0.05f * lead / 120.0f, _race.computeRubberBandScale( 1 ), kKartTolerance );

    SW_EXPECT_NEAR( 1.0f, _race.computeRubberBandScale( -1 ), kKartTolerance );

    sw::KartRaceSettings off = _settings;
    off._bRubberBand         = SW_FALSE;
    sw::KartRace plain;
    initializeRace( plain, off );
    (void)plain.addRacer( sw::KinematicVehicleSettings{}, false );
    (void)plain.addRacer( sw::KinematicVehicleSettings{}, true );
    plain.start();
    SW_EXPECT_NEAR( 1.0f, plain.computeRubberBandScale( 1 ), kKartTolerance );
}

/**
 * @brief [KartRaceTest] AI 차는 스스로 달려 앞으로 나아간다
 */
SW_TEST_CASE_F( KartRaceTest, AIDriverMakesProgress )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 2, true );
    const float32 before = racer( 1 )._progress;
    stepTimes( 180 );
    SW_EXPECT_TRUE( racer( 1 )._progress > before + 10.0f );
    SW_EXPECT_TRUE( _track.isOnRoad( racer( 1 )._motor.getPosition() ) );
    SW_EXPECT_NEAR( before, racer( 0 )._progress, 1.0f ); // 사람은 입력이 없다
}

/**
 * @brief [KartRaceTest] 상태를 쓰고 읽은 경기는 같은 입력에 같은 경로를 낸다. 차 수가 다르면 거절한다
 */
SW_TEST_CASE_F( KartRaceTest, StateRoundTripContinuesSameRace )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 2 );
    sw::KartRacerInput input;
    input._vehicle._throttle = 1.0f;
    input._vehicle._steer    = 0.1f;
    for ( int32 stepIndex = 0; stepIndex < 30; ++stepIndex )
    {
        _race.setInput( 0, input );
        _race.step();
    }
    _race.giveItem( 0, "green" );
    SW_REQUIRE_TRUE( _race.useItem( 0 ) );

    sw::Archive written;
    _race.writeState( written );

    sw::KartRace restored;
    initializeRace( restored, _settings );
    (void)restored.addRacer( sw::KinematicVehicleSettings{}, false );
    (void)restored.addRacer( sw::KinematicVehicleSettings{}, false );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( sw::MatchPhase::InProgress, restored.getPhase() );
    SW_REQUIRE_EQUAL( size_t( 1 ), restored.getProjectiles().size() );

    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        _race.setInput( 0, input );
        restored.setInput( 0, input );
        _race.step();
        restored.step();
    }
    SW_EXPECT_NEAR( racer( 0 )._motor.getPosition()._x, restored.findRacer( 0 )->_motor.getPosition()._x, kKartTolerance );
    SW_EXPECT_NEAR( racer( 0 )._motor.getPosition()._z, restored.findRacer( 0 )->_motor.getPosition()._z, kKartTolerance );
    SW_EXPECT_NEAR( _race.getRaceTime(), restored.getRaceTime(), kKartTolerance );

    sw::KartRace solo;
    initializeRace( solo, _settings );
    (void)solo.addRacer( sw::KinematicVehicleSettings{}, false );
    sw::Archive readingAgain( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( solo.readState( readingAgain ) );
}

/**
 * @brief [KartRaceTest] 결함: 체크포인트 수를 트랙과 맞춰 보지 않아, 체크포인트가 줄어든 트랙에서 세이브를 읽으면 진행값 계산이 문 밖을 읽었다 — 거절하고 상태를 둔다
 */
SW_TEST_CASE_F( KartRaceTest, ReadStateRefusesCheckpointOutsideTrack )
{
    open( KartTestUtil::makeRingDef( 3, { 0.25f, 0.5f, 0.75f } ), 1 );
    SW_REQUIRE_EQUAL( 3, racer( 0 )._nextCheckpoint ); // 출발선 전에는 다 지난 것으로 본다
    sw::Archive written;
    _race.writeState( written );

    sw::KartTrack noGates;
    SW_REQUIRE_TRUE( noGates.initialize( KartTestUtil::makeRingDef( 3, {} ) ) );
    sw::KartRace other;
    initializeRace( other, _settings, &noGates );
    SW_REQUIRE_EQUAL( 0, other.addRacer( sw::KinematicVehicleSettings{}, false ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( other.readState( reading ) );
    SW_EXPECT_EQUAL( sw::MatchPhase::Waiting, other.getPhase() );
    SW_EXPECT_EQUAL( 0, other.findRacer( 0 )->_nextCheckpoint );
}

/**
 * @brief [KartRaceTest] 결함: NaN · 음수 경기 시간과 최고 기록, NaN 첫 결승 시각(시한이 영영 끝나지 않는다)을 받았다 — 거절한다. 꼬리의 값만 바꾼 바이트로 본다(바꾸지 않으면 읽힌다)
 */
SW_TEST_CASE_F( KartRaceTest, ReadStateRefusesBrokenTimes )
{
    open( KartTestUtil::makeRingDef( 3, {} ), 1 );
    stepTimes( 5 );
    sw::Archive written;
    _race.writeState( written );
    const sw::vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    // 꼬리: 경기 시간(4) 카운트다운(4) 최고 기록(4) 첫 결승(4) 들어온 수(4) 단계(1)
    constexpr size_t kRaceTimeFromEnd    = 21;
    constexpr size_t kBestLapFromEnd     = 13;
    constexpr size_t kFirstFinishFromEnd = 9;

    const auto readPatched = [this, &bytes]( size_t fromEnd, float32 value )
    {
        sw::vector<uint8> patched = bytes;
        std::memcpy( patched.data() + patched.size() - fromEnd, &value, sizeof( value ) );
        sw::Archive reading( patched.data(), patched.size() );
        return _race.readState( reading );
    };
    const float32 raceTime = _race.getRaceTime();
    const float32 nan      = std::numeric_limits<float32>::quiet_NaN();
    SW_EXPECT_TRUE( readPatched( kRaceTimeFromEnd, raceTime ) ); // 같은 값이면 읽힌다
    SW_EXPECT_NEAR( raceTime, _race.getRaceTime(), kKartTolerance );

    SW_EXPECT_FALSE( readPatched( kRaceTimeFromEnd, nan ) );
    SW_EXPECT_FALSE( readPatched( kRaceTimeFromEnd, -1.0f ) );
    SW_EXPECT_FALSE( readPatched( kBestLapFromEnd, nan ) );
    SW_EXPECT_FALSE( readPatched( kBestLapFromEnd, -1.0f ) );
    SW_EXPECT_FALSE( readPatched( kFirstFinishFromEnd, nan ) );
    SW_EXPECT_NEAR( raceTime, _race.getRaceTime(), kKartTolerance );
}
