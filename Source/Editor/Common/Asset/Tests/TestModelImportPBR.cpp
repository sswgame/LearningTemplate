// ModelImportMaterial — 일반 glTF 의 금속도 · 거칠기 머티리얼 읽기: 바깥 이미지 경로(`../`) · 파일 이름 접미사 · factor · 가림(ORM 에 묶였을 때만).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Model/ModelImportMaterial.h"

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
    bool parseScanLikeGltf( const string& root, bool bOcclusionInORM, ParsedGltf& outParsed, string& outSourcePath )
    {
        const string textureFolder = FileUtil::joinPath( root, "textures_raw/scan" );
        if ( FileUtil::ensureDirectoryExists( textureFolder ) == false || FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, "models_raw" ) ) == false )
            return false;
        for ( const utf8* pName : { "crate_diff_2k.jpg", "crate_nor_gl_2k.jpg", "crate_arm_2k.jpg" } )
        {
            if ( FileUtil::writeTextFile( FileUtil::joinPath( textureFolder, pName ), pName ) == false )
                return false;
        }
        const string occlusion = bOcclusionInORM ? R"(, "occlusionTexture": { "index": 2, "strength": 0.5 })" : "";
        const string json      = string( R"({ "asset": { "version": "2.0" },
            "images": [ { "uri": "../textures_raw/scan/crate_diff_2k.jpg", "mimeType": "image/jpeg" },
                        { "uri": "../textures_raw/scan/crate_nor_gl_2k.jpg", "mimeType": "image/jpeg" },
                        { "uri": "../textures_raw/scan/crate_arm_2k.jpg", "mimeType": "image/jpeg" } ],
            "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 } ],
            "materials": [ { "name": "Untextured" },
                           { "name": "Wooden Crate", "normalTexture": { "index": 1, "scale": 0.75 },
                             "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 }, "metallicRoughnessTexture": { "index": 2 },
                                                       "baseColorFactor": [ 1, 0.5, 0.25, 1 ], "metallicFactor": 0.2, "roughnessFactor": 0.9 })" ) +
                            occlusion + " } ] }";
        outSourcePath = FileUtil::joinPath( root, "models_raw/crate.gltf" );
        cgltf_options options{};
        return cgltf_parse( &options, json.data(), json.size(), &outParsed._pData ) == cgltf_result_success;
    }
} // namespace

/**
 * @brief [ModelImportPBRTest] 첫 텍스처 머티리얼을 읽는다 — `../` 를 푼 이미지 원본 경로 · 바이트 · `_albedo` · `_normal` · `_orm` 접미사 · factor 그대로, 가림 없음은 0
 * @details 원본 경로에 `..` 가 남으면 머티리얼 쓰기가 "이미 textures_raw 의 파일" 을 알아보지 못해 이미지를 한 벌 더 꺼낸다.
 */
SW_TEST_CASE( ModelImportPBRTest, ReadsFirstTexturedMaterialWithResolvedImagePaths )
{
    const string root = test::makeScratchPath( "ModelImportPbr" );
    ParsedGltf   parsed;
    string       sourcePath;
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
    const string root = test::makeScratchPath( "ModelImportPbrAO" );
    ParsedGltf   parsed;
    string       sourcePath;
    SW_REQUIRE_TRUE( parseScanLikeGltf( root, true, parsed, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImportMaterial::readPBRMaterial( *parsed._pData, sourcePath, result ) );
    SW_EXPECT_NEAR( 0.5f, result._pbrMaterial._occlusionStrength, 1e-6f );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ModelImportPBRTest] glTF 가 가리키는 이미지 파일이 없으면 흰 텍스처로 넘어가지 않고 실패한다
 */
SW_TEST_CASE( ModelImportPBRTest, MissingImageFileFails )
{
    const string root = test::makeScratchPath( "ModelImportPbrMissing" );
    ParsedGltf   parsed;
    string       sourcePath;
    SW_REQUIRE_TRUE( parseScanLikeGltf( root, false, parsed, sourcePath ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( FileUtil::joinPath( root, "textures_raw/scan/crate_nor_gl_2k.jpg" ) ) );

    ModelImportResult result;
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "ModelImport.ImageFileMissing" );
    SW_EXPECT_FALSE( ModelImportMaterial::readPBRMaterial( *parsed._pData, sourcePath, result ) );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
