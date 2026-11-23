// 임포트 규칙 JSON 이 `PROPERTY` 스키마(`JSONFields`)로 읽히는지 본다 — 적은 키만 읽기(프리셋 상속), 모르는 키 · 값 · 범위 거절.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/ModelImport/ModelImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"

#include "Engine/Asset/AssetDatabase.h"

namespace
{
    sw::AssetImportOverride makeOverride( const char* pKey, const char* pValue )
    {
        sw::AssetImportOverride item;
        item._key   = pKey;
        item._value = pValue;
        return item;
    }
} // namespace

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
 * @brief [ImportConfigReadTest] 규칙 파일이 없으면 기본 규칙으로 넘어가지 않고 오류다 — 에디터 핫 리로드와 헤드리스 임포트가 같은 답을 받는다.
 */
SW_TEST_CASE( ImportConfigReadTest, MissingRuleFilesAreErrors )
{
    const sw::AbsolutePath missing = sw::test::makeScratchPath( "NoImportConfig.json" );
    SW_EXPECT_LOG( "TextureImportConfig.FailedReadTextureImportConfigFile" );
    sw::editor::TextureImportConfig textureConfig;
    SW_EXPECT_FALSE( textureConfig.loadFromFile( missing.str() ) );
    SW_EXPECT_LOG( "ModelImportConfig.FailedReadModelImportConfigFile" );
    sw::editor::ModelImportConfig modelConfig;
    SW_EXPECT_FALSE( modelConfig.loadFromFile( missing.str() ) );
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

/**
 * @brief [ModelImportConfigTest] 서술(`.meta`)의 예외가 규칙 위 마지막 층이다 — 적은 칸만 바뀌고, `lods` 는 규칙의 단계를 통째로 바꾸며, 맞는 규칙이 없으면 기본 규칙이 바탕이다.
 */
SW_TEST_CASE( ModelImportConfigTest, MetaOverrideWinsOverRule )
{
    sw::editor::ModelImportConfig config;
    SW_REQUIRE_TRUE( config.loadFromJSONString( R"({ "rules": [ {
        "name": "Kit", "filter": { "includePatterns": ["kit/**"] }, "importMaterials": false, "animationSampleRate": 24,
        "lods": [ { "triangleRatio": 0.5, "screenSize": 0.3 } ]
    } ] })" ) );

    const sw::vector<sw::AssetImportOverride> listOverride = { makeOverride( "animationSampleRate", "60" ), makeOverride( "lods", R"([{"triangleRatio": 0.25, "screenSize": 0.12}])" ),
                                                               makeOverride( "fracture", R"({"pieces": 20, "levels": [3]})" ) };
    const sw::editor::ModelImportRuleMatch    match        = config.resolveRule( "kit/skeleton_minion.glb", listOverride );
    SW_REQUIRE_FALSE( match.hasError() );
    SW_EXPECT_TRUE( match._bMatched );
    SW_EXPECT_EQUAL( 60.0f, match._rule._animationSampleRate );
    SW_EXPECT_TRUE( match._rule._bImportMaterials == SW_FALSE ); // 적지 않은 칸은 규칙 값
    SW_REQUIRE_EQUAL( size_t{ 1 }, match._rule._listLOD.size() );
    SW_EXPECT_EQUAL( 0.25f, match._rule._listLOD[0]._triangleRatio );
    SW_EXPECT_TRUE( match._rule._bFracture == SW_TRUE );
    SW_EXPECT_EQUAL( 20u, match._rule._fracture._pieceCount );
    // 해석한 규칙이 해시 글에 든다 — 예외를 고치면 그 에셋만 다시 임포트한다.
    SW_EXPECT_TRUE( match._rule.makeAnimationHashText() != config.findMatchingRule( "kit/skeleton_minion.glb" )._rule.makeAnimationHashText() );

    const sw::editor::ModelImportRuleMatch defaultMatch = config.resolveRule( "other/horse.glb", { makeOverride( "animationCodec", R"("raw")" ) } );
    SW_REQUIRE_FALSE( defaultMatch.hasError() );
    SW_EXPECT_FALSE( defaultMatch._bMatched );
    SW_EXPECT_EQUAL( sw::string( "raw" ), defaultMatch._rule._animationCodec );

    sw::editor::TextureImportConfig textureConfig;
    SW_REQUIRE_TRUE( textureConfig.loadFromJSONString( R"({ "presets": { "Base": { "format": "BC7" } }, "rules": [ { "name": "All", "inherits": "Base" } ] })" ) );
    const sw::editor::TextureImportRuleMatch textureMatch = textureConfig.resolveRule( "game/textures_raw/blocks.png", { makeOverride( "format", R"("BC1")" ), makeOverride( "generateMips", "false" ) } );
    SW_REQUIRE_FALSE( textureMatch.hasError() );
    SW_EXPECT_TRUE( textureMatch._rule._format == sw::editor::TextureImportFormat::BC1 );
    SW_EXPECT_TRUE( textureMatch._rule._bGenerateMips == SW_FALSE );
}

