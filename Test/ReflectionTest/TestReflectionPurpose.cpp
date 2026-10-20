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

    /** @brief 원소가 반사된 값 구조체(`PurposePlainSample`)인 목록을 가진 값입니다. 원소 안의 RuntimeState 필드가 바깥 읽기에 어떻게 올라오는지 봅니다. */
    struct PurposeListHolder
    {
        sw::vector<sw::PurposePlainSample> _listSample;
        int32                              _after{ 0 };
    };

    sw::TypeInfo makePurposeListHolderType()
    {
        sw::TypeInfo holderType;
        holderType._name               = sw::hashed_string( "PurposeListHolder" );
        holderType._fullyQualifiedName = sw::hashed_string( "sw::PurposeListHolder" );
        holderType._size               = sizeof( PurposeListHolder );
        holderType._listProperty       = {
            { sw::hashed_string( "_listSample" ), sw::hashed_string( "vector" ), SW_OFFSET_OF( PurposeListHolder, _listSample ), true, sw::ContainerKind::Sequence,
             sw::PurposePlainSample::StaticType()->_name, sw::hashed_string(), sw::make_shared<sw::VectorWrapper<sw::vector<sw::PurposePlainSample>>>() },
            { sw::hashed_string( "_after" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( PurposeListHolder, _after ) },
        };
        return holderType;
    }
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

/**
 * @brief [ReflectionPurposeTest] 컨테이너 원소인 값 구조체 안의 RuntimeState 필드도 소프트 읽기를 객체째 실패시킨다(칸째 되돌려 orphan 으로 넘기지 않는다)
 * @details 예전에는 원소 읽기의 실패가 "필드 실패" 하나로 내려와 그 칸이 읽기 전 값으로 되돌려지고 orphan 이 되어 읽기가 성공했다
 *          (`docs/plans/SerializerSilentSkips.md` S3). 이제 원소 결과가 `ContainerReadResult::ObjectRejected` 로 올라온다. 바이너리는 `RuntimeStateInAContainerElementRejectsTheBinarySoftRead`.
 */
SW_TEST_CASE( ReflectionPurposeTest, RuntimeStateInAContainerElementRejectsTheSoftRead )
{
    const sw::TypeInfo          holderType = makePurposeListHolderType();
    const sw::SerializeContext& snapshot   = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    const sw::SerializeContext& authoring  = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    PurposeListHolder           source;
    source._listSample.resize( 1 );
    source._listSample[0]._plain   = 11;
    source._listSample[0]._runtime = 33;
    source._after                  = 9;

    test::ScopedLogSuppressor suppressor; // 금지된 필드 오류

    PurposeListHolder                 xmlTarget;
    sw::vector<sw::SchemaOrphanValue> listXMLOrphan;
    const sw::string                  xmlText = sw::XMLSerializer::serialize( &source, holderType, snapshot );
    SW_EXPECT_TRUE( sw::XMLSerializer::deserializeSoft( &xmlTarget, holderType, xmlText, &listXMLOrphan, nullptr, authoring ) == sw::SoftReadResult::Rejected );

    PurposeListHolder                 jsonTarget;
    sw::vector<sw::SchemaOrphanValue> listJSONOrphan;
    const sw::string                  jsonText = sw::JSONSerializer::serialize( &source, holderType, snapshot );
    SW_EXPECT_TRUE( sw::JSONSerializer::deserializeSoft( &jsonTarget, holderType, jsonText, &listJSONOrphan, nullptr, authoring ) == sw::SoftReadResult::Rejected );

    // 저작 목적으로 쓴 데이터는 원소에도 RuntimeState 를 싣지 않으므로 소프트 읽기가 그대로 읽는다.
    PurposeListHolder                 authoringTarget;
    sw::vector<sw::SchemaOrphanValue> listAuthoringOrphan;
    const sw::string                  authoringText = sw::XMLSerializer::serialize( &source, holderType, authoring );
    SW_EXPECT_TRUE( sw::XMLSerializer::deserializeSoft( &authoringTarget, holderType, authoringText, &listAuthoringOrphan, nullptr, authoring ) == sw::SoftReadResult::Read );
    SW_ASSERT_EQUAL( size_t( 1 ), authoringTarget._listSample.size() );
    SW_EXPECT_EQUAL( 11, authoringTarget._listSample[0]._plain );
    SW_EXPECT_EQUAL( 9, authoringTarget._after );
}

/**
 * @brief [ReflectionPurposeTest] 바이너리도 컨테이너 원소인 값 구조체 안의 RuntimeState 필드가 소프트 읽기를 객체째 실패시킨다
 * @details 원소 구조체는 `BinarySerializer::deserialize` 의 bool 로 읽혀 금지된 필드가 원소 실패(스트림 깨짐)로 내려왔고, 그 칸이 읽기 전 값으로
 *          되돌려져 orphan 이 되어 읽기가 성공했다. 이제 `deserializeNested` 의 `FieldReadResult::ObjectRejected` 가 그대로 올라온다
 *          (`docs/plans/SerializerSilentSkips.md` S4).
 */
SW_TEST_CASE( ReflectionPurposeTest, RuntimeStateInAContainerElementRejectsTheBinarySoftRead )
{
    const sw::TypeInfo          holderType = makePurposeListHolderType();
    const sw::SerializeContext& snapshot   = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    const sw::SerializeContext& authoring  = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    PurposeListHolder           source;
    source._listSample.resize( 1 );
    source._listSample[0]._plain   = 11;
    source._listSample[0]._runtime = 33;
    source._after                  = 9;

    test::ScopedLogSuppressor suppressor; // 금지된 필드 오류

    sw::vector<uint8> snapshotBytes;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, holderType, snapshotBytes, snapshot ) );
    PurposeListHolder                 binaryTarget;
    sw::vector<sw::SchemaOrphanValue> listBinaryOrphan;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &binaryTarget, holderType, snapshotBytes.data(), snapshotBytes.size(), &listBinaryOrphan, authoring ) ==
                    sw::SoftReadResult::Rejected );
    SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &binaryTarget, holderType, snapshotBytes.data(), snapshotBytes.size(), authoring ) );

    // 저작 목적으로 쓴 데이터는 원소에도 RuntimeState 를 싣지 않으므로 소프트 읽기가 그대로 읽는다.
    sw::vector<uint8> authoringBytes;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, holderType, authoringBytes, authoring ) );
    PurposeListHolder                 authoringTarget;
    sw::vector<sw::SchemaOrphanValue> listAuthoringOrphan;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &authoringTarget, holderType, authoringBytes.data(), authoringBytes.size(), &listAuthoringOrphan, authoring ) ==
                    sw::SoftReadResult::Read );
    SW_ASSERT_EQUAL( size_t( 1 ), authoringTarget._listSample.size() );
    SW_EXPECT_EQUAL( 11, authoringTarget._listSample[0]._plain );
    SW_EXPECT_EQUAL( 9, authoringTarget._after );
}
