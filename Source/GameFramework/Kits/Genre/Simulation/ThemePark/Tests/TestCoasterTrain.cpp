// CoasterTrainTest — 열차 물리(마찰 · 구간 장치 · 끝 · 바퀴 · G)와 승차 평가(완주 · 멈춤 · 낙하 · 뒤집힘 · 평가 식). 키트 GF_ThemePark(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrain.h"

#include <cmath>

using namespace sw;

namespace
{
    struct CoasterTrainTestUtil
    {
        /** @brief 저항이 없고 중력이 10 인 매개변수입니다. 손으로 계산한 값과 견줍니다. */
        static CoasterPhysicsParams makeFrictionlessParams()
        {
            CoasterPhysicsParams params;
            params._gravity           = 10.0f;
            params._rollingResistance = 0.0f;
            params._dragCoefficient   = 0.0f;
            return params;
        }

        static CoasterTrackPiece makePiece( CoasterPieceType type, float32 length, float32 height = 0.0f )
        {
            CoasterTrackPiece piece;
            piece._type   = type;
            piece._length = length;
            piece._height = height;
            return piece;
        }

        static CoasterTrackPiece makeTurnRight( float32 radius, float32 angle )
        {
            CoasterTrackPiece piece;
            piece._type   = CoasterPieceType::TurnRight;
            piece._radius = radius;
            piece._angle  = angle;
            return piece;
        }

        /** @brief 조각들로 트랙을 만듭니다. 시작은 높이 1, 진행 방향 +Z 입니다. */
        static CoasterTrack makeTrack( const vector<CoasterTrackPiece>& listPiece, bool bClosed )
        {
            CoasterTrackBuilder builder;
            builder.reset( float3{ 0.0f, 1.0f, 0.0f }, 0.0f );
            builder.appendPieces( listPiece );
            return builder.makeTrack( bClosed );
        }

        /** @brief 반지름 10 의 수평 원입니다(90 도 오른쪽 회전 넷, 길이 약 62.8 m). 표시가 없습니다. */
        static CoasterTrack makeFlatCircle()
        {
            const CoasterTrackPiece quarter = makeTurnRight( 10.0f, MathUtil::kHalfPi );
            return makeTrack( { quarter, quarter, quarter, quarter }, true );
        }

        /** @brief @p seconds 를 1/60 초 프레임으로 나눠 돌립니다(`step` 은 한 프레임을 0.25 초에서 자른다). */
        static void run( CoasterTrain& inoutTrain, float32 seconds )
        {
            const int32 frameCount = static_cast<int32>( std::lround( seconds * 60.0f ) );
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                inoutTrain.step( 1.0f / 60.0f );
            }
        }
    };
    using Util = CoasterTrainTestUtil;
} // namespace

/**
 * @brief [CoasterTrainTest] 범위 밖 매개변수를 자르지 않고 거절하고 열차를 그대로 둔다 - 적분 간격 양 끝, 음수 상수
 */
SW_TEST_CASE( CoasterTrainTest, InitializeRefusesOutOfRangeParams )
{
    SW_EXPECT_LOG( "CoasterTrain.CoasterPhysicsParamsRefusedFixedStepNeeds" );
    const CoasterTrack track = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 20.0f ) }, false );
    CoasterTrain       train;

    CoasterPhysicsParams tooCoarse = Util::makeFrictionlessParams();
    tooCoarse._fixedStep           = 1.0f / 10.0f;
    SW_EXPECT_FALSE( train.initialize( &track, tooCoarse ) );

    CoasterPhysicsParams tooFine = Util::makeFrictionlessParams();
    tooFine._fixedStep           = 1.0f / 5000.0f;
    SW_EXPECT_FALSE( train.initialize( &track, tooFine ) );

    CoasterPhysicsParams negativeDrag = Util::makeFrictionlessParams();
    negativeDrag._dragCoefficient     = -0.1f;
    SW_EXPECT_FALSE( train.initialize( &track, negativeDrag ) );

    SW_EXPECT_NULL( train.getTrack() );
    SW_EXPECT_TRUE( train.initialize( &track, Util::makeFrictionlessParams() ) );
    SW_EXPECT_TRUE( train.getTrack() == &track );
}

