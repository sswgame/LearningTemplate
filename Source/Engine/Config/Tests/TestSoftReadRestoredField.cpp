// 소프트 읽기의 되돌린 칸 — 세 형식(JSON · XML · 바이너리)이 읽지 못한 칸을 읽기 전 값으로 되돌려도 그 값(바탕 층 형식의 새 객체 — 0, Min 밖)이
// 범위 오류로 덧나지 않고 orphan 하나로 끝나는지 본다(`SerializerUtil::isWithinDeclaredRanges` 의 되돌린 칸 목록, nogpu).
// 표본은 기본값 없이 Min 이 있는 칸을 가진 실제 형식 `RenderConfig`(구조체 칸 `_textureStreaming` 포함)다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/ShippingHostDefaults.h"

using namespace sw;

namespace
{
    /** @brief 커밋된 엔진 설정(구성 때 구운 사본)의 `_render` — 모든 칸이 범위 안인 표본입니다. */
    [[nodiscard]] bool loadCommittedRender( RenderConfig& outRender )
    {
        EngineConfig engine{};
        if ( ConfigManager::readConfigJSON( engine, shipping_host::kEngineConfigJSON, "EngineConfig.json (configure-time copy)" ) == false )
            return false;
        outRender = engine._render;
        return true;
    }

    /** @brief @p type 의 프로퍼티 @p pName 의 이름 해시입니다. 없으면 0 입니다. */
    [[nodiscard]] uint32 findNameHash( const TypeInfo& type, const char* pName )
    {
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._name == hashed_string( pName ) )
                return prop.getNameHash();
        }
        return 0u;
    }
} // namespace

/**
 * @brief [SoftReadRestoredFieldTest] JSON — 구조체 칸 안의 모르는 키는 그 칸을 orphan 하나로 넘기고 끝난다(되돌린 0 이 범위 오류로 덧나지 않는다)
 */
SW_TEST_CASE( SoftReadRestoredFieldTest, JSONUnknownKeyInStructFieldIsOneOrphan )
{
    RenderConfig render{};
    SW_REQUIRE_TRUE( loadCommittedRender( render ) );
    const expected<string> text = JSONSerializer::serialize( &render, *RenderConfig::StaticType() );
    SW_REQUIRE_TRUE( text.has_value() );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( *text ) );
    document.getRoot().get( "_textureStreaming" ).set( "_unknownKey" ).setUint( 1 );
    const expected<string> edited = document.dump();
    SW_REQUIRE_TRUE( edited.has_value() );

    RenderConfig              fresh{};
    vector<SchemaOrphanValue> listOrphan;
    SW_EXPECT_TRUE( JSONSerializer::deserializeSoft( &fresh, *RenderConfig::StaticType(), *edited, &listOrphan ) == SoftReadResult::Read );
    SW_EXPECT_EQUAL( size_t{ 1 }, listOrphan.size() );
    SW_EXPECT_EQUAL( render._shadowLocalCullSlotLimit, fresh._shadowLocalCullSlotLimit ); // 다른 칸은 읽혔다
}

/**
 * @brief [SoftReadRestoredFieldTest] XML — 읽지 못한 값(Min 있는 칸에 글자)은 그 칸을 orphan 하나로 넘기고 끝난다
 * @details XML 의 구조체 칸 안 모르는 이름은 되돌리지 않고 orphan 으로만 남는다 — 되돌리는 길은 읽지 못한 값 · 컨테이너다.
 */
SW_TEST_CASE( SoftReadRestoredFieldTest, XMLUnreadableValueIsOneOrphan )
{
    RenderConfig render{};
    SW_REQUIRE_TRUE( loadCommittedRender( render ) );
    string                                   text = XMLSerializer::serialize( &render, *RenderConfig::StaticType() );
    StringBuilder<constant::buffer::kMax128> before;
    before.appendFormat( "_shadowLocalMinTileResolution=\"%#\"", render._shadowLocalMinTileResolution );
    const size_t at = text.find( before.c_str() );
    SW_REQUIRE_TRUE( at != string::npos );
    text.replace( at, before.size(), "_shadowLocalMinTileResolution=\"abc\"" );

    RenderConfig              fresh{};
    vector<SchemaOrphanValue> listOrphan;
    SW_EXPECT_TRUE( XMLSerializer::deserializeSoft( &fresh, *RenderConfig::StaticType(), text, &listOrphan ) == SoftReadResult::Read );
    SW_EXPECT_EQUAL( size_t{ 1 }, listOrphan.size() );
    SW_EXPECT_EQUAL( 0u, fresh._shadowLocalMinTileResolution ); // 읽기 전 값(새 객체)으로 남았다
}

/**
 * @brief [SoftReadRestoredFieldTest] 바이너리 — 구조체 칸 안의 모르는 태그는 그 칸을 orphan 하나로 넘기고 끝난다
 * @details 구조체 칸의 안쪽 읽기는 엄격이라 모르는 태그에서 칸째 실패하고, 바깥 소프트 읽기가 칸을 읽기 전 값으로 되돌린다.
 */
SW_TEST_CASE( SoftReadRestoredFieldTest, BinaryUnknownTagInStructFieldIsOneOrphan )
{
    RenderConfig render{};
    SW_REQUIRE_TRUE( loadCommittedRender( render ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &render, *RenderConfig::StaticType(), bytes ) );
    // `_uploadsPerFrame` 은 `TextureStreamingConfig` 에만 있는 이름이다 — 그 태그를 모르는 이름으로 바꾼다.
    const uint32 tagHash = findNameHash( *TextureStreamingConfig::StaticType(), "_uploadsPerFrame" );
    SW_REQUIRE_TRUE( tagHash != 0u );
    bool bReplaced = false;
    for ( size_t offset = 0; offset + sizeof( uint32 ) <= bytes.size() && bReplaced == false; ++offset )
    {
        uint32 value = 0;
        std::memcpy( &value, bytes.data() + offset, sizeof( uint32 ) );
        if ( value != tagHash )
            continue;
        const uint32 unknownHash = 0xDEADBEEFu;
        std::memcpy( bytes.data() + offset, &unknownHash, sizeof( uint32 ) );
        bReplaced = true;
    }
    SW_REQUIRE_TRUE( bReplaced );

    RenderConfig              fresh{};
    vector<SchemaOrphanValue> listOrphan;
    SW_EXPECT_TRUE( BinarySerializer::deserializeSoft( &fresh, *RenderConfig::StaticType(), bytes.data(), bytes.size(), &listOrphan ) == SoftReadResult::Read );
    SW_EXPECT_EQUAL( size_t{ 1 }, listOrphan.size() );
    SW_EXPECT_EQUAL( 0u, fresh._textureStreaming._poolMegabytes ); // 칸째 읽기 전 값(새 객체)으로 남았다
}
