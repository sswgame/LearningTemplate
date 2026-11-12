#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureTargeting.h"

#include <cstring>
#include <limits>

// AdventureTargetingTest — Z 주목 상태 기계: 잡기와 평행 주목, 옆걸음과 입력 경계, 회피 시작과 입력 잠금, 무적 시간, 대상 놓침, 이동 방향 변환, 상태 왕복.
// 결함: 버튼을 떼면 진행 중인 회피와 무적이 끊겼다(회피 중 다시 눌러도 상태가 덮였다) · NaN 표적 위치를 받았다. 키트 GF_ActionAdventure(nogpu).

namespace
{
    const sw::float3 kEye{ 0.0f, 0.0f, 0.0f };
    const sw::float3 kForward{ 0.0f, 0.0f, 1.0f };

    constexpr uint64  kTargetFront    = 7;
    constexpr uint64  kTargetRight    = 8;
    constexpr float32 kAimStep        = 0.125f; // 2 의 거듭제곱 분수라 시간 더하기가 정확하다
    constexpr float32 kHoverTolerance = 1.0e-4f;

    struct AdventureTargetingTestUtil
    {
        /** @brief 회피 0.5 초 · 옆 뛰기 0.25 초 · 무적 0.25 초로 시작합니다. */
        static void setUp( sw::AdventureTargeting& targeting )
        {
            sw::AdventureTargetingSettings settings;
            settings._backflipDuration  = 0.5f;
            settings._sideHopDuration   = 0.25f;
            settings._evadeInvulnerable = 0.25f;
            targeting.initialize( settings );
        }

        static sw::vector<sw::LockOnCandidate> makeFrontTarget()
        {
            sw::LockOnCandidate front;
            front._id       = kTargetFront;
            front._position = sw::float3{ 0.0f, 0.0f, 10.0f };
            return { front };
        }

        static sw::vector<sw::LockOnCandidate> makeTwoTargets()
        {
            sw::vector<sw::LockOnCandidate> listCandidate = makeFrontTarget();
            sw::LockOnCandidate             right;
            right._id       = kTargetRight;
            right._position = sw::float3{ 5.0f, 0.0f, 10.0f };
            listCandidate.push_back( right );
            return listCandidate;
        }

        static sw::AdventureTargetingState tick( sw::AdventureTargeting& targeting, const sw::vector<sw::LockOnCandidate>& listCandidate, const sw::float2& move = sw::float2{}, bool bJump = false )
        {
            return targeting.update( kEye, listCandidate, move, bJump, kAimStep );
        }
    };
} // namespace

/**
 * @brief [AdventureTargetingTest] 대상이 없으면 평행 주목으로 옆걸음은 되지만 회피는 없고, 떼면 자유로 돌아간다
 */
SW_TEST_CASE( AdventureTargetingTest, ParallelModeHasStrafeButNoDodge )
{
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );
    const sw::vector<sw::LockOnCandidate> listNone;
    SW_EXPECT_FALSE( targeting.press( kEye, kForward, listNone ) );
    SW_EXPECT_TRUE( targeting.getState() == sw::AdventureTargetingState::Parallel );
    SW_EXPECT_TRUE( targeting.isHeld() );
    SW_EXPECT_FALSE( targeting.hasTarget() );

    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listNone, sw::float2{ 1.0f, 0.0f } ) == sw::AdventureTargetingState::StrafeRight );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listNone, sw::float2{ -1.0f, 0.0f } ) == sw::AdventureTargetingState::StrafeLeft );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listNone, sw::float2{ 1.0f, 0.0f }, true ) == sw::AdventureTargetingState::StrafeRight );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listNone, sw::float2{ 0.0f, -1.0f }, true ) == sw::AdventureTargetingState::Parallel );
    SW_EXPECT_FALSE( targeting.isEvading() );

    targeting.release();
    SW_EXPECT_TRUE( targeting.getState() == sw::AdventureTargetingState::Free );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listNone, sw::float2{ 1.0f, 0.0f } ) == sw::AdventureTargetingState::Free );
    SW_EXPECT_EQUAL( uint64( 0 ), targeting.cycleTarget( kEye, kForward, listNone, 1 ) );
}

/**
 * @brief [AdventureTargetingTest] 입력 경계: 데드존 값 자체는 입력이 아니고, 옆은 앞뒤보다 커야 하며, 뒤와 옆이 같으면 뒤다. 앞으로 뛰는 것은 회피가 아니다
 */