/**
 * @brief [CoasterTrainTest] 결함: 중력 0 을 받아 G 를 0 으로 나눠 NaN 을 냈고 승차 평가까지 번졌다. 거절한다 - 분석도 돌지 않는다
 */
SW_TEST_CASE( CoasterTrainTest, ZeroGravityIsRefused )
{
    SW_EXPECT_LOG( "CoasterTrain.CoasterPhysicsParamsRefusedFixedStepNeeds" );
    const CoasterTrack   track  = Util::makeTrack( { Util::makePiece( CoasterPieceType::Station, 20.0f ) }, false );
    CoasterPhysicsParams params = Util::makeFrictionlessParams();
    params._gravity             = 0.0f;
    CoasterTrain train;
    SW_EXPECT_FALSE( train.initialize( &track, params ) );

    const CoasterRideStats stats = CoasterRideStats::analyze( track, params );
    SW_EXPECT_TRUE( stats._bCompleted == SW_FALSE );
    SW_EXPECT_EQUAL( 1.0f, stats._maxVerticalG ); // 측정하지 않은 기본값 그대로다
    SW_EXPECT_NEAR( track.getLength(), stats._length, 1.0e-4f );
}

/**
 * @brief [CoasterTrainTest] 트랙이 없는 열차는 진행하지 않고 기본 좌표계를 낸다
 */
SW_TEST_CASE( CoasterTrainTest, TrainWithoutTrackDoesNothing )
{
    CoasterTrain train;
    train.step( 1.0f );
    train.setDistance( 5.0f );
    SW_EXPECT_EQUAL( 0.0f, train.getDistance() );
    SW_EXPECT_EQUAL( 0.0f, train.getElapsedTime() );
    SW_EXPECT_EQUAL( 1.0f, train.getFrame()._up._y );
    SW_EXPECT_EQUAL( 1.0f, train.getCarFrame( 2, 3.0f )._forward._z );
}

/**
 * @brief [CoasterTrainTest] 수평에 멈춘 열차는 그대로 서고, 구름 저항은 μg 로 감속한다. 공기 저항은 속도 제곱으로 더해진다
 */
SW_TEST_CASE( CoasterTrainTest, FrictionDeceleratesAndStaticFrictionHolds )
{
    const CoasterTrack track = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 500.0f ) }, false );

    CoasterPhysicsParams rolling = Util::makeFrictionlessParams();
    rolling._rollingResistance   = 0.1f; // 감속 1 m/s^2
    CoasterTrain train;
    SW_REQUIRE_TRUE( train.initialize( &track, rolling, 100.0f ) );
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( 0.0f, train.getSpeed(), 1.0e-4f ); // 정지 마찰
    SW_EXPECT_NEAR( 100.0f, train.getDistance(), 1.0e-3f );

    train.setSpeed( 5.0f );
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( 4.0f, train.getSpeed(), 0.02f );
    SW_EXPECT_NEAR( 104.5f, train.getDistance(), 0.05f );
    SW_EXPECT_NEAR( 2.0f, train.getElapsedTime(), 0.02f ); // 멈춰 있던 1 초도 시간은 흐른다

    CoasterPhysicsParams drag = Util::makeFrictionlessParams();
    drag._dragCoefficient     = 0.01f; // 10 m/s 에서 감속 1 m/s^2
    SW_REQUIRE_TRUE( train.initialize( &track, drag, 0.0f ) );
    train.setSpeed( 10.0f );
    Util::run( train, 0.1f );
    SW_EXPECT_NEAR( 9.9f, train.getSpeed(), 0.02f );
}

