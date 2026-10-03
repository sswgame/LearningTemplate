#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/ThemePark/CoasterTrain.h"

#include "TestFramework/TestFramework.h"

// 테마파크 키트의 코스터 — 조각으로 지은 트랙의 모양(닫힘 · 좌표계), 열차 물리(에너지 보존 · 체인 · 브레이크), 시험 운행 평가(뒤집힘 · G ·
// 에어타임 · 멈춤)를 본다. 엔진 오브젝트 없이 수학만 돈다.

using namespace sw;

namespace
{
    CoasterTrackPiece makeCoasterPiece( CoasterPieceType type, float32 length, float32 height )
    {
        CoasterTrackPiece piece;
        piece._type   = type;
        piece._length = length;
        piece._height = height;
        return piece;
    }

    CoasterTrackPiece makeCoasterTurn( CoasterPieceType type, float32 radius, float32 angle, float32 bank )
    {
        CoasterTrackPiece piece;
        piece._type   = type;
        piece._radius = radius;
        piece._angle  = angle;
        piece._bank   = bank;
        return piece;
    }

    /**
     * @brief 스테이션 → 리프트 → 낙하 → (루프) → U 턴 → 돌아오는 긴 직선 → U 턴 → 브레이크로 스테이션 바로 뒤에 닿는 회로입니다.
     * @details 끝이 스테이션 **앞**(z < 0)에 오도록 길이를 맞췄다 — 끝이 스테이션을 지나 있으면 닫는 연결 곡선이 되접혀 G 가 튄다(빌더가 경고한다).
     */
    CoasterTrack buildTestCircuit( bool bWithLoop, float32 liftHeight )
    {
        CoasterTrackBuilder builder;
        builder.reset( float3{ 0.0f, 1.0f, 0.0f }, 0.0f );
        builder.appendPiece( makeCoasterPiece( CoasterPieceType::Station, 12.0f, 0.0f ) );
        builder.appendPiece( makeCoasterPiece( CoasterPieceType::LiftHill, 30.0f, liftHeight ) );
        builder.appendPiece( makeCoasterPiece( CoasterPieceType::Drop, 45.0f, liftHeight ) );
        if ( bWithLoop )
        {
            CoasterTrackPiece loop;
            loop._type   = CoasterPieceType::Loop;
            loop._radius = 10.0f;
            loop._width  = 3.0f;
            builder.appendPiece( loop );
        }
        builder.appendPiece( makeCoasterPiece( CoasterPieceType::Straight, 10.0f, 0.0f ) );
        builder.appendPiece( makeCoasterTurn( CoasterPieceType::TurnRight, 18.0f, 180.0f, 45.0f ) );
        builder.appendPiece( makeCoasterPiece( CoasterPieceType::Straight, bWithLoop ? 130.0f : 116.0f, 0.0f ) );
        builder.appendPiece( makeCoasterTurn( CoasterPieceType::TurnRight, 18.0f, 180.0f, 30.0f ) );
        builder.appendPiece( makeCoasterPiece( CoasterPieceType::Brakes, 15.0f, 0.0f ) );
        return builder.makeTrack( true );
    }

    bool isOrthonormal( const CoasterTrackFrame& frame )
    {
        const float32 tolerance = 1.0e-3f;
        return MathUtil::abs( frame._forward.getLength() - 1.0f ) < tolerance && MathUtil::abs( frame._up.getLength() - 1.0f ) < tolerance &&
               MathUtil::abs( frame._right.getLength() - 1.0f ) < tolerance && MathUtil::abs( frame._forward.dot( frame._up ) ) < tolerance &&
               MathUtil::abs( frame._forward.dot( frame._right ) ) < tolerance;
    }
} // namespace

/**
 * @brief [CoasterTest] 조각으로 지은 회로는 닫혀 처음 자리로 돌아오고, 어디서든 좌표계가 직교 단위다
 * @details 끝에서 시작점까지는 에르미트 연결 곡선이 잇는다. 거리는 한 바퀴 길이로 감긴다(음수 포함). 뱅크 회전 안의 위는 회전 안쪽으로 기운다.
 */
SW_TEST_CASE( CoasterTest, CircuitClosesAndFramesStayOrthonormal )
{
    const CoasterTrack track = buildTestCircuit( true, 25.0f );
    SW_ASSERT_TRUE( track.isClosed() );
    SW_EXPECT_TRUE( track.getLength() > 150.0f );
    SW_EXPECT_NEAR_EQUAL( 26.0f, track.getMaxHeight(), 0.5f ); // 1 + 25 (루프 꼭대기 14 보다 높다)

    const CoasterTrackFrame start   = track.sample( 0.0f );
    const CoasterTrackFrame wrapped = track.sample( track.getLength() );
    const CoasterTrackFrame before  = track.sample( -1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, float3::getDistance( start._position, wrapped._position ), 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, float3::getDistance( before._position, track.sample( track.getLength() - 1.0f )._position ), 0.01f );

    bool bAllOrthonormal = true;
    for ( float32 distance = 0.0f; distance < track.getLength(); distance += 0.7f )
        bAllOrthonormal = bAllOrthonormal && isOrthonormal( track.sample( distance ) );
    SW_EXPECT_TRUE( bAllOrthonormal );

    // 스테이션 · 리프트 · 브레이크 표시가 그 구간에 실린다.
    SW_EXPECT_TRUE( ( track.sample( 5.0f )._flags & CoasterSegmentFlag::kStation ) != 0 );
    SW_EXPECT_TRUE( ( track.sample( 25.0f )._flags & CoasterSegmentFlag::kLift ) != 0 );
}