SW_TEST_CASE( AdventureTargetingTest, InputBoundariesPickTheDodge )
{
    const sw::vector<sw::LockOnCandidate> listTarget = AdventureTargetingTestUtil::makeFrontTarget();
    struct Case
    {
        sw::float2                  _move;
        sw::AdventureTargetingState _state;
    };
    const Case arrCase[] = {
        { sw::float2{ 0.0f, -0.3f },       sw::AdventureTargetingState::Locked},
        {  sw::float2{ 0.3f, 0.0f },       sw::AdventureTargetingState::Locked},
        {sw::float2{ 0.0f, -0.31f },     sw::AdventureTargetingState::Backflip},
        { sw::float2{ 0.31f, 0.0f }, sw::AdventureTargetingState::SideHopRight},
        {sw::float2{ -0.31f, 0.0f },  sw::AdventureTargetingState::SideHopLeft},
        { sw::float2{ 0.5f, -0.5f },     sw::AdventureTargetingState::Backflip},
        { sw::float2{ 0.6f, -0.5f }, sw::AdventureTargetingState::SideHopRight},
        {  sw::float2{ 0.0f, 1.0f },       sw::AdventureTargetingState::Locked},
        {  sw::float2{ 0.5f, 0.5f },       sw::AdventureTargetingState::Locked},
    };
    for ( const Case& testCase : arrCase )
    {
        sw::AdventureTargeting targeting;
        AdventureTargetingTestUtil::setUp( targeting );
        SW_REQUIRE_TRUE( targeting.press( kEye, kForward, listTarget ) );
        SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, testCase._move, true ) == testCase._state );
    }

    // 뛰지 않으면 옆은 옆걸음이고 뒤는 그대로 선다.
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );
    SW_REQUIRE_TRUE( targeting.press( kEye, kForward, listTarget ) );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sw::float2{ 0.0f, -1.0f } ) == sw::AdventureTargetingState::Locked );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sw::float2{ -1.0f, 0.0f } ) == sw::AdventureTargetingState::StrafeLeft );
}

/**
 * @brief [AdventureTargetingTest] 회피 동안은 입력을 받지 않고 상태가 유지되며, 무적은 먼저 끝나고, 회피가 끝나는 틱에 입력을 다시 받는다
 */
SW_TEST_CASE( AdventureTargetingTest, EvadeLocksInputAndInvulnerabilityEndsFirst )
{
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );
    const sw::vector<sw::LockOnCandidate> listTarget = AdventureTargetingTestUtil::makeFrontTarget();
    SW_REQUIRE_TRUE( targeting.press( kEye, kForward, listTarget ) );

    SW_REQUIRE_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sw::float2{ 0.0f, -1.0f }, true ) == sw::AdventureTargetingState::Backflip );
    SW_EXPECT_TRUE( targeting.isEvading() );
    SW_EXPECT_TRUE( targeting.isInvulnerable() );

    const sw::float2 sideInput{ 1.0f, 0.0f };
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sideInput, true ) == sw::AdventureTargetingState::Backflip ); // 0.125
    SW_EXPECT_TRUE( targeting.isInvulnerable() );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sideInput, true ) == sw::AdventureTargetingState::Backflip ); // 0.25 — 무적 끝
    SW_EXPECT_FALSE( targeting.isInvulnerable() );
    SW_EXPECT_TRUE( targeting.isEvading() );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sideInput, true ) == sw::AdventureTargetingState::Backflip ); // 0.375

    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sideInput, true ) == sw::AdventureTargetingState::SideHopRight ); // 0.5 — 끝난 틱에 다음 회피
    SW_EXPECT_TRUE( targeting.isEvading() );
    SW_EXPECT_TRUE( targeting.isInvulnerable() );
}

/**
 * @brief [AdventureTargetingTest] 결함: 회피 중 버튼을 떼면 공중에서 끊기고 무적까지 잃었다. 대상은 놓되 회피와 무적은 끝까지 가고 그 뒤 자유가 된다
 */