/**
 * @brief [CoasterTrainTest] 체인은 정한 속도로 끌고, 부스터는 상한까지 올리고, 브레이크는 정한 속도까지 내리되 더 내리지 않는다
 */
SW_TEST_CASE( CoasterTrainTest, LiftBoosterAndBrakeSetTheSpeed )
{
    const CoasterPhysicsParams params = Util::makeFrictionlessParams();
    CoasterTrain               train;

    const CoasterTrack lift = Util::makeTrack( { Util::makePiece( CoasterPieceType::LiftHill, 100.0f, 5.0f ) }, false );
    SW_REQUIRE_TRUE( train.initialize( &lift, params ) );
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( params._liftSpeed, train.getSpeed(), 0.01f );
    train.setSpeed( -3.0f ); // 체인은 뒤로 못 가게 끈다
    Util::run( train, 0.1f );
    SW_EXPECT_NEAR( params._liftSpeed, train.getSpeed(), 0.01f );

    const CoasterTrack booster = Util::makeTrack( { Util::makePiece( CoasterPieceType::Booster, 500.0f ) }, false );
    SW_REQUIRE_TRUE( train.initialize( &booster, params ) );
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( params._boosterAcceleration, train.getSpeed(), 0.1f );
    Util::run( train, 4.0f );
    SW_EXPECT_NEAR( params._boosterSpeed, train.getSpeed(), 0.01f );

    const CoasterTrack brake = Util::makeTrack( { Util::makePiece( CoasterPieceType::Brakes, 500.0f ) }, false );
    SW_REQUIRE_TRUE( train.initialize( &brake, params ) );
    train.setSpeed( 20.0f );
    Util::run( train, 0.5f );
    SW_EXPECT_NEAR( 15.0f, train.getSpeed(), 0.1f );
    Util::run( train, 3.0f );
    SW_EXPECT_NEAR( params._brakeSpeed, train.getSpeed(), 0.01f );
    train.setSpeed( 2.0f ); // 그보다 느리면 건드리지 않는다
    Util::run( train, 0.5f );
    SW_EXPECT_NEAR( 2.0f, train.getSpeed(), 0.01f );
}

/**
 * @brief [CoasterTrainTest] 스테이션은 느리면 올리고 빠르면 내려 정한 속도로 맞춘다
 */
SW_TEST_CASE( CoasterTrainTest, StationConvergesFromBothSides )
{
    const CoasterPhysicsParams params = Util::makeFrictionlessParams();
    const CoasterTrack         track  = Util::makeTrack( { Util::makePiece( CoasterPieceType::Station, 500.0f ) }, false );
    CoasterTrain               train;
    SW_REQUIRE_TRUE( train.initialize( &track, params ) );
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( params._stationSpeed, train.getSpeed(), 0.01f );

    train.setSpeed( 13.0f );
    Util::run( train, 0.5f );
    SW_EXPECT_NEAR( 8.0f, train.getSpeed(), 0.1f ); // 감속 10 m/s^2
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( params._stationSpeed, train.getSpeed(), 0.01f );
}

/**
 * @brief [CoasterTrainTest] 경사에서 중력이 끌어 내리고, 언덕을 못 넘으면 음수 속도로 뒤로 굴러 내린다
 */
SW_TEST_CASE( CoasterTrainTest, SlopeAcceleratesDownhillAndRollsBack )
{
    const CoasterTrack track = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 10.0f ), Util::makePiece( CoasterPieceType::Drop, 40.0f, 10.0f ) }, false );
    CoasterTrain       train;
    SW_REQUIRE_TRUE( train.initialize( &track, Util::makeFrictionlessParams(), 30.0f ) ); // 내리막 중간
    Util::run( train, 0.5f );
    SW_EXPECT_TRUE( train.getSpeed() > 0.0f );

    const CoasterTrack climb = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 10.0f ), Util::makePiece( CoasterPieceType::Hill, 40.0f, 20.0f ) }, false );
    SW_REQUIRE_TRUE( train.initialize( &climb, Util::makeFrictionlessParams(), 5.0f ) );
    train.setSpeed( 8.0f ); // 3.2 m 밖에 못 오른다. 약 2 초에 꼭대기, 약 5 초에 시작점으로 돌아가 멈추므로 그 사이를 본다
    Util::run( train, 3.0f );
    SW_EXPECT_TRUE( train.getSpeed() < 0.0f );
}

