// ModelImporter — 머티리얼이 여럿인 일반 glTF: 텍스처 머티리얼이 있으면 머티리얼마다 구간 + PBR 머티리얼, 텍스처 없는 머티리얼만이면 색을 구운 메시 하나.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Model/ModelImportConfig.h"
#include "Editor/Common/Asset/Model/ModelImportMaterial.h"
#include "Editor/Common/Asset/Model/ModelImporter.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /**
     * @brief 삼각형 둘(프리미티브마다 하나, 머티리얼 0 · 1)인 glTF 를 `<root>/models_raw/pair.gltf` 에 씁니다(바깥 `.bin`, 기본색 이미지는 `textures_raw/`).
     * @param bTextured true 면 머티리얼 0 이 기본색 텍스처를 가진다.
     */
    bool writeTwoMaterialGltf( const string& root, bool bTextured, string& outSourcePath )
    {
        const string modelFolder   = FileUtil::joinPath( root, "models_raw" );
        const string textureFolder = FileUtil::joinPath( root, "textures_raw/pair" );
        if ( FileUtil::ensureDirectoryExists( modelFolder ) == false || FileUtil::ensureDirectoryExists( textureFolder ) == false )
            return false;
        if ( FileUtil::writeTextFile( FileUtil::joinPath( textureFolder, "wood.png" ), "wood" ) == false )
            return false;
        // 위치 6 개(삼각형 둘) — 바닥면의 두 삼각형이다.
        const float32 arrPosition[18]{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1 };
        if ( FileUtil::writeFile( FileUtil::joinPath( modelFolder, "pair.bin" ), reinterpret_cast<const uint8*>( arrPosition ), sizeof( arrPosition ) ) == false )
            return false;
        const string texture = bTextured ? R"(, "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 }, "metallicFactor": 0 })" : "";
        const string json    = string( R"({ "asset": { "version": "2.0" }, "scene": 0, "scenes": [ { "nodes": [ 0 ] } ], "nodes": [ { "mesh": 0 } ],
            "buffers": [ { "uri": "pair.bin", "byteLength": 72 } ],
            "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 }, { "buffer": 0, "byteOffset": 36, "byteLength": 36 } ],
            "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 0, 1 ] },
                           { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 0, 1 ] } ],
            "images": [ { "uri": "../textures_raw/pair/wood.png", "mimeType": "image/png" } ],
            "textures": [ { "source": 0 } ],
            "materials": [ { "name": "Wood", "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true)" ) +
                            texture + R"( }, { "name": "Stone", "pbrMetallicRoughness": { "baseColorFactor": [ 0.5, 0.5, 0.5, 1 ] } } ],
            "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 }, { "attributes": { "POSITION": 1 }, "material": 1 } ] } ] })";
        outSourcePath = FileUtil::joinPath( modelFolder, "pair.gltf" );
        return FileUtil::writeTextFile( outSourcePath, json );
    }
} // namespace

/**
 * @brief [ModelImportSectionsTest] 텍스처 머티리얼이 있고 머티리얼이 둘이면 머티리얼마다 구간이 되고 PBR 머티리얼 표가 glTF 순서로 찬다
 * @details 첫 머티리얼만 쓰던 때는 돌 면이 나무 텍스처로 그려졌다(Quaternius 메가키트의 절반이 머티리얼 둘 이상이다).
 */
