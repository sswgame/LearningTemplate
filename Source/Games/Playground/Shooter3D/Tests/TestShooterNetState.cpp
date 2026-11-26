// ShooterNetStateTest — 슈터의 복제 상태(적 엔티티 · 소유자 상태)는 칸 선언(`PROPERTY( Replicated, … )`)이 바이트를 정한다.
// 쓰고 읽으면 양자화 간격 안에서 같은 값이고, 선언 범위 밖 · 깨진 바이트는 읽기가 거절한다(nogpu, 에셋 없음).
// ShooterNetPawnTest — 네트워크 몸이 세션의 수명을 따른다(연결 없음).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Foundation/Service/Tests/SceneNetLinkTestBinding.h"

#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
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

/** @brief [ShooterNetStateTest] 남의 몸 상태는 양자화 간격 안에서 돌아오고, 없는 무기 슬롯은 쓰지 않는다. 몸과 적의 엔티티 번호는 겹치지 않는다. */
SW_TEST_CASE( ShooterNetStateTest, PawnStateRoundTripsWithinQuantizationStep )
{
    ShooterPawnNetState sent;
    sent._position    = float3{ 4.0f, 0.0f, -16.0f };
    sent._yaw         = -2.5f;
    sent._weaponIndex = 2;
    sent._bAlive      = SW_FALSE;
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( sent, writer ) );
    BitReader           reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    ShooterPawnNetState received;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( ( received._position - sent._position ).getLength() <= 0.01f );
    SW_EXPECT_TRUE( MathUtil::abs( received._yaw - sent._yaw ) <= 0.001f );
    SW_EXPECT_EQUAL( sent._weaponIndex, received._weaponIndex );
    SW_EXPECT_EQUAL( sent._bAlive, received._bAlive );

    sent._weaponIndex = 3;
    BitWriter badWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( sent, badWriter ) );
    SW_EXPECT_TRUE( ShooterNetPawnComponent::kPawnEntityID + ShooterNetPawnComponent::kMaxSeat <= ShooterNetPawnComponent::kEnemyEntityIDBase );
}

/** @brief [ShooterNetStateTest] 소유자 상태는 걸음 상태 · 서버 판정 결과 · 내 몸의 엔티티를 한 선언으로 왕복하고, 잘린 바이트는 읽지 않는다. */
SW_TEST_CASE( ShooterNetStateTest, OwnerStateRoundTripsAsOneDeclaration )
{
    ShooterOwnerNetState sent;
    sent._move._position      = float3{ 3.0f, 1.5f, -2.25f };
    sent._move._verticalSpeed = -4.5f;
    sent._move._bOnGround     = SW_FALSE;
    sent._confirmedHitCount   = 17;
    sent._killCount           = 9;
    sent._pawnEntityID        = ShooterNetPawnComponent::kPawnEntityID + 1u;
    sent._bOut                = SW_TRUE;
    sent._health              = 62.5f;
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( ShooterNetPawnComponent::writeOwnerState( sent, bytes ) );

    ShooterOwnerNetState received;
    SW_REQUIRE_TRUE( ShooterNetPawnComponent::readOwnerState( bytes, received ) );
    SW_EXPECT_TRUE( received._move._position == sent._move._position );
    SW_EXPECT_EQUAL( sent._move._verticalSpeed, received._move._verticalSpeed );
    SW_EXPECT_EQUAL( sent._move._bOnGround, received._move._bOnGround );
    SW_EXPECT_EQUAL( sent._confirmedHitCount, received._confirmedHitCount );
    SW_EXPECT_EQUAL( sent._killCount, received._killCount );
    SW_EXPECT_EQUAL( sent._pawnEntityID, received._pawnEntityID );
    SW_EXPECT_EQUAL( sent._bOut, received._bOut );
    SW_EXPECT_EQUAL( sent._health, received._health );

    const vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>( bytes.size() / 2 ) );
    SW_EXPECT_FALSE( ShooterNetPawnComponent::readOwnerState( truncated, received ) );
}

/** @brief [ShooterNetPawnTest] 플레이 중에 세션이 닫히면 역할을 놓고 몸이 혼자 하는 길로 돌아온다 — 아무도 옮기지 않는 몸(netDriven)이 남지 않는다. */
SW_TEST_CASE( ShooterNetPawnTest, ClosedSessionReturnsTheBodyToLocalMovement )
{
    GameObjectManager                   manager;
    SceneNetLinkTestBinding             link( manager, NetLinkRole::Client );
    GameObject* const                   pPlayer   = manager.createGameObject( hashed_string( "Player" ) );
    ShooterBodyMovementComponent* const pMovement = pPlayer->addComponent<ShooterBodyMovementComponent>();
    ShooterNetPawnComponent* const      pNetPawn  = pPlayer->addComponent<ShooterNetPawnComponent>();
    manager.beginPlay();
    SW_REQUIRE_TRUE( pNetPawn->isClient() );
    SW_EXPECT_TRUE( pMovement->isNetDriven() );

    link.closeSession();
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_FALSE( pNetPawn->isActive() );
    SW_EXPECT_FALSE( pMovement->isNetDriven() );
    manager.endPlay();
}
