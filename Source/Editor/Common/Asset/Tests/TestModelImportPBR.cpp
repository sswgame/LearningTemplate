// ModelImportMaterial — 일반 glTF 의 금속도 · 거칠기 머티리얼 읽기: 바깥 이미지 경로(`../`) · 파일 이름 접미사 · factor · 가림(ORM 에 묶였을 때만).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/ModelImport/ModelImportMaterial.h"
#include "Editor/Common/Asset/ModelImport/ModelImportUtil.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief cgltf 로 읽은 glTF 입니다(버퍼 없이 JSON 만). */
    struct ParsedGltf
    {
        cgltf_data* _pData = nullptr;
        ~ParsedGltf() { cgltf_free( _pData ); }
    };

    /**
     * @brief 내려받은 스캔 에셋처럼 이미지를 원본 옆이 아닌 `textures_raw/` 에 두고 `../` 로 가리키는 glTF 를 만들어 읽습니다.
     * @param bOcclusionInORM true 면 가림 텍스처가 ORM 과 같은 이미지다(glTF 의 ORM 묶음), false 면 가림이 없다(Poly Haven).
     */
    bool parseScanLikeGltf( const AbsolutePath& root, bool bOcclusionInORM, ParsedGltf& outParsed, AbsolutePath& outSourcePath )
    {
        const AbsolutePath textureFolder = root.join( "textures_raw/scan" );
        if ( FileUtil::ensureDirectoryExists( textureFolder ) == false || FileUtil::ensureDirectoryExists( root.join( "models_raw" ) ) == false )
            return false;
        for ( const utf8* pName : { "crate_diff_2k.jpg", "crate_nor_gl_2k.jpg", "crate_arm_2k.jpg" } )
        {
            if ( FileUtil::writeTextFile( textureFolder.join( pName ), pName ) == false )
                return false;
        }
        const string                              occlusion = bOcclusionInORM ? R"(, "occlusionTexture": { "index": 2, "strength": 0.5 })" : "";
        StringBuilder<constant::buffer::kMax2048> json;
        json.appendFormat( R"({ "asset": { "version": "2.0" },
            "images": [ { "uri": "../textures_raw/scan/crate_diff_2k.jpg", "mimeType": "image/jpeg" },
                        { "uri": "../textures_raw/scan/crate_nor_gl_2k.jpg", "mimeType": "image/jpeg" },
                        { "uri": "../textures_raw/scan/crate_arm_2k.jpg", "mimeType": "image/jpeg" } ],
            "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 } ],
            "materials": [ { "name": "Untextured" },
                           { "name": "Wooden Crate", "normalTexture": { "index": 1, "scale": 0.75 },
                             "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 }, "metallicRoughnessTexture": { "index": 2 },
                                                       "baseColorFactor": [ 1, 0.5, 0.25, 1 ], "metallicFactor": 0.2, "roughnessFactor": 0.9 }%# } ] })",
                           occlusion );
        outSourcePath = root.join( "models_raw/crate.gltf" );
        cgltf_options options{};
        return cgltf_parse( &options, json.c_str(), json.size(), &outParsed._pData ) == cgltf_result_success;
    }
} // namespace

/**
 * @brief [ModelImportPBRTest] 첫 텍스처 머티리얼을 읽는다 — `../` 를 푼 이미지 원본 경로 · 바이트 · `_albedo` · `_normal` · `_orm` 접미사 · factor 그대로, 가림 없음은 0
 * @details 원본 경로에 `..` 가 남으면 머티리얼 쓰기가 "이미 textures_raw 의 파일" 을 알아보지 못해 이미지를 한 벌 더 꺼낸다.
 */
