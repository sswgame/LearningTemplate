#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Serialization/Diff/ObjectDiffSerializer.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

// ObjectDiffSerializerTest — 기본값(CDO) 대비 델타: 바꾼 필드만 싣고, 새 기본 객체에 적용하면 바꾼 객체와 같으며, 깨진 델타는 적용하지 않는다.
// 대상은 문자열 · 정수 · bool · 구조체 배열이 섞인 엔진 설정(`ServerConfig`, 직렬화와 같은 층)이다(nogpu).

namespace
{
    bool isSame( const sw::ServerConfig& left, const sw::ServerConfig& right )
    {
        if ( left._listStore.size() != right._listStore.size() )
            return false;
        for ( size_t index = 0; index < left._listStore.size(); ++index )
        {
            if ( left._listStore[index]._name != right._listStore[index]._name || left._listStore[index]._driver != right._listStore[index]._driver )
                return false;
        }
        return left._listenAddress == right._listenAddress && left._gamePort == right._gamePort && left._tickRateHz == right._tickRateHz &&
               left._bConsoleInput == right._bConsoleInput && left._servicePort == right._servicePort;
    }

    const sw::SerializeContext& getContext()
    {
        return sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    }
} // namespace

/**
 * @brief [ObjectDiffSerializerTest] 바꾼 필드만 델타에 실리고, 새 기본 객체에 적용하면 바꾼 객체와 같다. 바꾼 것이 없으면 델타가 더 작다
 */
SW_TEST_CASE( ObjectDiffSerializerTest, AppliesOnlyChangedFields )
{
    const sw::ServerConfig defaults;
    sw::ServerConfig       modified;
    modified._listenAddress = "10.0.0.5";
    modified._gamePort      = 9000;
    modified._bConsoleInput = false;
    sw::ServerStoreEntry store;
    store._name   = "accounts";
    store._driver = "sql";
    modified._listStore.push_back( store );
    const sw::TypeInfo& type = *sw::ServerConfig::StaticType();

    sw::vector<uint8> diff;
    SW_REQUIRE_TRUE( sw::ObjectDiffSerializer::serializeDiff( diff, &defaults, &modified, type, getContext() ) );
    sw::vector<uint8> unchanged;
    SW_REQUIRE_TRUE( sw::ObjectDiffSerializer::serializeDiff( unchanged, &defaults, &defaults, type, getContext() ) );
    SW_EXPECT_TRUE( unchanged.size() < diff.size() );

    sw::ServerConfig target;
    SW_REQUIRE_TRUE( sw::ObjectDiffSerializer::deserializeDiff( &target, type, diff.data(), diff.size(), getContext() ) );
    SW_EXPECT_TRUE( isSame( modified, target ) );

    sw::ServerConfig untouched;
    untouched._tickRateHz = 60; // 델타에 없는 필드는 적용이 건드리지 않는다
    SW_REQUIRE_TRUE( sw::ObjectDiffSerializer::deserializeDiff( &untouched, type, diff.data(), diff.size(), getContext() ) );
    SW_EXPECT_EQUAL( 60, untouched._tickRateHz );
    SW_EXPECT_EQUAL( 9000, untouched._gamePort );
}

/**
 * @brief [ObjectDiffSerializerTest] 잘린 델타 · 모르는 필드 해시는 적용하지 않는다
 */
SW_TEST_CASE( ObjectDiffSerializerTest, RefusesBrokenDiffs )
{
    const sw::ServerConfig defaults;
    sw::ServerConfig       modified;
    modified._gamePort       = 1234;
    const sw::TypeInfo& type = *sw::ServerConfig::StaticType();
    sw::vector<uint8>   diff;
    SW_REQUIRE_TRUE( sw::ObjectDiffSerializer::serializeDiff( diff, &defaults, &modified, type, getContext() ) );
    SW_REQUIRE_TRUE( diff.size() > 2 );

    sw::ServerConfig target;
    SW_EXPECT_FALSE( sw::ObjectDiffSerializer::deserializeDiff( &target, type, diff.data(), diff.size() - 1, getContext() ) );

    const uint8 arrGarbage[16] = { 0xEF, 0xBE, 0xAD, 0xDE, 0x11, 0x22, 0x33, 0x44, 4, 0, 0, 0, 1, 2, 3, 4 };
    SW_EXPECT_FALSE( sw::ObjectDiffSerializer::deserializeDiff( &target, type, arrGarbage, sizeof( arrGarbage ), getContext() ) );
}