/**
 * @brief [CoasterTrainTest] 열린 트랙은 끝에서 멈추고(거리 = 길이, 속도 0) 바퀴를 세지 않는다
 */
SW_TEST_CASE( CoasterTrainTest, OpenTrackStopsAtTheEnd )
{
    const CoasterTrack track = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 10.0f ) }, false );
    CoasterTrain       train;
    SW_REQUIRE_TRUE( train.initialize( &track, Util::makeFrictionlessParams(), 5.0f ) );
    train.setSpeed( 20.0f );
    Util::run( train, 1.0f );
    SW_EXPECT_NEAR( track.getLength(), train.getDistance(), 1.0e-4f );
    SW_EXPECT_EQUAL( 0.0f, train.getSpeed() );
    SW_EXPECT_EQUAL( 0, train.getLapCount() );
    train.setSpeed( -20.0f );
    Util::run( train, 1.0f );
    SW_EXPECT_EQUAL( 0.0f, train.getDistance() );
    SW_EXPECT_EQUAL( 0.0f, train.getSpeed() );
}

/**
 * @brief [CoasterTrainTest] 닫힌 회로는 거리를 감고, 앞으로 지나면 바퀴가 늘고 뒤로 지나면 준다. `setDistance` 는 바퀴를 지운다
 */
SW_TEST_CASE( CoasterTrainTest, ClosedTrackWrapsAndCountsLaps )
{
    const CoasterTrack track = Util::makeFlatCircle();
    SW_REQUIRE_TRUE( track.isClosed() );
    const float32 length = track.getLength();
    SW_EXPECT_NEAR( 2.0f * MathUtil::kPi * 10.0f, length, 0.1f );

    CoasterTrain train;
    SW_REQUIRE_TRUE( train.initialize( &track, Util::makeFrictionlessParams(), length - 1.0f ) );
    train.setSpeed( 4.0f );
    Util::run( train, 1.0f ); // 4 m 를 가서 시작점을 지난다
    SW_EXPECT_EQUAL( 1, train.getLapCount() );
    SW_EXPECT_NEAR( 3.0f, train.getDistance(), 0.05f );

    train.setDistance( 1.0f );
    SW_EXPECT_EQUAL( 0, train.getLapCount() );
    SW_EXPECT_NEAR( 4.0f, train.getSpeed(), 1.0e-4f ); // 속도는 그대로다
    train.setSpeed( -4.0f );
    Util::run( train, 1.0f );
    SW_EXPECT_EQUAL( -1, train.getLapCount() );
    SW_EXPECT_NEAR( length - 3.0f, train.getDistance(), 0.05f );

    train.setDistance( length + 2.0f ); // 넘는 거리는 감는다
    SW_EXPECT_NEAR( 2.0f, train.getDistance(), 1.0e-3f );
}

/**
 * @brief [CoasterTrainTest] 뒤 차량은 앞 차량에서 번호 x 간격만큼 뒤에 있고, 닫힌 회로에서는 시작점을 건너 감긴다
 */
SW_TEST_CASE( CoasterTrainTest, CarFramesFollowBehindTheFrontCar )
{
    const CoasterTrack track = Util::makeFlatCircle();
    CoasterTrain       train;
    SW_REQUIRE_TRUE( train.initialize( &track, Util::makeFrictionlessParams(), 1.0f ) );
    const float3 front    = train.getCarFrame( 0, 3.0f )._position;
    const float3 expected = track.sample( track.getLength() - 5.0f )._position; // 1 - 2 x 3
    const float3 actual   = train.getCarFrame( 2, 3.0f )._position;
    SW_EXPECT_NEAR( train.getFrame()._position._x, front._x, 1.0e-5f );
    SW_EXPECT_NEAR( train.getFrame()._position._z, front._z, 1.0e-5f );
    SW_EXPECT_NEAR( expected._x, actual._x, 1.0e-3f );
    SW_EXPECT_NEAR( expected._z, actual._z, 1.0e-3f );
}

