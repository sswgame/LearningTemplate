#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

// ReflectionPurposeTest — 저장 목적(Authoring · Snapshot · SaveGame) × 필드 플래그(없음 · Transient · RuntimeState · SaveGame) × 형식(XML · JSON · 바이너리)의 표가
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

    /** @brief @p source 를 형식 @p format · 목적 @p purpose 로 쓰고, 같은 목적으로 @p outTarget 에 읽습니다. */
    template <typename T>
    bool roundTrip( const T& source, T& outTarget, PurposeFormat format, sw::SerializePurpose purpose )
    {
        const sw::TypeInfo*         pTypeInfo = T::StaticType();
        const sw::SerializeContext& context   = sw::SerializeContext::getDefault( purpose );
        if ( pTypeInfo == nullptr )
            return false;
        switch ( format )
        {
            case PurposeFormat::XML:
            {
                return sw::XMLSerializer::deserialize( &outTarget, *pTypeInfo, sw::XMLSerializer::serialize( &source, *pTypeInfo, context ), context );
            }
            case PurposeFormat::JSON:
            {
                return sw::JSONSerializer::deserialize( &outTarget, *pTypeInfo, sw::JSONSerializer::serialize( &source, *pTypeInfo, context ), context );
            }
            case PurposeFormat::Binary:
            {
                sw::vector<uint8> bytes;
                if ( sw::BinarySerializer::serialize( &source, *pTypeInfo, bytes, context ) == false )
                    return false;
                return sw::BinarySerializer::deserialize( &outTarget, *pTypeInfo, bytes.data(), bytes.size(), context );
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

    /** @brief 원소 하나(그 안의 RuntimeState 필드에 값이 있다)와 뒤 칸을 든 목록 샘플입니다. */
    sw::PurposeListHolder makeListSource()
    {
        sw::PurposeListHolder source;
        source._listSample.resize( 1 );
        source._listSample[0]._plain   = 11;
        source._listSample[0]._runtime = 33;
        source._after                  = 9;
        return source;
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
        SW_REQUIRE_TRUE( roundTrip( source, authoring, format, sw::SerializePurpose::Authoring ) );
        SW_EXPECT_EQUAL( 11, authoring._plain );
        SW_EXPECT_EQUAL( kUntouched, authoring._transient );
        SW_EXPECT_EQUAL( kUntouched, authoring._runtime );
        SW_EXPECT_EQUAL( 44, authoring._saved );
        SW_EXPECT_EQUAL( kUntouched, authoring._savedRuntime );

        sw::PurposeOptInSample snapshot = makeOptInTarget();
        SW_REQUIRE_TRUE( roundTrip( source, snapshot, format, sw::SerializePurpose::Snapshot ) );
        SW_EXPECT_EQUAL( 11, snapshot._plain );
        SW_EXPECT_EQUAL( kUntouched, snapshot._transient );
        SW_EXPECT_EQUAL( 33, snapshot._runtime );
        SW_EXPECT_EQUAL( 44, snapshot._saved );
        SW_EXPECT_EQUAL( 55, snapshot._savedRuntime );

        sw::PurposeOptInSample saveGame = makeOptInTarget();
        SW_REQUIRE_TRUE( roundTrip( source, saveGame, format, sw::SerializePurpose::SaveGame ) );
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
            SW_REQUIRE_TRUE( roundTrip( source, target, format, purpose ) );
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
    SW_EXPECT_LOG( "Serialization.RuntimeStateInAuthoring" ); // 거절이 이 테스트가 보려는 동작이다
    const sw::TypeInfo* pTypeInfo = sw::PurposePlainSample::StaticType();
    SW_REQUIRE_NOT_NULL( pTypeInfo );
    const sw::SerializeContext& snapshot  = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    const sw::SerializeContext& authoring = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    sw::PurposePlainSample      source;
    source._runtime = 33;

    sw::PurposePlainSample xmlTarget;
    SW_EXPECT_FALSE( sw::XMLSerializer::deserialize( &xmlTarget, *pTypeInfo, sw::XMLSerializer::serialize( &source, *pTypeInfo, snapshot ), authoring ) );

    sw::PurposePlainSample jsonTarget;
    SW_EXPECT_FALSE( sw::JSONSerializer::deserialize( &jsonTarget, *pTypeInfo, sw::JSONSerializer::serialize( &source, *pTypeInfo, snapshot ), authoring ) );

    sw::vector<uint8> bytes;
    SW_REQUIRE_TRUE( sw::BinarySerializer::serialize( &source, *pTypeInfo, bytes, snapshot ) );
    sw::PurposePlainSample binaryTarget;
    SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &binaryTarget, *pTypeInfo, bytes.data(), bytes.size(), authoring ) );

    // 저작 목적으로 쓴 데이터는 RuntimeState 를 싣지 않으므로 같은 목적으로 다시 읽힌다.
    sw::PurposePlainSample authoringTarget;
    SW_EXPECT_TRUE( sw::XMLSerializer::deserialize( &authoringTarget, *pTypeInfo, sw::XMLSerializer::serialize( &source, *pTypeInfo, authoring ), authoring ) );
}

/**
 * @brief [ReflectionPurposeTest] 컨테이너 원소인 값 구조체 안의 RuntimeState 필드도 소프트 읽기를 객체째 실패시킨다(칸째 되돌려 orphan 으로 넘기지 않는다)
 * @details 예전에는 원소 읽기의 실패가 "필드 실패" 하나로 내려와 그 칸이 읽기 전 값으로 되돌려지고 orphan 이 되어 읽기가 성공했다.
 *          이제 원소 결과가 `ContainerReadResult::ObjectRejected` 로 올라온다. 바이너리는 `RuntimeStateInAContainerElementRejectsTheBinarySoftRead`.
 */
SW_TEST_CASE( ReflectionPurposeTest, RuntimeStateInAContainerElementRejectsTheSoftRead )
{
    SW_EXPECT_LOG( "Serialization.RuntimeStateInAuthoring" ); // 원소 안의 금지된 필드
    const sw::TypeInfo* pHolderType = sw::PurposeListHolder::StaticType();
    SW_REQUIRE_NOT_NULL( pHolderType );
    const sw::SerializeContext& snapshot  = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    const sw::SerializeContext& authoring = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    const sw::PurposeListHolder source    = makeListSource();

    sw::PurposeListHolder             xmlTarget;
    sw::vector<sw::SchemaOrphanValue> listXMLOrphan;
    const sw::string                  xmlText = sw::XMLSerializer::serialize( &source, *pHolderType, snapshot );
    SW_EXPECT_TRUE( sw::XMLSerializer::deserializeSoft( &xmlTarget, *pHolderType, xmlText, &listXMLOrphan, nullptr, authoring ) == sw::SoftReadResult::Rejected );

    sw::PurposeListHolder             jsonTarget;
    sw::vector<sw::SchemaOrphanValue> listJSONOrphan;
    const sw::string                  jsonText = sw::JSONSerializer::serialize( &source, *pHolderType, snapshot );
    SW_EXPECT_TRUE( sw::JSONSerializer::deserializeSoft( &jsonTarget, *pHolderType, jsonText, &listJSONOrphan, nullptr, authoring ) == sw::SoftReadResult::Rejected );

    // 저작 목적으로 쓴 데이터는 원소에도 RuntimeState 를 싣지 않으므로 소프트 읽기가 그대로 읽는다.
    sw::PurposeListHolder             authoringTarget;
    sw::vector<sw::SchemaOrphanValue> listAuthoringOrphan;
    const sw::string                  authoringText = sw::XMLSerializer::serialize( &source, *pHolderType, authoring );
    SW_EXPECT_TRUE( sw::XMLSerializer::deserializeSoft( &authoringTarget, *pHolderType, authoringText, &listAuthoringOrphan, nullptr, authoring ) == sw::SoftReadResult::Read );
    SW_REQUIRE_EQUAL( size_t( 1 ), authoringTarget._listSample.size() );
    SW_EXPECT_EQUAL( 11, authoringTarget._listSample[0]._plain );
    SW_EXPECT_EQUAL( 9, authoringTarget._after );
}

/**
 * @brief [ReflectionPurposeTest] 바이너리도 컨테이너 원소인 값 구조체 안의 RuntimeState 필드가 소프트 읽기를 객체째 실패시킨다
 * @details 원소 구조체는 `deserializeNested` 의 `FieldReadResult::ObjectRejected` 를 그대로 올린다(예전에는 원소 실패로 내려와 orphan 이 되어 읽기가 성공했다).
 */
SW_TEST_CASE( ReflectionPurposeTest, RuntimeStateInAContainerElementRejectsTheBinarySoftRead )
{
    SW_EXPECT_LOG( "Serialization.RuntimeStateInAuthoring" ); // 원소 안의 금지된 필드
    const sw::TypeInfo* pHolderType = sw::PurposeListHolder::StaticType();
    SW_REQUIRE_NOT_NULL( pHolderType );
    const sw::SerializeContext& snapshot  = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    const sw::SerializeContext& authoring = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    const sw::PurposeListHolder source    = makeListSource();

    sw::vector<uint8> snapshotBytes;
    SW_REQUIRE_TRUE( sw::BinarySerializer::serialize( &source, *pHolderType, snapshotBytes, snapshot ) );
    sw::PurposeListHolder             binaryTarget;
    sw::vector<sw::SchemaOrphanValue> listBinaryOrphan;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &binaryTarget, *pHolderType, snapshotBytes.data(), snapshotBytes.size(), &listBinaryOrphan, authoring ) ==
                    sw::SoftReadResult::Rejected );
    SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &binaryTarget, *pHolderType, snapshotBytes.data(), snapshotBytes.size(), authoring ) );

    // 저작 목적으로 쓴 데이터는 원소에도 RuntimeState 를 싣지 않으므로 소프트 읽기가 그대로 읽는다.
    sw::vector<uint8> authoringBytes;
    SW_REQUIRE_TRUE( sw::BinarySerializer::serialize( &source, *pHolderType, authoringBytes, authoring ) );
    sw::PurposeListHolder             authoringTarget;
    sw::vector<sw::SchemaOrphanValue> listAuthoringOrphan;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &authoringTarget, *pHolderType, authoringBytes.data(), authoringBytes.size(), &listAuthoringOrphan, authoring ) ==
                    sw::SoftReadResult::Read );
    SW_REQUIRE_EQUAL( size_t( 1 ), authoringTarget._listSample.size() );
    SW_EXPECT_EQUAL( 11, authoringTarget._listSample[0]._plain );
    SW_EXPECT_EQUAL( 9, authoringTarget._after );
}