SW_TEST_CASE( ModelImportPBRTest, ReadsFirstTexturedMaterialWithResolvedImagePaths )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportPbr" );
    ParsedGltf         parsed;
    AbsolutePath       sourcePath;
    SW_REQUIRE_TRUE( parseScanLikeGltf( root, false, parsed, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImportMaterial::readPBRMaterial( *parsed._pData, sourcePath, result ) );
    const ModelImportPBRMaterial& pbr = result._pbrMaterial;
    SW_REQUIRE_TRUE( pbr._bPresent == SW_TRUE );
    SW_EXPECT_EQUAL( string( "wooden_crate" ), pbr._name );
    SW_EXPECT_EQUAL( string( "wooden_crate_albedo" ), pbr._albedo._fileStem );
    SW_EXPECT_EQUAL( string( "wooden_crate_normal" ), pbr._normal._fileStem );
    SW_EXPECT_EQUAL( string( "wooden_crate_orm" ), pbr._orm._fileStem );
    SW_EXPECT_TRUE( pbr._albedo._sourcePath.find( ".." ) == string::npos );
    SW_EXPECT_TRUE( StringUtil::endsWith( pbr._albedo._sourcePath, "/textures_raw/scan/crate_diff_2k.jpg" ) );
    SW_EXPECT_EQUAL( string( ".jpg" ), pbr._orm._extension );
    const string expectedBytes = "crate_arm_2k.jpg";
    SW_EXPECT_TRUE( pbr._orm._bytes == vector<uint8>( expectedBytes.begin(), expectedBytes.end() ) );
    SW_EXPECT_NEAR( 0.5f, pbr._baseColor._y, 1e-6f );
    SW_EXPECT_NEAR( 0.2f, pbr._metallic, 1e-6f );
    SW_EXPECT_NEAR( 0.9f, pbr._roughness, 1e-6f );
    SW_EXPECT_NEAR( 0.75f, pbr._normalScale, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, pbr._occlusionStrength, 1e-6f );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ModelImportPBRTest] 가림 텍스처가 ORM 과 같은 이미지면 그 strength 를 가림 세기로 쓴다
 */
SW_TEST_CASE( ModelImportPBRTest, OcclusionPackedInORMKeepsStrength )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportPbrAO" );
    ParsedGltf         parsed;
    AbsolutePath       sourcePath;
    SW_REQUIRE_TRUE( parseScanLikeGltf( root, true, parsed, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImportMaterial::readPBRMaterial( *parsed._pData, sourcePath, result ) );
    SW_EXPECT_NEAR( 0.5f, result._pbrMaterial._occlusionStrength, 1e-6f );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ModelImportPBRTest] 본문에 든 이미지(`data:` URI)와 퍼센트 표기 파일 이름을 읽는다 — 흰 텍스처로 넘어가거나 "파일 없음" 이 되지 않는다
 */
SW_TEST_CASE( ModelImportPBRTest, ReadsEmbeddedAndPercentEncodedImages )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportPbrURI" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root.join( "models_raw" ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "models_raw/my tex.png" ), "png" ) );
    const string_view  json       = R"({ "asset": { "version": "2.0" },
        "images": [ { "uri": "data:image/jpeg;base64,YWJj" }, { "uri": "my%20tex.png" } ],
        "textures": [ { "source": 0 }, { "source": 1 } ],
        "materials": [ { "name": "Embedded", "normalTexture": { "index": 1 }, "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ] })";
    const AbsolutePath sourcePath = root.join( "models_raw/embedded.gltf" );
    ParsedGltf         parsed;
    cgltf_options      options{};
    SW_REQUIRE_TRUE( cgltf_parse( &options, json.data(), json.size(), &parsed._pData ) == cgltf_result_success );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImportMaterial::readPBRMaterial( *parsed._pData, sourcePath, result ) );
    SW_EXPECT_TRUE( result._pbrMaterial._albedo._bytes == ( vector<uint8>{ 'a', 'b', 'c' } ) );
    SW_EXPECT_EQUAL( string( ".jpg" ), result._pbrMaterial._albedo._extension );
    SW_EXPECT_TRUE( result._pbrMaterial._normal._bytes == ( vector<uint8>{ 'p', 'n', 'g' } ) );
    SW_EXPECT_TRUE( StringUtil::endsWith( result._pbrMaterial._normal._sourcePath, "/models_raw/my tex.png" ) );
}

