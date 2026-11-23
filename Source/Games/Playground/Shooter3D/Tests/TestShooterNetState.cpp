// ShooterNetStateTest — 슈터의 복제 상태(적 엔티티 · 소유자 상태)는 칸 선언(`PROPERTY( Replicated, … )`)이 바이트를 정한다.
// 쓰고 읽으면 양자화 간격 안에서 같은 값이고, 선언 범위 밖 · 깨진 바이트는 읽기가 거절한다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"

using namespace sw;

/** @brief [ShooterNetStateTest] 적 상태는 자리 1 cm · 요 0.001 라디안 간격으로 돌아오고, 단계가 선언 범위 밖이면 쓰지 않는다. */
SW_TEST_CASE( ShooterNetStateTest, EnemyStateRoundTripsWithinQuantizationStep )
{
    ShooterEnemyNetState sent;
    sent._position       = float3{ 12.345f, 0.5f, -7.891f };
    sent._yaw            = 1.2345f;
    sent._phase          = 3;
    sent._kindIndex      = 255;
    sent._appearanceSeed = 0xABCDEF01u;
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( sent, writer ) );
    SW_EXPECT_TRUE( writer.getBytes().size() < 4 * 4 + 1 + 2 + 5 ); // 손 코덱(실수 넷 · 가변 정수 셋 = 24 바이트)보다 작다

    BitReader            reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    ShooterEnemyNetState received;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( ( received._position - sent._position ).getLength() <= 0.01f );
    SW_EXPECT_TRUE( MathUtil::abs( received._yaw - sent._yaw ) <= 0.001f );
    SW_EXPECT_EQUAL( sent._phase, received._phase );
    SW_EXPECT_EQUAL( sent._kindIndex, received._kindIndex );
    SW_EXPECT_EQUAL( sent._appearanceSeed, received._appearanceSeed );

    ShooterEnemyNetState badPhase = sent;
    badPhase._phase               = 5; // `ShooterEnemyPhase::Dying` 다음
    BitWriter badWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( badPhase, badWriter ) );
}

/** @brief [ShooterNetStateTest] 소유자 상태는 걸음 상태 · 맞힌 발 수 · 처치 수 · 체력을 한 선언으로 왕복하고, 잘린 바이트는 읽지 않는다. */
SW_TEST_CASE( ShooterNetStateTest, OwnerStateRoundTripsAsOneDeclaration )
{
    ShooterMoveState move;
    move._position      = float3{ 3.0f, 1.5f, -2.25f };
    move._verticalSpeed = -4.5f;
    move._bOnGround     = SW_FALSE;
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( ShooterNetPawnComponent::writeOwnerState( move, 17u, 9u, 62.5f, bytes ) );

    ShooterMoveState readMove;
    uint32           hitCount  = 0;
    uint32           killCount = 0;
    float32          health    = 0.0f;
    SW_REQUIRE_TRUE( ShooterNetPawnComponent::readOwnerState( bytes, readMove, hitCount, killCount, health ) );
    SW_EXPECT_TRUE( readMove._position == move._position );
    SW_EXPECT_EQUAL( move._verticalSpeed, readMove._verticalSpeed );
    SW_EXPECT_EQUAL( move._bOnGround, readMove._bOnGround );
    SW_EXPECT_EQUAL( uint32{ 17 }, hitCount );
    SW_EXPECT_EQUAL( uint32{ 9 }, killCount );
    SW_EXPECT_EQUAL( 62.5f, health );

    const vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>( bytes.size() / 2 ) );
    SW_EXPECT_FALSE( ShooterNetPawnComponent::readOwnerState( truncated, readMove, hitCount, killCount, health ) );
}
