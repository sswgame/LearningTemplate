// HubEntityNetStateTest — 허브 복제 엔티티의 상태 바이트는 칸 선언(`HubEntityNetState`)이 정한다. 왕복하면 같고, 종류 번호가 선언 범위 밖이면 쓰지도 읽지도 않는다.
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "Games/Playground/Hub/HubNetLink.h"

using namespace sw;

/** @brief [HubEntityNetStateTest] 짐승의 체력이 같은 값으로 돌아오고, 등록 밖 종류 번호는 쓰기에서 거절된다. */
SW_TEST_CASE( HubEntityNetStateTest, EntityStateRoundTripsAndRejectsUnknownType )
{
    HubEntityNetState sent;
    sent._typeID     = HubEntityType::kCreature;
    sent._stateValue = -3; // 가변 길이 부호 정수도 그대로 온다
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( sent, writer ) );

    BitReader         reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    HubEntityNetState received;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( sent._typeID, received._typeID );
    SW_EXPECT_EQUAL( sent._stateValue, received._stateValue );

    HubEntityNetState unknown;
    unknown._typeID = HubEntityType::kHerb + 1;
    BitWriter unknownWriter;
    SW_EXPECT_FALSE( ReplicatedLayout::writeFull( unknown, unknownWriter ) );
}