SW_TEST_CASE( AdventureTargetingTest, ReleaseDoesNotCancelARunningEvade )
{
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );
    const sw::vector<sw::LockOnCandidate> listTarget = AdventureTargetingTestUtil::makeFrontTarget();
    SW_REQUIRE_TRUE( targeting.press( kEye, kForward, listTarget ) );
    SW_REQUIRE_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sw::float2{ 0.0f, -1.0f }, true ) == sw::AdventureTargetingState::Backflip );

    targeting.release();
    SW_EXPECT_FALSE( targeting.isHeld() );
    SW_EXPECT_FALSE( targeting.hasTarget() );
    SW_EXPECT_TRUE( targeting.getState() == sw::AdventureTargetingState::Backflip );
    SW_EXPECT_TRUE( targeting.isEvading() );
    SW_EXPECT_TRUE( targeting.isInvulnerable() );

    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget ) == sw::AdventureTargetingState::Backflip );
    SW_EXPECT_TRUE( targeting.isInvulnerable() );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget ) == sw::AdventureTargetingState::Backflip );
    SW_EXPECT_FALSE( targeting.isInvulnerable() );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget ) == sw::AdventureTargetingState::Backflip );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget ) == sw::AdventureTargetingState::Free );
    SW_EXPECT_FALSE( targeting.isEvading() );
}

/**
 * @brief [AdventureTargetingTest] 결함: 회피 중 다시 누르면 상태가 잠금으로 덮였다. 대상은 잡되 상태는 회피 그대로이고, 끝난 틱에 잠금이 된다. 초기화는 회피까지 지운다
 */
SW_TEST_CASE( AdventureTargetingTest, PressDuringEvadeKeepsTheEvadeState )
{
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );
    const sw::vector<sw::LockOnCandidate> listTarget = AdventureTargetingTestUtil::makeFrontTarget();
    SW_REQUIRE_TRUE( targeting.press( kEye, kForward, listTarget ) );
    SW_REQUIRE_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sw::float2{ -1.0f, 0.0f }, true ) == sw::AdventureTargetingState::SideHopLeft );
    targeting.release();

    SW_EXPECT_TRUE( targeting.press( kEye, kForward, listTarget ) );
    SW_EXPECT_TRUE( targeting.hasTarget() );
    SW_EXPECT_TRUE( targeting.getState() == sw::AdventureTargetingState::SideHopLeft );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget ) == sw::AdventureTargetingState::SideHopLeft );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget ) == sw::AdventureTargetingState::Locked );

    SW_REQUIRE_TRUE( AdventureTargetingTestUtil::tick( targeting, listTarget, sw::float2{ 0.0f, -1.0f }, true ) == sw::AdventureTargetingState::Backflip );
    AdventureTargetingTestUtil::setUp( targeting );
    SW_EXPECT_TRUE( targeting.getState() == sw::AdventureTargetingState::Free );
    SW_EXPECT_FALSE( targeting.isEvading() );
    SW_EXPECT_FALSE( targeting.isInvulnerable() );
}

/**
 * @brief [AdventureTargetingTest] 대상이 사라지거나, 너무 멀어지거나, 오래 가려지면 평행 주목으로 떨어진다. 회피 중에 놓쳐도 회피는 끝까지 간다
 */
SW_TEST_CASE( AdventureTargetingTest, LosingTheTargetFallsBackToParallel )
{
    const sw::vector<sw::LockOnCandidate> listTarget = AdventureTargetingTestUtil::makeFrontTarget();
    const sw::vector<sw::LockOnCandidate> listNone;

    sw::AdventureTargeting gone;
    AdventureTargetingTestUtil::setUp( gone );
    SW_REQUIRE_TRUE( gone.press( kEye, kForward, listTarget ) );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( gone, listNone ) == sw::AdventureTargetingState::Parallel );
    SW_EXPECT_FALSE( gone.hasTarget() );

    sw::vector<sw::LockOnCandidate> listFar = listTarget;
    listFar[0]._position                    = sw::float3{ 0.0f, 0.0f, 32.5f }; // 유지 거리 32 밖
    sw::AdventureTargeting distant;
    AdventureTargetingTestUtil::setUp( distant );
    SW_REQUIRE_TRUE( distant.press( kEye, kForward, listTarget ) );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( distant, listFar ) == sw::AdventureTargetingState::Parallel );

    sw::vector<sw::LockOnCandidate> listHidden = listTarget;
    listHidden[0]._bVisible                    = SW_FALSE;
    sw::AdventureTargeting hidden;
    AdventureTargetingTestUtil::setUp( hidden );
    SW_REQUIRE_TRUE( hidden.press( kEye, kForward, listTarget ) );
    SW_EXPECT_TRUE( hidden.update( kEye, listHidden, sw::float2{}, false, 1.0f ) == sw::AdventureTargetingState::Locked ); // 유예 1 초는 넘지 않았다
    SW_EXPECT_TRUE( hidden.update( kEye, listHidden, sw::float2{}, false, 0.125f ) == sw::AdventureTargetingState::Parallel );

    sw::AdventureTargeting midEvade;
    AdventureTargetingTestUtil::setUp( midEvade );
    SW_REQUIRE_TRUE( midEvade.press( kEye, kForward, listTarget ) );
    SW_REQUIRE_TRUE( AdventureTargetingTestUtil::tick( midEvade, listTarget, sw::float2{ 1.0f, 0.0f }, true ) == sw::AdventureTargetingState::SideHopRight );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( midEvade, listNone ) == sw::AdventureTargetingState::SideHopRight );
    SW_EXPECT_FALSE( midEvade.hasTarget() );
    SW_EXPECT_TRUE( AdventureTargetingTestUtil::tick( midEvade, listNone ) == sw::AdventureTargetingState::Parallel );
}