/**
 * @brief [CoasterTest] 마찰 없는 낙하의 바닥 속도는 에너지 보존 그대로다 — v² = v0² + 2gh
 */
SW_TEST_CASE( CoasterTest, FrictionlessDropConservesEnergy )
{
    CoasterTrackBuilder builder;
    builder.reset( float3{ 0.0f, 30.0f, 0.0f }, 0.0f );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Straight, 5.0f, 0.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Drop, 40.0f, 20.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Straight, 200.0f, 0.0f ) );
    const CoasterTrack track = builder.makeTrack( false );
    SW_ASSERT_TRUE( track.isClosed() == false );

    CoasterPhysicsParams params;
    params._rollingResistance = 0.0f;
    params._dragCoefficient   = 0.0f;
    CoasterTrain train;
    train.initialize( &track, params, 0.0f );
    train.setSpeed( 1.0f );
    while ( train.getDistance() < 60.0f && train.getElapsedTime() < 30.0f )
        train.step( 1.0f / 60.0f );

    const float32 expectedSpeed = MathUtil::sqrt( 1.0f + 2.0f * 9.81f * 20.0f );
    SW_EXPECT_NEAR_EQUAL( expectedSpeed, train.getSpeed(), expectedSpeed * 0.02f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, train.getGForce()._vertical, 0.05f ); // 평지에서는 1G
}

/**
 * @brief [CoasterTest] 체인은 리프트 속도로 끌어 올리고, 브레이크는 그 속도 위를 깎는다 · 스테이션은 멈춘 열차를 출발시킨다
 */
SW_TEST_CASE( CoasterTest, LiftBrakesAndStationDriveTheTrain )
{
    const CoasterTrack   track = buildTestCircuit( false, 20.0f );
    CoasterPhysicsParams params;
    CoasterTrain         train;
    train.initialize( &track, params, 0.0f );

    bool    bSawLift        = false;
    bool    bLiftAtSpeed    = true;
    bool    bLeftStation    = false;
    float32 brakeEntrySpeed = -1.0f;
    float32 brakeExitSpeed  = -1.0f;
    for ( int32 frameIndex = 0; frameIndex < 60 * 120 && train.getLapCount() < 1; ++frameIndex )
    {
        train.step( 1.0f / 60.0f );
        const uint8 flags = train.getFrame()._flags;
        bLeftStation      = bLeftStation || train.getDistance() > 12.0f;
        if ( ( flags & CoasterSegmentFlag::kLift ) != 0 && train.getDistance() > 20.0f && train.getDistance() < 38.0f )
        {
            bSawLift     = true;
            bLiftAtSpeed = bLiftAtSpeed && MathUtil::abs( train.getSpeed() - params._liftSpeed ) < 0.2f;
        }
        if ( ( flags & CoasterSegmentFlag::kBrake ) != 0 )
        {
            if ( brakeEntrySpeed < 0.0f )
                brakeEntrySpeed = train.getSpeed();
            brakeExitSpeed = train.getSpeed();
        }
    }
    SW_EXPECT_TRUE( bLeftStation );
    SW_EXPECT_TRUE( bSawLift );
    SW_EXPECT_TRUE( bLiftAtSpeed );
    SW_EXPECT_EQUAL( 1, train.getLapCount() );
    SW_EXPECT_TRUE( brakeEntrySpeed > params._brakeSpeed + 1.0f ); // 브레이크가 할 일이 있었다
    SW_EXPECT_NEAR_EQUAL( params._brakeSpeed, brakeExitSpeed, 0.3f );
}

/**
 * @brief [CoasterTest] 루프가 있는 회로는 시험 운행에서 한 바퀴를 돌고, 뒤집힘 하나 · 바닥의 큰 양의 G · 낙하 하나를 잰다
 */