/**
 * @brief [ModelImportPBRTest] 원본 해시는 옆에 둔 이미지의 내용을 따르고, 텍스처 원본 폴더의 이미지는 섞지 않는다(그 내용은 텍스처 임포트가 따라간다)
 */
SW_TEST_CASE( ModelImportPBRTest, SourceHashFollowsExternalImages )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportPbrHash" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root.join( "models_raw" ) ) && FileUtil::ensureDirectoryExists( root.join( "textures_raw" ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "models_raw/side tex.png" ), "one" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "textures_raw/shared.png" ), "one" ) );
    const string_view   json = R"({ "asset": { "version": "2.0" }, "images": [ { "uri": "side%20tex.png" }, { "uri": "../textures_raw/shared.png" } ] })";
    const vector<uint8> bytes( json.begin(), json.end() );
    const AbsolutePath  sourcePath = root.join( "models_raw/side.gltf" );
    const uint64        before     = ModelImportUtil::mixExternalFiles( sourcePath, bytes, "rules", 1 );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "textures_raw/shared.png" ), "two" ) );
    SW_EXPECT_EQUAL( before, ModelImportUtil::mixExternalFiles( sourcePath, bytes, "rules", 1 ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "models_raw/side tex.png" ), "two" ) );
    SW_EXPECT_TRUE( before != ModelImportUtil::mixExternalFiles( sourcePath, bytes, "rules", 1 ) );
}

/**
 * @brief [ModelImportPBRTest] 이미지를 꺼내는 모델의 원본 해시는 텍스처 규칙을 고르는 글을 따르고, 꺼내지 않는 모델(텍스처 원본 폴더를 가리킴 · 이미지 없음)은 따르지 않는다
 */
SW_TEST_CASE( ModelImportPBRTest, SourceHashFollowsImageRules )
{
    const AbsolutePath sourcePath = test::makeScratchPath( "ModelImportPbrRules" ).join( "models_raw/a.gltf" );
    const auto         mix        = [&sourcePath]( string_view json, string_view ruleText )
    { return ModelImportUtil::mixExternalFiles( sourcePath, vector<uint8>( json.begin(), json.end() ), ruleText, 1 ); };
    constexpr string_view kEmbedded = R"({ "asset": { "version": "2.0" }, "images": [ { "uri": "data:image/png;base64,AA==" } ] })";
    constexpr string_view kRawOnly  = R"({ "asset": { "version": "2.0" }, "images": [ { "uri": "../textures_raw/shared.png" } ] })";
    constexpr string_view kNoImage  = R"({ "asset": { "version": "2.0" } })";
    SW_EXPECT_TRUE( mix( kEmbedded, "a" ) != mix( kEmbedded, "b" ) );
    SW_EXPECT_EQUAL( mix( kRawOnly, "a" ), mix( kRawOnly, "b" ) );
    SW_EXPECT_EQUAL( mix( kNoImage, "a" ), mix( kNoImage, "b" ) );
}

/**
 * @brief [ModelImportPBRTest] glTF 가 가리키는 이미지 파일이 없으면 흰 텍스처로 넘어가지 않고 실패한다
 */
SW_TEST_CASE( ModelImportPBRTest, MissingImageFileFails )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportPbrMissing" );
    ParsedGltf         parsed;
    AbsolutePath       sourcePath;
    SW_REQUIRE_TRUE( parseScanLikeGltf( root, false, parsed, sourcePath ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( root.join( "textures_raw/scan/crate_nor_gl_2k.jpg" ) ) );

    ModelImportResult result;
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "ModelImport.ImageFileMissing" );
    SW_EXPECT_FALSE( ModelImportMaterial::readPBRMaterial( *parsed._pData, sourcePath, result ) );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
