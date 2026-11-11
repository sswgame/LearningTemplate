// 임포트 규칙 JSON 이 `PROPERTY` 스키마(`JSONFields`)로 읽히는지 본다 — 적은 키만 읽기(프리셋 상속), 모르는 키 · 값 · 범위 거절.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Model/ModelImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"

/**
 * @brief [ImportConfigReadTest] 프리셋을 물려받은 규칙은 적은 키만 덮어쓰고, 안쪽 구조체(`filter`)와 열거 이름을 읽는다.
 */
SW_TEST_CASE( ImportConfigReadTest, TextureRuleOverridesOnlyWrittenKeys )
{
    sw::editor::TextureImportConfig config;
    SW_REQUIRE_TRUE( config.loadFromJSONString( R"({
        "presets": { "Base": { "format": "BC5", "sRGB": false, "swizzle": "BGRA" } },
        "rules": [ { "name": "Normal", "inherits": "Base", "generateMips": false, "filter": { "includePatterns": ["*_n.*"], "excludePaths": ["ui/"] } } ]
    })" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, config.getRules().size() );
    const sw::editor::TextureImportRule& rule = config.getRules()[0];
    SW_EXPECT_TRUE( rule._format == sw::editor::TextureImportFormat::BC5 );
    SW_EXPECT_TRUE( rule._swizzle == sw::editor::TextureSwizzle::BGRA );
    SW_EXPECT_TRUE( rule._bSRGB == SW_FALSE );
    SW_EXPECT_TRUE( rule._bGenerateMips == SW_FALSE );
    SW_EXPECT_EQUAL( size_t{ 1 }, rule._filter._listIncludePattern.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, rule._filter._listExcludePath.size() );
    SW_EXPECT_TRUE( rule._filter.matchesPath( "game/textures_raw/brick_n.png" ) );
    SW_EXPECT_FALSE( rule._filter.matchesPath( "game/ui/brick_n.png" ) );
}

/**
 * @brief [ImportConfigReadTest] 모르는 키(바깥 · 안쪽)와 모르는 열거 이름은 설정 전체의 오류이고 규칙을 남기지 않는다.
 */
SW_TEST_CASE( ImportConfigReadTest, UnknownKeysAndNamesFailTheLoad )
{
    sw::editor::TextureImportConfig config;
    SW_EXPECT_LOG( "JSONFields.UnknownField" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "generate_mips": false } ] })" ) );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "filter": { "include_patterns": ["*"] } } ] })" ) );
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    SW_EXPECT_LOG( "JSONFields.ValueUnreadable" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "format": "BC2" } ] })" ) );
    SW_EXPECT_LOG( "JSONFields.UnknownKey" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rule": [] })" ) );
    SW_EXPECT_TRUE( config.getRules().empty() );
}

/**
 * @brief [ImportConfigReadTest] 모델 규칙은 이름을 지정한 칸 · 고정 배열 · 벡터 글 · 안쪽 파쇄 설정을 읽고, 파쇄를 적으면 `_bFracture` 가 켜진다.
 */
SW_TEST_CASE( ImportConfigReadTest, ModelRuleReadsNamedArrayAndNestedProperties )
{
    sw::editor::ModelImportConfig config;
    SW_REQUIRE_TRUE( config.loadFromJSONString( R"({ "rules": [ {
        "name": "A",
        "translation": [-2.0, 0.0, -1.5],
        "clips": ["Idle", "Run"],
        "importMaterials": false,
        "recenter": "BottomCenter",
        "fracture": { "pattern": "Slices", "volume": "Bounds", "pieces": 24, "levels": [4, 8], "slices": [2, 3, 4], "interiorColor": "0.5, 0.25, 0.75, 1", "impactPoint": "1, 2, 3" },
        "lods": [ { "triangleRatio": 0.5, "screenSize": 0.3 }, { "triangleRatio": 0.25, "screenSize": 0.1, "maxError": 0.05 } ]
    } ] })" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, config.getRules().size() );
    const sw::editor::ModelImportRule& rule = config.getRules()[0];
    SW_EXPECT_NEAR( -2.0f, rule._arrTranslation[0], 1e-6 );
    SW_EXPECT_NEAR( -1.5f, rule._arrTranslation[2], 1e-6 );
    SW_EXPECT_EQUAL( size_t{ 2 }, rule._listClipName.size() );
    SW_EXPECT_TRUE( rule._bImportMaterials == SW_FALSE );
    SW_EXPECT_TRUE( rule._bImportAnimations == SW_TRUE );
    SW_EXPECT_TRUE( rule._recenter == sw::editor::ModelRecenter::BottomCenter );
    SW_EXPECT_TRUE( rule._bFracture == SW_TRUE );
    SW_EXPECT_TRUE( rule._fracture._pattern == sw::FracturePattern::Slices );
    SW_EXPECT_TRUE( rule._fracture._volume == sw::FractureVolume::Bounds );
    SW_EXPECT_EQUAL( 24u, rule._fracture._pieceCount );
    SW_EXPECT_EQUAL( size_t{ 2 }, rule._fracture._listLevelCount.size() );
    SW_EXPECT_EQUAL( 3u, rule._fracture._arrSliceCount[1] );
    SW_EXPECT_NEAR( 0.25f, rule._fracture._interiorColor._y, 1e-6 );
    SW_EXPECT_NEAR( 3.0f, rule._fracture._impactPoint._z, 1e-6 );
    SW_EXPECT_NEAR( 0.15f, rule._fracture._sliceJitter, 1e-6 ); // 적지 않은 칸은 초기값이다
    SW_REQUIRE_EQUAL( size_t{ 2 }, rule._listLOD.size() );
    SW_EXPECT_NEAR( 0.05f, rule._listLOD[1]._maxError, 1e-6 );
}

/**
 * @brief [ImportConfigReadTest] 범위 밖 값 · 길이가 틀린 고정 배열 · 모르는 코덱 · 커지는 LOD 는 설정 오류다.
 */
SW_TEST_CASE( ImportConfigReadTest, ModelRuleRejectsBadValues )
{
    sw::editor::ModelImportConfig config;
    SW_EXPECT_LOG( "JSONFields.ValueOutOfRange" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "animationSampleRate": 0 } ] })" ) );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "fracture": { "levels": [0] } } ] })" ) );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "fracture": { "sliceJitter": 0.9 } } ] })" ) );
    SW_EXPECT_LOG( "JSONFields.ValueUnreadable" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "translation": [1, 2] } ] })" ) );
    SW_EXPECT_LOG( "ModelImportConfig.ModelImportConfigRuleUnknownAnimation" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "animationCodec": "nope" } ] })" ) );
    SW_EXPECT_LOG( "ModelImportConfig.EntryNeedsNumbersTriangle" );
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "lods": [ { "triangleRatio": 0.2 }, { "triangleRatio": 0.4 } ] } ] })" ) );
    SW_EXPECT_TRUE( config.getRules().empty() );
}