SW_TEST_CASE( CoasterTest, LoopCircuitCompletesWithOneInversion )
{
    const CoasterTrack     track = buildTestCircuit( true, 25.0f );
    const CoasterRideStats stats = CoasterRideAnalyzer::analyze( track, CoasterPhysicsParams{} );
    SW_EXPECT_TRUE( stats._bCompleted == SW_TRUE );
    SW_EXPECT_TRUE( stats._bStalled == SW_FALSE );
    SW_EXPECT_EQUAL( 1u, stats._inversionCount );
    SW_EXPECT_TRUE( stats._maxVerticalG > 2.0f );
    SW_EXPECT_TRUE( stats._dropCount >= 1u );
    SW_EXPECT_TRUE( stats._maxDropHeight > 20.0f );
    SW_EXPECT_TRUE( stats._maxSpeed > 18.0f );
    SW_EXPECT_TRUE( stats._excitement > 0.0f );
    SW_EXPECT_TRUE( stats._intensity > 0.0f );

    // 루프가 없는 같은 회로는 뒤집힘이 없고 멀미도 덜하다.
    const CoasterRideStats tame = CoasterRideAnalyzer::analyze( buildTestCircuit( false, 25.0f ), CoasterPhysicsParams{} );
    SW_EXPECT_TRUE( tame._bCompleted == SW_TRUE );
    SW_EXPECT_EQUAL( 0u, tame._inversionCount );
    SW_EXPECT_TRUE( tame._nausea < stats._nausea );
}

/**
 * @brief [CoasterTest] 빠르게 넘는 낙타 등은 에어타임(음의 G)을 주고, 넘을 수 없는 언덕은 끝까지 못 가 흥분이 0 이다(뒤로 굴러 스테이션과 언덕 사이를 오간다)
 */
SW_TEST_CASE( CoasterTest, CamelbackGivesAirtimeAndTooTallHillStalls )
{
    CoasterTrackBuilder builder;
    builder.reset( float3{ 0.0f, 1.0f, 0.0f }, 0.0f );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Station, 10.0f, 0.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::LiftHill, 30.0f, 30.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Drop, 30.0f, 30.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Hill, 14.0f, 8.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Straight, 150.0f, 0.0f ) );
    const CoasterRideStats airtime = CoasterRideAnalyzer::analyze( builder.makeTrack( false ), CoasterPhysicsParams{}, 120.0f );
    SW_EXPECT_TRUE( airtime._airtime > 0.1f );
    SW_EXPECT_TRUE( airtime._minVerticalG < 0.0f );

    builder.reset( float3{ 0.0f, 1.0f, 0.0f }, 0.0f );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Station, 10.0f, 0.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::LiftHill, 20.0f, 10.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Drop, 20.0f, 10.0f ) );
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Hill, 30.0f, 25.0f ) ); // 리프트(10 m)보다 높다
    builder.appendPiece( makeCoasterPiece( CoasterPieceType::Straight, 20.0f, 0.0f ) );
    const CoasterRideStats stalled = CoasterRideAnalyzer::analyze( builder.makeTrack( false ), CoasterPhysicsParams{}, 120.0f );
    SW_EXPECT_TRUE( stalled._bCompleted == SW_FALSE );
    SW_EXPECT_EQUAL( 0.0f, stalled._excitement );
}

/**
 * @brief [CoasterTest] 레이아웃 XML 은 조각을 차례로 읽고 모르는 조각은 건너뛴다 · 조각 이름은 왕복한다
 */
SW_TEST_CASE( CoasterTest, LayoutCatalogReadsPiecesAndSkipsUnknownOnes )
{
    constexpr const utf8* kLayoutXml = R"(
<CoasterCatalog>
  <Layout id="mini" name="Mini" startHeight="2">
    <Piece type="Station" length="8"/>
    <Piece type="liftHill" length="20" height="12"/>
    <Piece type="Corkscrew" length="10"/>
    <Piece type="TurnLeft" radius="9" angle="120" bank="25"/>
  </Layout>
</CoasterCatalog>
)";
    CoasterLayoutCatalog  catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kLayoutXml, "CoasterTest" ) );
    const CoasterLayoutDef* pLayout = catalog.findLayout( "mini" );
    SW_ASSERT_NOT_NULL( pLayout );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), pLayout->_listPiece.size() );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pLayout->_startHeight, 1.0e-4f );
    SW_EXPECT_TRUE( pLayout->_listPiece[1]._type == CoasterPieceType::LiftHill );
    SW_EXPECT_NEAR_EQUAL( 25.0f, pLayout->_listPiece[2]._bank, 1.0e-4f );

    const CoasterPieceType arrType[] = { CoasterPieceType::Station, CoasterPieceType::Straight, CoasterPieceType::LiftHill, CoasterPieceType::Drop,
                                         CoasterPieceType::Hill, CoasterPieceType::TurnLeft, CoasterPieceType::TurnRight, CoasterPieceType::Loop,
                                         CoasterPieceType::Brakes, CoasterPieceType::Booster };
    for ( const CoasterPieceType type : arrType )
    {
        CoasterPieceType parsed{ CoasterPieceType::Straight };
        SW_EXPECT_TRUE( parseCoasterPieceType( toString( type ), parsed ) && parsed == type );
    }
}
