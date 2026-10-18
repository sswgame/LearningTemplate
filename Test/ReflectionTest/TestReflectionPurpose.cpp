#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 저장 목적(Authoring · Snapshot · SaveGame) × 필드 플래그(없음 · Transient · RuntimeState · SaveGame) × 형식(XML · JSON · 바이너리)의 표가
// `SerializerUtil::shouldSerializeProperty` 한 곳에서 같게 나오는지 본다. 읽는 쪽은 목적이 다루지 않는 필드를 지금 값 그대로 둔다.

namespace
{
    enum class PurposeFormat : uint8
    {
        XML,
        JSON,
        Binary,
    };

    constexpr int32 kUntouched = -1;

    /** @brief @p source 를 형식 @p format · 목적 @p purpose 로 쓰고, 모든 칸이 `kUntouched` 인 @p outTarget 으로 같은 목적으로 읽습니다. */
    template <typename T>
    bool roundTrip( const T& source, T& outTarget, PurposeFormat format, sw::SerializePurpose purpose )
    {
        const sw::TypeInfo&         typeInfo = *T::StaticType();
        const sw::SerializeContext& context  = sw::SerializeContext::getDefault( purpose );
        switch ( format )
        {
            case PurposeFormat::XML:
            {
                return sw::XMLSerializer::deserialize( &outTarget, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo, context ), context );
            }
            case PurposeFormat::JSON:
            {
                return sw::JSONSerializer::deserialize( &outTarget, typeInfo, sw::JSONSerializer::serialize( &source, typeInfo, context ), context );
            }
            case PurposeFormat::Binary:
            {
                sw::vector<uint8> bytes;
                if ( sw::BinarySerializer::serialize( &source, typeInfo, bytes, context ) == false )
                    return false;
                return sw::BinarySerializer::deserialize( &outTarget, typeInfo, bytes.data(), bytes.size(), context );
            }
        }
        return false;
    }

    sw::PurposeOptInSample makeOptInSource()
    {
        sw::PurposeOptInSample source;
        source._plain        = 11;
        source._transient    = 22;
        source._runtime      = 33;
        source._saved        = 44;
        source._savedRuntime = 55;
        return source;
    }

    sw::PurposeOptInSample makeOptInTarget()
    {
        sw::PurposeOptInSample target;
        target._plain        = kUntouched;
        target._transient    = kUntouched;
        target._runtime      = kUntouched;
        target._saved        = kUntouched;
        target._savedRuntime = kUntouched;
        return target;
    }

    constexpr PurposeFormat kArrFormat[] = { PurposeFormat::XML, PurposeFormat::JSON, PurposeFormat::Binary };
} // namespace

/**
 * @brief [ReflectionPurposeTest] 옵트인 세이브 타입(SaveGame 필드가 있음)의 네 행 × 세 목적 × 세 형식
 * @details Authoring 은 RuntimeState(SaveGame 과 함께여도)를 빼고, Snapshot 은 Transient 만 빼며, SaveGame 은 SaveGame 필드만 쓴다.
 */