/**
 * @brief [AdventureTargetingTest] 대상 넘기기는 오른쪽 · 왼쪽으로 옮기고 표적 위치를 갱신하며, 그쪽에 없으면 그대로다. 대상이 움직이면 위치를 따라간다
 */
SW_TEST_CASE( AdventureTargetingTest, CycleAndTrackTheTarget )
{
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );
    sw::vector<sw::LockOnCandidate> listCandidate = AdventureTargetingTestUtil::makeTwoTargets();
    SW_REQUIRE_TRUE( targeting.press( kEye, kForward, listCandidate ) );
    SW_EXPECT_EQUAL( kTargetFront, targeting.getTarget() );
    SW_EXPECT_NEAR( 10.0f, targeting.getTargetPosition()._z, kHoverTolerance );

    SW_EXPECT_EQUAL( kTargetRight, targeting.cycleTarget( kEye, kForward, listCandidate, 1 ) );
    SW_EXPECT_NEAR( 5.0f, targeting.getTargetPosition()._x, kHoverTolerance );
    SW_EXPECT_EQUAL( kTargetRight, targeting.cycleTarget( kEye, kForward, listCandidate, 1 ) );
    SW_EXPECT_EQUAL( kTargetFront, targeting.cycleTarget( kEye, kForward, listCandidate, -1 ) );

    listCandidate[0]._position = sw::float3{ 2.0f, 0.0f, 12.0f };
    (void)AdventureTargetingTestUtil::tick( targeting, listCandidate );
    SW_EXPECT_NEAR( 12.0f, targeting.getTargetPosition()._z, kHoverTolerance );
}

/**
 * @brief [AdventureTargetingTest] 이동 방향: 잡지 않으면 카메라 앞(높이는 뺀다), 잡으면 대상 쪽이 앞이라 옆 입력이 접선이다. 앞이 정해지지 않으면 0 이다
 */
SW_TEST_CASE( AdventureTargetingTest, MoveDirectionFollowsCameraOrTarget )
{
    sw::AdventureTargeting targeting;
    AdventureTargetingTestUtil::setUp( targeting );

    sw::float3 direction = targeting.computeMoveDirection( sw::float3{}, sw::float3{ 0.0f, 5.0f, 2.0f }, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, direction._x, kHoverTolerance );
    SW_EXPECT_NEAR( 0.0f, direction._y, kHoverTolerance );
    SW_EXPECT_NEAR( 0.0f, direction._z, kHoverTolerance );
    direction = targeting.computeMoveDirection( sw::float3{}, kForward, sw::float2{ 0.0f, 0.5f } );
    SW_EXPECT_NEAR( 0.5f, direction._z, kHoverTolerance );

    direction = targeting.computeMoveDirection( sw::float3{}, sw::float3{ 0.0f, 1.0f, 0.0f }, sw::float2{ 1.0f, 1.0f } ); // 카메라가 바로 위를 본다
    SW_EXPECT_NEAR( 0.0f, direction.getLength(), kHoverTolerance );

    sw::LockOnCandidate east;
    east._id       = kTargetFront;
    east._position = sw::float3{ 10.0f, 3.0f, 0.0f };
    const sw::vector<sw::LockOnCandidate> listEast{ east };
    SW_REQUIRE_TRUE( targeting.press( kEye, sw::float3{ 1.0f, 0.0f, 0.0f }, listEast ) );
    direction = targeting.computeMoveDirection( sw::float3{}, kForward, sw::float2{ 1.0f, 0.0f } ); // 앞이 +X 면 오른쪽은 -Z
    SW_EXPECT_NEAR( 0.0f, direction._x, kHoverTolerance );
    SW_EXPECT_NEAR( -1.0f, direction._z, kHoverTolerance );
    direction = targeting.computeMoveDirection( sw::float3{}, kForward, sw::float2{ 0.0f, 1.0f } );
    SW_EXPECT_NEAR( 1.0f, direction._x, kHoverTolerance );
    direction = targeting.computeMoveDirection( sw::float3{ 10.0f, 0.0f, 0.0f }, kForward, sw::float2{ 1.0f, 0.0f } ); // 대상 위에 섰다
    SW_EXPECT_NEAR( 0.0f, direction.getLength(), kHoverTolerance );
}