/**
 * @brief [CoasterTrainTest] G: 수평 등속은 수직 1G 이고, 감속은 앞뒤 G 로, 오른쪽 회전의 구심 가속은 오른쪽 + 로 나타난다(v^2 / r / g)
 */
SW_TEST_CASE( CoasterTrainTest, GForceComesFromVelocityChangeAndGravity )
{
    const CoasterTrack   straight = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 500.0f ) }, false );
    CoasterPhysicsParams rolling  = Util::makeFrictionlessParams();
    rolling._rollingResistance    = 0.1f; // 감속 1 m/s^2 = 0.1G
    CoasterTrain train;
    SW_REQUIRE_TRUE( train.initialize( &straight, rolling, 10.0f ) );
    train.setSpeed( 20.0f );
    Util::run( train, 0.5f );
    SW_EXPECT_NEAR( 1.0f, train.getGForce()._vertical, 0.01f );
    SW_EXPECT_NEAR( 0.0f, train.getGForce()._lateral, 0.01f );
    SW_EXPECT_NEAR( -0.1f, train.getGForce()._longitudinal, 0.01f );

    const CoasterTrack circle = Util::makeFlatCircle();
    SW_REQUIRE_TRUE( train.initialize( &circle, Util::makeFrictionlessParams() ) );
    train.setSpeed( 10.0f );
    Util::run( train, 0.5f ); // 첫 회전 조각 안
    SW_EXPECT_NEAR( 1.0f, train.getGForce()._vertical, 0.05f );
    SW_EXPECT_NEAR( 1.0f, train.getGForce()._lateral, 0.1f );
}

/**
 * @brief [CoasterTrainTest] 분석: 구동 없는 수평에 선 열차는 정한 시간 뒤 멈춤으로 끝나고, 완주가 아니어서 흥분은 0 이다
 */
SW_TEST_CASE( CoasterTrainTest, AnalyzeReportsStallOnUndrivenTrack )
{
    const CoasterPhysicsParams params = CoasterPhysicsParams{};
    const CoasterTrack         track  = Util::makeTrack( { Util::makePiece( CoasterPieceType::Straight, 50.0f ) }, false );
    const CoasterRideStats     stats  = CoasterRideStats::analyze( track, params );
    SW_EXPECT_TRUE( stats._bStalled == SW_TRUE );
    SW_EXPECT_TRUE( stats._bCompleted == SW_FALSE );
    SW_EXPECT_NEAR( params._stallSeconds, stats._lapTime, 0.05f );
    SW_EXPECT_EQUAL( 0.0f, stats._excitement );
}

/**
 * @brief [CoasterTrainTest] 분석: 구동되는 스테이션은 멈춤으로 보지 않고 열린 트랙 끝까지 가면 완주다. 시간이 모자라면 둘 다 아니다. 점이 모자란 트랙은 돌지 않는다
 */
