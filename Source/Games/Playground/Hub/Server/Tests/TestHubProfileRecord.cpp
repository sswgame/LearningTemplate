// HubProfileRecordTest — 허브 진행 레코드는 진행의 칸 선언(`HubProfile` 의 `Replicated` · 범위)이 바이트를 정한다.
// 레코드는 왕복하고, 모르는 판 · 범위 밖 값은 쓰지도 읽지도 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Games/Playground/Hub/Server/HubProfileStore.h"

using namespace sw;

/** @brief [HubProfileRecordTest] 레코드가 같은 값으로 돌아오고, 모르는 판과 범위 밖 진행은 거절된다. */
SW_TEST_CASE( HubProfileRecordTest, RecordRoundTripsAndRefusesUnknownVersion )
{
    HubProfile saved;
    saved._level       = 7;
    saved._experience  = 1234;
    saved._coins       = 56;
    saved._herbCount   = 3;
    saved._defeatCount = 21;
    saved._position    = float3{ -12.5f, 0.0f, 40.25f };
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( HubProfileStore::encodeRecord( saved, bytes ) );
    HubProfile loaded;
    SW_REQUIRE_TRUE( HubProfileStore::decodeRecord( bytes, loaded ) );
    SW_EXPECT_EQUAL( 7, loaded._level );
    SW_EXPECT_EQUAL( 1234, loaded._experience );
    SW_EXPECT_EQUAL( 56, loaded._coins );
    SW_EXPECT_EQUAL( 3, loaded._herbCount );
    SW_EXPECT_EQUAL( 21, loaded._defeatCount );
    SW_EXPECT_TRUE( loaded._position == saved._position );

    BitWriter unknown;
    unknown.writeVarUint( HubProfile::kFormat._version + 1 );
    HubProfile untouched;
    SW_EXPECT_FALSE( HubProfileStore::decodeRecord( unknown.getBytes(), untouched ) );

    HubProfile levelZero;
    levelZero._level = 0; // `Min = 1` 밖
    vector<uint8> refused;
    SW_EXPECT_FALSE( HubProfileStore::encodeRecord( levelZero, refused ) );
    SW_EXPECT_TRUE( refused.empty() );
}