/**
 * @brief [ModelImportConfigTest] 서술의 모르는 키 · 읽지 못하는 값 · 규칙을 고르는 칸 · JSON 값 하나가 아닌 글은 임포트 오류다(규칙만으로 임포트하지 않는다).
 */
SW_TEST_CASE( ModelImportConfigTest, UnknownOverrideKeyIsAnError )
{
    sw::editor::ModelImportConfig config;
    SW_REQUIRE_TRUE( config.loadFromJSONString( R"({ "rules": [ { "name": "Kit", "filter": { "includePatterns": ["kit/**"] } } ] })" ) );

    SW_EXPECT_LOG( "JSONFields.UnknownField" );
    SW_EXPECT_TRUE( config.resolveRule( "kit/a.glb", { makeOverride( "animationCodek", R"("raw")" ) } ).hasError() );
    SW_EXPECT_LOG( "JSONFields.ValueOutOfRange" );
    SW_EXPECT_TRUE( config.resolveRule( "kit/a.glb", { makeOverride( "animationSampleRate", "0" ) } ).hasError() );
    SW_EXPECT_TRUE( config.resolveRule( "kit/a.glb", { makeOverride( "filter", R"({"includePatterns": ["*"]})" ) } ).hasError() );
    SW_EXPECT_TRUE( config.resolveRule( "kit/a.glb", { makeOverride( "animationSampleRate", R"(60, "name": "x")" ) } ).hasError() );
    SW_EXPECT_TRUE( config.resolveRule( "kit/a.glb", { makeOverride( "animationCodec", "raw" ) } ).hasError() ); // 글 값은 따옴표로 적는다
    SW_EXPECT_TRUE( config.resolveRule( "kit/a.glb", { makeOverride( "seed", "1" ), makeOverride( "seed", "2" ) } ).hasError() );
}

/**
 * @brief [ModelImportConfigTest] 와일드카드 없는 포함 패턴(한 파일 규칙)은 로드 오류다 — 한 파일 예외는 그 에셋의 서술에 든다.
 */
SW_TEST_CASE( ModelImportConfigTest, SingleFilePatternIsRejected )
{
    SW_EXPECT_LOG( "AssetImportConfig.SingleFilePattern" );
    sw::editor::ModelImportConfig config;
    SW_EXPECT_FALSE( config.loadFromJSONString( R"({ "rules": [ { "name": "One", "filter": { "includePatterns": ["common/models_raw/kaykit/skeleton_minion.glb"] } } ] })" ) );
    SW_EXPECT_TRUE( config.getRules().empty() );
    SW_EXPECT_TRUE( config.loadFromJSONString( R"({ "rules": [ { "name": "Kit", "filter": { "includePatterns": ["common/models_raw/kaykit/**"], "excludePatterns": ["*/skeleton_minion.glb"] } } ] })" ) );

    sw::editor::TextureImportConfig textureConfig;
    SW_EXPECT_FALSE( textureConfig.loadFromJSONString( R"({ "rules": [ { "name": "One", "filter": { "includePatterns": ["game/textures_raw/blocks.png"] } } ] })" ) );
}