/**
 * @brief [AdventureTargetingTest] 회피 중에 쓰고 읽어도 원본과 같이 끝난다
 */
SW_TEST_CASE( AdventureTargetingTest, StateRoundTripResumesTheEvade )
{
    sw::AdventureTargeting original;
    AdventureTargetingTestUtil::setUp( original );
    const sw::vector<sw::LockOnCandidate> listTarget = AdventureTargetingTestUtil::makeFrontTarget();
    SW_REQUIRE_TRUE( original.press( kEye, kForward, listTarget ) );
    SW_REQUIRE_TRUE( AdventureTargetingTestUtil::tick( original, listTarget, sw::float2{ 0.0f, -1.0f }, true ) == sw::AdventureTargetingState::Backflip );

    sw::Archive written;
    original.writeState( written );
    sw::AdventureTargeting restored;
    AdventureTargetingTestUtil::setUp( restored );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( kTargetFront, restored.getTarget() );
    SW_EXPECT_TRUE( restored.isHeld() );
    SW_EXPECT_TRUE( restored.isEvading() );
    SW_EXPECT_NEAR( 10.0f, restored.getTargetPosition()._z, kHoverTolerance );

    for ( int32 tick = 0; tick < 6; ++tick )
    {
        const sw::AdventureTargetingState expected = AdventureTargetingTestUtil::tick( original, listTarget );
        const sw::AdventureTargetingState actual   = AdventureTargetingTestUtil::tick( restored, listTarget );
        SW_EXPECT_TRUE( expected == actual );
        SW_EXPECT_TRUE( original.isInvulnerable() == restored.isInvulnerable() );
    }
}

/**
 * @brief [AdventureTargetingTest] 결함: NaN 표적 위치를 받아 이동 방향이 NaN 이 됐다. 모르는 상태 · 깨진 표시 · 음수 숨은 시간 · 잘린 바이트와 함께 거절하고 그대로 둔다
 */
SW_TEST_CASE( AdventureTargetingTest, ReadRejectsBrokenBytes )
{
    sw::AdventureTargeting source;
    AdventureTargetingTestUtil::setUp( source );
    sw::Archive written;
    source.writeState( written );
    // 표적(8) 숨은 시간(4) 표적 위치(12) 회피(4) 무적(4) 상태(1) 눌림(1) = 34 바이트.
    const size_t size = static_cast<size_t>( written.getSize() );
    SW_REQUIRE_EQUAL( size_t( 34 ), size );

    sw::AdventureTargeting target;
    AdventureTargetingTestUtil::setUp( target );
    const auto expectRejected = [&]( size_t offset, auto value )
    {
        sw::vector<uint8> listByte( written.getData(), written.getData() + size );
        std::memcpy( listByte.data() + offset, &value, sizeof( value ) );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_TRUE( target.getState() == sw::AdventureTargetingState::Free );
        SW_EXPECT_FALSE( target.isHeld() );
    };
    const float32 nan = std::numeric_limits<float32>::quiet_NaN();
    expectRejected( 8, -1.0f );
    expectRejected( 12, nan );
    expectRejected( 16, std::numeric_limits<float32>::infinity() );
    expectRejected( 20, nan );
    expectRejected( 24, nan );
    expectRejected( 28, nan );
    expectRejected( 32, static_cast<uint8>( 200 ) );
    expectRejected( 33, static_cast<uint8>( 2 ) );

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
}