SW_TEST_CASE( ModelImportSectionsTest, TexturedMultiMaterialGltfSplitsIntoSections )
{
    const string root = test::makeScratchPath( "ModelImportSections" );
    string       sourcePath;
    SW_REQUIRE_TRUE( writeTwoMaterialGltf( root, true, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImporter::readModelAsset( sourcePath, ModelImportRule{}, result ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, result._listPBRMaterial.size() );
    SW_EXPECT_EQUAL( string( "wood" ), result._listPBRMaterial[0]._name );
    SW_EXPECT_EQUAL( string( "wood_albedo" ), result._listPBRMaterial[0]._albedo._fileStem );
    SW_EXPECT_TRUE( result._listPBRMaterial[0]._bAlphaCutoff == SW_TRUE ); // alphaMode MASK — 잎 · 꽃이 네모로 그려지지 않는다
    SW_EXPECT_NEAR( 0.3f, result._listPBRMaterial[0]._alphaCutoff, 1e-6f );
    SW_EXPECT_TRUE( result._listPBRMaterial[0]._bTwoSided == SW_TRUE );
    SW_EXPECT_TRUE( result._listPBRMaterial[1]._bAlphaCutoff == SW_FALSE );
    SW_EXPECT_NEAR( 0.5f, result._listPBRMaterial[1]._baseColor._x, 1e-6f );
    SW_EXPECT_TRUE( result._pbrMaterial._bPresent == SW_FALSE ); // 구간 없는 메시의 머티리얼은 쓰지 않는다
    SW_REQUIRE_EQUAL( size_t{ 2 }, result._listSection.size() );
    SW_EXPECT_EQUAL( string( "wood" ), result._listSection[0]._fileStem );
    SW_EXPECT_EQUAL( string( "stone" ), result._listSection[1]._fileStem );
    SW_EXPECT_EQUAL( uint32{ 3 }, result._listSection[1]._firstIndex );
    SW_EXPECT_EQUAL( uint32{ 3 }, result._listSection[1]._indexCount );
    SW_EXPECT_EQUAL( size_t{ 2 }, result._mesh._listSection.size() );
    // 구간은 원본의 날 머티리얼 이름을 든다(파일 이름 꼴 "wood" 가 아니라 "Wood") — 다시 임포트할 때 이름을 지키는 열쇠다.
    SW_EXPECT_EQUAL( string( "Wood" ), result._listSection[0]._sourceMaterial );
    SW_EXPECT_TRUE( result._mesh._listSection[1]._sourceMaterial == hashed_string( "Stone" ) );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ModelImportSectionsTest] 다시 임포트하면 날 이름 · 가시성이 같은 앞 구간의 이름을 이어받는다
 * @details 원본에 머티리얼이 늘어 파일 이름 번호가 밀려도(wood → wood_1) 외형 데이터가 짚던 구간 이름이 그대로다. 날 이름이 없는 옛 구간 · 다른 새 구간이 이미 쓰는 이름은 받지 않는다.
 */
SW_TEST_CASE( ModelImportSectionsTest, ReimportKeepsSectionNamesByRawNames )
{
    vector<ModelImportSection> listSection( 3 );
    listSection[0]._sectionName      = "wood_1";
    listSection[0]._sourceMaterial   = "Wood";
    listSection[0]._sourceNode       = "Table";
    listSection[1]._sectionName      = "wood";
    listSection[1]._sourceMaterial   = "WoodNew";
    listSection[2]._sectionName      = "stone_tp";
    listSection[2]._sourceMaterial   = "Stone";
    listSection[2]._personVisibility = MeshPersonVisibility::ThirdPersonOnly;

    vector<MeshSection> listPrevious;
    listPrevious.push_back( MeshSection{ hashed_string( "wood" ), 0, 3, MeshPersonVisibility::Both, hashed_string( "Wood" ), hashed_string( "Table" ) } );
    listPrevious.push_back( MeshSection{ hashed_string( "rock" ), 3, 3, MeshPersonVisibility::Both, hashed_string( "Stone" ), hashed_string{} } ); // 가시성이 다르다
    listPrevious.push_back( MeshSection{ hashed_string( "old" ), 6, 3, MeshPersonVisibility::Both } );                                             // 날 이름 없음

    // 첫 구간은 앞 이름 "wood" 를 받고 싶지만 새 둘째 구간이 이미 "wood" 다 — 겹치면 메시가 구간을 잃으므로 받지 않는다.
    SW_EXPECT_EQUAL( uint32{ 0 }, ModelImportMaterial::keepPreviousSectionNames( listSection, listPrevious ) );
    SW_EXPECT_EQUAL( string( "wood_1" ), listSection[0]._sectionName );
    SW_EXPECT_EQUAL( string( "stone_tp" ), listSection[2]._sectionName );

    listSection[1]._sectionName = "woodnew";
    SW_EXPECT_EQUAL( uint32{ 1 }, ModelImportMaterial::keepPreviousSectionNames( listSection, listPrevious ) );
    SW_EXPECT_EQUAL( string( "wood" ), listSection[0]._sectionName );
    SW_EXPECT_EQUAL( string( "woodnew" ), listSection[1]._sectionName );
}

/**
 * @brief [ModelImportSectionsTest] 텍스처 없는 머티리얼만 여럿이면 구간 없이 색을 정점에 구운 메시 하나다(로우폴리 팩의 색 머티리얼)
 */
SW_TEST_CASE( ModelImportSectionsTest, UntexturedMultiMaterialGltfStaysOneMesh )
{
    const string root = test::makeScratchPath( "ModelImportSectionsFlat" );
    string       sourcePath;
    SW_REQUIRE_TRUE( writeTwoMaterialGltf( root, false, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImporter::readModelAsset( sourcePath, ModelImportRule{}, result ) );
    SW_EXPECT_TRUE( result._listPBRMaterial.empty() );
    SW_EXPECT_TRUE( result._listSection.empty() );
    SW_EXPECT_TRUE( result._mesh._listSection.empty() );
    SW_EXPECT_EQUAL( size_t{ 6 }, result._mesh._listVertex.size() );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
