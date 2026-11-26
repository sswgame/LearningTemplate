// MMOEntityHeaderTest — MMO 엔티티 머리(`MMOEntityEnterHeader` · `MMOEntityUpdateHeader`)는 칸 선언이 바이트를 정한다.
// 왕복하면 비트 그대로 같고, 자리가 NaN · 선언 범위 밖이면 쓰기 · 읽기가 거절하며 서버는 그런 엔티티를 받지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

#include <limits>

using namespace sw;

/** @brief [MMOEntityHeaderTest] 새로 보임 머리는 id · 자리 · 타입 · 정적 id 가 그대로 돌아오고, 선언 범위 밖 자리는 쓰지 않는다. */
SW_TEST_CASE( MMOEntityHeaderTest, EnterHeaderRoundTripsAndRejectsOutOfRangePosition )
{
    MMOEntityEnterHeader sent;
    sent._header._entityID = 70000;
    sent._header._position = float3{ -1234.5f, 8.25f, 99999.0f };
    sent._typeID           = 3;
    sent._staticID         = 0x0123456789ABCDEFull;
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( sent, writer ) );

    BitReader            reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    MMOEntityEnterHeader received;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( sent._header._entityID, received._header._entityID );
    SW_EXPECT_TRUE( received._header._position == sent._header._position ); // 양자화하지 않는다 — 비트 그대로
    SW_EXPECT_EQUAL( sent._typeID, received._typeID );
    SW_EXPECT_EQUAL( sent._staticID, received._staticID );

    MMOEntityUpdateHeader far;
    far._position._x = 2000000.0f; // 선언 범위(±1,000,000 m) 밖
    BitWriter farWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( far, farWriter ) );
    MMOEntityUpdateHeader notANumber;
    notANumber._position._y = std::numeric_limits<float32>::quiet_NaN();
    BitWriter nanWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( notANumber, nanWriter ) );
}

/** @brief [MMOEntityHeaderTest] 갱신 머리 자리에 NaN 비트를 실은 바이트는 읽기가 거절하고, 서버는 그런 자리의 엔티티를 받지 않는다. */
SW_TEST_CASE( MMOEntityHeaderTest, NonFinitePositionIsRejectedOnBothSides )
{
    // 머리의 바이트 — 엔티티 id(가변 길이) 뒤에 자리 셋(원본 비트). 받는 쪽이 깨진 바이트를 받는 경우를 만든다.
    BitWriter corrupt;
    corrupt.writeVarUint( 5 );
    corrupt.writeFloat( std::numeric_limits<float32>::quiet_NaN() );
    corrupt.writeFloat( 0.0f );
    corrupt.writeFloat( 0.0f );
    BitReader             reader( corrupt.getBytes().data(), static_cast<int32>( corrupt.getBytes().size() ) );
    MMOEntityUpdateHeader received;
    SW_EXPECT_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) != FixedReadResult::Ok );
    SW_EXPECT_EQUAL( uint32{ 0 }, received._entityID ); // `Ok` 가 아니면 바꾸지 않는다

    MMOReplicator replicator;
    MMOEntity     entity;
    entity._entityID           = 9;
    entity._position._value._z = std::numeric_limits<float32>::infinity();
    SW_EXPECT_LOG( "MMOReplicator.EntityHeaderOutOfRange" );
    replicator.setEntity( entity );
    SW_EXPECT_EQUAL( uint64{ 1 }, replicator.getOutOfRangeEntityCount() );
}