SW_TEST_CASE( ReflectionPurposeTest, OptInTypeFollowsThePurposeTable )
{
    const sw::PurposeOptInSample source = makeOptInSource();
    for ( const PurposeFormat format : kArrFormat )
    {
        sw::PurposeOptInSample authoring = makeOptInTarget();
        SW_ASSERT_TRUE( roundTrip( source, authoring, format, sw::SerializePurpose::Authoring ) );
        SW_EXPECT_EQUAL( 11, authoring._plain );
        SW_EXPECT_EQUAL( kUntouched, authoring._transient );
        SW_EXPECT_EQUAL( kUntouched, authoring._runtime );
        SW_EXPECT_EQUAL( 44, authoring._saved );
        SW_EXPECT_EQUAL( kUntouched, authoring._savedRuntime );

        sw::PurposeOptInSample snapshot = makeOptInTarget();
        SW_ASSERT_TRUE( roundTrip( source, snapshot, format, sw::SerializePurpose::Snapshot ) );
        SW_EXPECT_EQUAL( 11, snapshot._plain );
        SW_EXPECT_EQUAL( kUntouched, snapshot._transient );
        SW_EXPECT_EQUAL( 33, snapshot._runtime );
        SW_EXPECT_EQUAL( 44, snapshot._saved );
        SW_EXPECT_EQUAL( 55, snapshot._savedRuntime );

        sw::PurposeOptInSample saveGame = makeOptInTarget();
        SW_ASSERT_TRUE( roundTrip( source, saveGame, format, sw::SerializePurpose::SaveGame ) );
        SW_EXPECT_EQUAL( kUntouched, saveGame._plain );
        SW_EXPECT_EQUAL( kUntouched, saveGame._transient );
        SW_EXPECT_EQUAL( kUntouched, saveGame._runtime );
        SW_EXPECT_EQUAL( 44, saveGame._saved );
        SW_EXPECT_EQUAL( 55, saveGame._savedRuntime );
    }
}

/**
 * @brief [ReflectionPurposeTest] SaveGame 필드가 없는 타입은 세이브가 Transient 밖을 모두 쓴다(RuntimeState 포함). 저작은 RuntimeState 를 뺀다
 */
SW_TEST_CASE( ReflectionPurposeTest, PlainTypeSavesEverythingButTransient )
{
    sw::PurposePlainSample source;
    source._plain     = 11;
    source._transient = 22;
    source._runtime   = 33;
    for ( const PurposeFormat format : kArrFormat )
    {
        for ( const sw::SerializePurpose purpose : { sw::SerializePurpose::Authoring, sw::SerializePurpose::Snapshot, sw::SerializePurpose::SaveGame } )
        {
            sw::PurposePlainSample target;
            target._plain     = kUntouched;
            target._transient = kUntouched;
            target._runtime   = kUntouched;
            SW_ASSERT_TRUE( roundTrip( source, target, format, purpose ) );
            SW_EXPECT_EQUAL( 11, target._plain );
            SW_EXPECT_EQUAL( kUntouched, target._transient );
            SW_EXPECT_EQUAL( purpose == sw::SerializePurpose::Authoring ? kUntouched : 33, target._runtime );
        }
    }
}

/**
 * @brief [ReflectionPurposeTest] 저작 목적으로 읽는 데이터에 RuntimeState 필드가 있으면 세 형식 모두 로드 오류다(조용히 버리지 않는다)
 */
SW_TEST_CASE( ReflectionPurposeTest, RuntimeStateInAnAuthoringFileFailsTheRead )
{
    const sw::TypeInfo&         typeInfo  = *sw::PurposePlainSample::StaticType();
    const sw::SerializeContext& snapshot  = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    const sw::SerializeContext& authoring = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    sw::PurposePlainSample      source;
    source._runtime = 33;

    sw::PurposePlainSample xmlTarget;
    SW_EXPECT_FALSE( sw::XMLSerializer::deserialize( &xmlTarget, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo, snapshot ), authoring ) );

    sw::PurposePlainSample jsonTarget;
    SW_EXPECT_FALSE( sw::JSONSerializer::deserialize( &jsonTarget, typeInfo, sw::JSONSerializer::serialize( &source, typeInfo, snapshot ), authoring ) );

    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, typeInfo, bytes, snapshot ) );
    sw::PurposePlainSample binaryTarget;
    SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &binaryTarget, typeInfo, bytes.data(), bytes.size(), authoring ) );

    // 저작 목적으로 쓴 데이터는 RuntimeState 를 싣지 않으므로 같은 목적으로 다시 읽힌다.
    sw::PurposePlainSample authoringTarget;
    SW_EXPECT_TRUE( sw::XMLSerializer::deserialize( &authoringTarget, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo, authoring ), authoring ) );
}
