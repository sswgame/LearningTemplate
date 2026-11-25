#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"
#include "GameFramework/Runtime/Online/Store/Tests/ServiceRecordUtilTestTypes.h"

// ServiceRecordUtilTest — 레코드 판은 타입(`REFLECT( Version )`)에 있고, 옛 판 영속 레코드는 그 타입의 패치 사슬로 올라 읽힌다(감사 11 — 이관 계층이 없어
// 판을 올리면 옛 레코드 전부가 읽기 실패였다). 새 판 · 단계가 빠진 판은 손상이다.

namespace sw
{
    namespace
    {
        struct ServiceRecordUtilTestPatches
        {
            /** @brief 판 1 → 2: 정수 체력을 실수 체력으로 옮긴다. */
            static bool convertShape1( const ServiceRecordTestShape1& oldRecord, ServiceRecordTestRecord& outRecord )
            {
                outRecord._health = static_cast<float32>( oldRecord._hp );
                return true;
            }
        };

        /** @brief 판 @p version 머리를 붙인 옛 모양 레코드 바이트입니다. */
        vector<uint8> makeShape1Bytes( uint32 version, int32 hp )
        {
            ServiceRecordTestShape1 shape;
            shape._hp = hp;
            vector<uint8> bytes;
            if ( BinarySerializer::serializeVersioned( version, &shape, *ServiceRecordTestShape1::StaticType(), bytes ) == false )
                bytes.clear(); // 쓰지 못하면 빈 바이트 — 읽는 단언이 진다
            return bytes;
        }
    } // namespace

    SW_TYPE_VERSION_PATCH( serviceRecordTest1To2, "sw::ServiceRecordTestRecord", 1, 2,
                           (&ServiceRecordUtil::upgradeRecordBody<ServiceRecordTestShape1, ServiceRecordTestRecord, &ServiceRecordUtilTestPatches::convertShape1>));
} // namespace sw

/**
 * @brief [ServiceRecordUtilTest] 판은 레코드 타입의 `REFLECT( Version )` 이고, 쓴 레코드는 그 판으로 다시 읽힌다
 */
SW_TEST_CASE( ServiceRecordUtilTest, VersionComesFromTheRecordType )
{
    SW_EXPECT_EQUAL( uint32{ 2 }, sw::ServiceRecordUtil::getVersion<sw::ServiceRecordTestRecord>() );
    SW_EXPECT_EQUAL( uint32{ 1 }, sw::ServiceRecordUtil::getVersion<sw::ServiceRecordTestShape1>() ); // 적지 않으면 1

    sw::ServiceRecordTestRecord record;
    record._health = 4.5f;
    sw::vector<uint8> bytes;
    SW_REQUIRE_TRUE( sw::ServiceRecordUtil::encode( record, bytes ) );
    sw::ServiceRecordTestRecord restored;
    SW_REQUIRE_TRUE( sw::ServiceRecordUtil::readDurable( "test/current", bytes, restored ) );
    SW_EXPECT_NEAR( 4.5f, restored._health, 1.0e-6f );
}

/**
 * @brief [ServiceRecordUtilTest] 옛 판(1) 영속 레코드는 그 타입의 패치 사슬로 올라 읽히고, 새 판(3)은 손상으로 거절하며 받을 레코드는 그대로다
 */
SW_TEST_CASE( ServiceRecordUtilTest, OldVersionIsPatchedNewerIsRefused )
{
    sw::ServiceRecordTestRecord upgraded;
    SW_REQUIRE_TRUE( sw::ServiceRecordUtil::readDurable( "test/old", sw::makeShape1Bytes( 1, 7 ), upgraded ) );
    SW_EXPECT_NEAR( 7.0f, upgraded._health, 1.0e-6f );

    sw::ServiceRecordTestRecord untouched;
    untouched._health = 2.0f;
    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    SW_EXPECT_LOG( "ServiceRecordUtil.DurableRecordVersionBytes" );
    SW_EXPECT_FALSE( sw::ServiceRecordUtil::readDurable( "test/newer", sw::makeShape1Bytes( 3, 9 ), untouched ) );
    SW_EXPECT_NEAR( 2.0f, untouched._health, 1.0e-6f );
}