SW_TEST_CASE( CoasterTrainTest, AnalyzeCompletesDrivenOpenTrackWithinTime )
{
    const CoasterPhysicsParams params = CoasterPhysicsParams{};
    const CoasterTrack         track  = Util::makeTrack( { Util::makePiece( CoasterPieceType::Station, 30.0f ) }, false );
    const CoasterRideStats     stats  = CoasterRideStats::analyze( track, params );
    SW_EXPECT_TRUE( stats._bCompleted == SW_TRUE );
    SW_EXPECT_TRUE( stats._bStalled == SW_FALSE );
    SW_EXPECT_NEAR( params._stationSpeed, stats._maxSpeed, 0.2f );
    SW_EXPECT_NEAR( 30.0f / params._stationSpeed, stats._lapTime, 1.0f );
    SW_EXPECT_NEAR( 30.0f / stats._lapTime, stats._averageSpeed, 0.1f );

    const CoasterRideStats shortRun = CoasterRideStats::analyze( track, params, 1.0f );
    SW_EXPECT_TRUE( shortRun._bCompleted == SW_FALSE );
    SW_EXPECT_TRUE( shortRun._bStalled == SW_FALSE );

    const CoasterRideStats noTrack = CoasterRideStats::analyze( CoasterTrack{}, params );
    SW_EXPECT_TRUE( noTrack._bCompleted == SW_FALSE );
    SW_EXPECT_EQUAL( 0.0f, noTrack._lapTime );
}

/**
 * @brief [CoasterTrainTest] 분석: 체인으로 오르고 떨어져 돌아오는 닫힌 회로를 완주한다 - 낙하 하나, 뒤집힘 없음, 회전의 옆 G, 평가가 0 보다 크다
 */
SW_TEST_CASE( CoasterTrainTest, AnalyzeMeasuresClosedCircuit )
{
    // 스테이션 → 오르막 → 내리막 → 유턴 → 돌아오는 직선 → 유턴이 정확히 시작점으로 닫힌다(연결 곡선이 필요 없다).
    const CoasterTrack track = Util::makeTrack( { Util::makePiece( CoasterPieceType::Station, 10.0f ), Util::makePiece( CoasterPieceType::LiftHill, 30.0f, 20.0f ), Util::makePiece( CoasterPieceType::Drop, 30.0f, 20.0f ),
                                                  Util::makeTurnRight( 15.0f, MathUtil::kPi ), Util::makePiece( CoasterPieceType::Straight, 70.0f ), Util::makeTurnRight( 15.0f, MathUtil::kPi ) },
                                                true );
    SW_REQUIRE_TRUE( track.isClosed() );
    const CoasterRideStats stats = CoasterRideStats::analyze( track, CoasterPhysicsParams{} );
    SW_EXPECT_TRUE( stats._bCompleted == SW_TRUE );
    SW_EXPECT_TRUE( stats._bStalled == SW_FALSE );
    SW_EXPECT_EQUAL( 1u, stats._dropCount );
    SW_EXPECT_NEAR( 20.0f, stats._maxDropHeight, 0.5f );
    SW_EXPECT_EQUAL( 0u, stats._inversionCount );
    SW_EXPECT_NEAR( 20.0f, stats._maxHeight, 0.1f );
    SW_EXPECT_TRUE( stats._maxSpeed > 10.0f );
    SW_EXPECT_TRUE( stats._maxVerticalG > 1.0f );
    SW_EXPECT_TRUE( stats._maxLateralG > 0.2f );
    SW_EXPECT_TRUE( stats._lapTime > 0.0f );
    SW_EXPECT_TRUE( stats._excitement > 0.0f );
    SW_EXPECT_TRUE( stats._intensity > 0.0f );
}

/**
 * @brief [CoasterTrainTest] 분석: 루프 하나는 뒤집힘 하나로 센다
 */
SW_TEST_CASE( CoasterTrainTest, AnalyzeCountsLoopAsOneInversion )
{
    CoasterTrackPiece loop;
    loop._type   = CoasterPieceType::Loop;
    loop._radius = 6.0f;
    const CoasterTrack track =
        Util::makeTrack( { Util::makePiece( CoasterPieceType::Station, 5.0f ), Util::makePiece( CoasterPieceType::LiftHill, 30.0f, 25.0f ), Util::makePiece( CoasterPieceType::Drop, 30.0f, 25.0f ), loop, Util::makePiece( CoasterPieceType::Straight, 20.0f ) }, false );
    const CoasterRideStats stats = CoasterRideStats::analyze( track, CoasterPhysicsParams{} );
    SW_EXPECT_TRUE( stats._bCompleted == SW_TRUE );
    SW_EXPECT_EQUAL( 1u, stats._inversionCount );
}

