// DestructionNetStateTest — 파괴 복제의 이벤트 · 자세 바이트는 칸 선언(`DestructionEventNetState` · `DestructionExactPoseNetState` · `DestructionMovingPoseNetState`)이 정한다.
// 이벤트는 비트 그대로 왕복하고(같은 이벤트열이 같은 상태), 움직이는 자세는 양자화 간격 안에서 돌아오며, 모르는 피해 종류 · NaN 은 쓰지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"

#include <limits>

using namespace sw;

/** @brief [DestructionNetStateTest] 이벤트는 실수까지 비트 그대로 돌아오고, 등록 밖 피해 종류와 NaN 은 쓰지 않는다. */
SW_TEST_CASE( DestructionNetStateTest, EventRoundTripsBitExactAndRejectsUnknownKind )
{
    DestructionEventNetState sent;
    sent._kind      = static_cast<uint8>( DestructionDamageKind::Impact );
    sent._leafHint  = DestructionDamageEvent::kNoLeafHint;
    sent._groupID   = 300;
    sent._position  = float3{ 0.1f, -2.3456789f, 1.0e-7f };
    sent._direction = float3{ 0.0f, 0.70710677f, -0.70710677f };
    sent._strain    = 12.5f;
    sent._radius    = 0.0f;
    sent._impulse   = 4321.125f;
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( sent, writer ) );

    BitReader                reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    DestructionEventNetState received;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( sent._kind, received._kind );
    SW_EXPECT_EQUAL( sent._leafHint, received._leafHint );
    SW_EXPECT_EQUAL( sent._groupID, received._groupID );
    SW_EXPECT_TRUE( received._position == sent._position ); // 양자화하지 않는다 — 비트 그대로
    SW_EXPECT_TRUE( received._direction == sent._direction );
    SW_EXPECT_EQUAL( sent._strain, received._strain );
    SW_EXPECT_EQUAL( sent._impulse, received._impulse );

    DestructionEventNetState unknownKind = sent;
    unknownKind._kind                    = static_cast<uint8>( DestructionDamageKind::Count );
    BitWriter unknownWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( unknownKind, unknownWriter ) );

    DestructionEventNetState notANumber = sent;
    notANumber._strain                  = std::numeric_limits<float32>::quiet_NaN();
    BitWriter nanWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( notANumber, nanWriter ) );
}

/**
 * @brief [DestructionNetStateTest] 움직이는 자세는 위치 1 mm · 회전 1/32767 간격으로 돌아온다. 선언 범위의 경계는 `kPositionRange` 와 같다 —
 *        경계 위 값은 그대로, 경계 밖 값은 경계로 잘려 온다(보내는 쪽이 `kPositionRange` 로 거르는 까닭).
 */
SW_TEST_CASE( DestructionNetStateTest, MovingPoseQuantizesWithinStepAndRangeMatchesConstant )
{
    const float32                 range = DestructionMovingPoseNetState::kPositionRange;
    DestructionMovingPoseNetState sent;
    sent._position = float3{ 12.3456f, -0.5f, range };
    sent._rotation = quaternion{ 0.0f, 0.38268343f, 0.0f, 0.92387953f };
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( sent, writer ) );

    BitReader                     reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    DestructionMovingPoseNetState received;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( ( received._position - sent._position ).getLength() <= 0.002f );
    SW_EXPECT_TRUE( MathUtil::abs( received._position._z - range ) <= 0.001f ); // 경계 위 값은 잘리지 않는다
    SW_EXPECT_TRUE( MathUtil::abs( received._rotation._y - sent._rotation._y ) <= 1.0f / 32767.0f );
    SW_EXPECT_TRUE( MathUtil::abs( received._rotation._w - sent._rotation._w ) <= 1.0f / 32767.0f );

    DestructionMovingPoseNetState outside;
    outside._position._x = range + 1.0f;
    BitWriter outsideWriter;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( outside, outsideWriter ) );
    BitReader                     outsideReader( outsideWriter.getBytes().data(), static_cast<int32>( outsideWriter.getBytes().size() ) );
    DestructionMovingPoseNetState clamped;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( outsideReader, clamped, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( MathUtil::abs( clamped._position._x - range ) <= 0.001f ); // 선언 `Max` 가 `kPositionRange` 다

    DestructionExactPoseNetState infinite;
    infinite._position._y = std::numeric_limits<float32>::infinity();
    BitWriter infiniteWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( infinite, infiniteWriter ) );
}
