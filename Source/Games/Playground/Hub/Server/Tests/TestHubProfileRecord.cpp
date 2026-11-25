// HubProfileRecordTest — 허브 진행 레코드는 진행의 칸 선언(`HubProfile` 의 `Replicated` · 범위)이 바이트를 정한다.
// 새 레코드는 왕복하고, 옛 판 1(손 배치 — 판을 두 번 실었다) 레코드도 잃지 않고 읽으며, 모르는 판 · 범위 밖 값은 쓰지도 읽지도 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Games/Playground/Hub/Server/HubProfileStore.h"

using namespace sw;

/** @brief [HubProfileRecordTest] 레코드가 같은 값으로 돌아오고, 판 1 레코드는 옛 배치로 읽히며, 모르는 판과 범위 밖 진행은 거절된다. */
SW_TEST_CASE( HubProfileRecordTest, RecordRoundTripsAndReadsFormat1 )
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

    // 판 1 레코드 — 저장소가 판을 쓰고 진행이 판을 한 번 더 쓴 뒤 칸을 손으로 이어 썼다.
    BitWriter format1;
    format1.writeVarUint( 1 );
    format1.writeVarUint( 1 );
    format1.writeVarInt( 4 );   // 레벨
    format1.writeVarInt( 90 );  // 경험
    format1.writeVarInt( 11 );  // 동전
    format1.writeVarInt( 2 );   // 약초
    format1.writeVarInt( 5 );   // 쓰러뜨린 수
    format1.writeFloat( 1.0f ); // 자리
    format1.writeFloat( 2.0f );
    format1.writeFloat( 3.0f );
    HubProfile legacy;
    SW_REQUIRE_TRUE( HubProfileStore::decodeRecord( format1.getBytes(), legacy ) );
    SW_EXPECT_EQUAL( 4, legacy._level );
    SW_EXPECT_EQUAL( 11, legacy._coins );
    SW_EXPECT_EQUAL( 5, legacy._defeatCount );
    SW_EXPECT_TRUE( legacy._position == ( float3{ 1.0f, 2.0f, 3.0f } ) );

    BitWriter unknown;
    unknown.writeVarUint( HubProfile::kFormatVersion + 1 );
    HubProfile untouched;
    SW_EXPECT_FALSE( HubProfileStore::decodeRecord( unknown.getBytes(), untouched ) );

    HubProfile levelZero;
    levelZero._level = 0; // `Min = 1` 밖
    vector<uint8> refused;
    SW_EXPECT_FALSE( HubProfileStore::encodeRecord( levelZero, refused ) );
    SW_EXPECT_TRUE( refused.empty() );
}