/**
 * @brief [CoasterTrainTest] 평가 식: 측정값에서 손으로 계산한 값과 같고, 강도 8 넘음은 흥분을 깎으며, 못 돈 코스터는 흥분 0, 셋 다 0..10 에서 잘린다
 */
SW_TEST_CASE( CoasterTrainTest, RatingsFollowTheFormula )
{
    CoasterRideStats stats;
    stats._maxVerticalG   = 3.0f;
    stats._minVerticalG   = -1.0f;
    stats._maxLateralG    = 1.0f;
    stats._maxSpeed       = 30.0f;
    stats._airtime        = 1.0f;
    stats._inversionCount = 2;
    stats._dropCount      = 2;
    stats._maxDropHeight  = 30.0f;
    stats._length         = 400.0f;
    stats._bCompleted     = SW_TRUE;
    CoasterRideStats::computeRatings( stats, CoasterRatingWeights{} );
    SW_EXPECT_NEAR( 6.5f, stats._intensity, 1.0e-4f );  // 2 x 0.9 + 1 x 1.5 + 1 x 1.2 + 30 / 15
    SW_EXPECT_NEAR( 5.425f, stats._nausea, 1.0e-4f );   // 1.5 + 2 + 0.3 + 6.5 x 0.25
    SW_EXPECT_NEAR( 8.8f, stats._excitement, 1.0e-4f ); // 3 + 0.6 + 1.6 + 0.6 + 2 + 1

    CoasterRideStats notCompleted = stats;
    notCompleted._bCompleted      = SW_FALSE;
    CoasterRideStats::computeRatings( notCompleted, CoasterRatingWeights{} );
    SW_EXPECT_EQUAL( 0.0f, notCompleted._excitement );

    CoasterRideStats tooIntense = stats;
    tooIntense._maxVerticalG    = 6.0f; // 강도 9.2 - 흥분에서 1.2 를 깎는다
    CoasterRideStats::computeRatings( tooIntense, CoasterRatingWeights{} );
    SW_EXPECT_NEAR( 9.2f, tooIntense._intensity, 1.0e-4f );
    SW_EXPECT_NEAR( 7.6f, tooIntense._excitement, 1.0e-4f );

    CoasterRideStats extreme = stats;
    extreme._maxVerticalG    = 50.0f;
    extreme._maxLateralG     = 50.0f;
    CoasterRideStats::computeRatings( extreme, CoasterRatingWeights{} );
    SW_EXPECT_EQUAL( 10.0f, extreme._intensity );
    SW_EXPECT_EQUAL( 10.0f, extreme._nausea );
    SW_EXPECT_EQUAL( 0.0f, extreme._excitement ); // 강도가 지나치면 아래로 잘린다
}

/**
 * @brief [CoasterTrainTest] 평가 계수는 데이터다: 계수를 바꾸면 같은 측정값의 평가가 그만큼 바뀌고, 위 끝도 계수를 따른다
 */
SW_TEST_CASE( CoasterTrainTest, RatingsFollowTheWeights )
{
    CoasterRideStats stats;
    stats._maxLateralG = 1.0f;
    stats._maxSpeed    = 30.0f;
    stats._bCompleted  = SW_TRUE;

    CoasterRatingWeights weights;
    weights._intensityPerLateralG = 2.0f;  // 기본 1.2
    weights._intensitySpeedScale  = 10.0f; // 기본 15
    CoasterRideStats::computeRatings( stats, weights );
    SW_EXPECT_NEAR( 5.0f, stats._intensity, 1.0e-4f ); // 1 x 2 + 30 / 10

    weights._maxRating = 4.0f;
    CoasterRideStats::computeRatings( stats, weights );
    SW_EXPECT_EQUAL( 4.0f, stats._intensity );
}
